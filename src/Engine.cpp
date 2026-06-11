#include "Engine.hpp"
#include "AliasModel.hpp"
#include "VirtualFileSystem.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <SDL3/SDL.h>

// Helper functions for locating data
static std::filesystem::path find_data_directory(const std::filesystem::path& start, const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> starts = { start, exeDir };
    for (const auto& s : starts) {
        if (s.empty()) continue;
        std::filesystem::path p = s;
        while (true) {
            std::filesystem::path cand = p / "data";
            if (std::filesystem::exists(cand) && std::filesystem::is_directory(cand) &&
                std::filesystem::exists(cand / "pak0.pak")) {
                return cand;
            }
            auto parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    }
    std::filesystem::path p = std::filesystem::current_path();
    while (true) {
        std::filesystem::path cand = p / "data";
        if (std::filesystem::exists(cand) && std::filesystem::is_directory(cand) &&
            std::filesystem::exists(cand / "pak0.pak")) {
            return cand;
        }
        auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

namespace engine {

Engine::Engine() {
    Init();
}

Engine::~Engine() {
    std::cout << "Shutting down Engine...\n";
    // unique_ptrs automatically tear down Renderer, then Window.
}

void Engine::Init() {
    std::cout << "Initializing Engine...\n";
    m_window = std::make_unique<Window>("Vulkan Quake Engine", 1280, 720);
    m_camera = std::make_unique<Camera>(glm::vec3(400.0f, 400.0f, 100.0f));

    std::filesystem::path exeDir;
    const char* basePath = SDL_GetBasePath();
    if (basePath) {
        exeDir = std::filesystem::path(basePath);
    } else {
        exeDir = std::filesystem::current_path();
    }

    // Initialize Renderer (Vulkan context)
    m_renderer = std::make_unique<Renderer>(m_window.get(), exeDir.string());

    // Initialize VFS
    std::filesystem::path dataPath = find_data_directory(std::filesystem::current_path(), exeDir);
    if (dataPath.empty()) {
        std::cerr << "WARNING: data directory with pak0.pak not found by search.\n";
        dataPath = std::filesystem::current_path() / "data"; 
    }

    engine::vfs::VirtualFileSystem vfs(dataPath);
    if (!vfs.MountPak("pak0.pak")) {
        throw std::runtime_error("ERROR: Could not find or read pak0.pak!");
    }

    auto paletteData = vfs.ReadFile("gfx/palette.lmp");
    auto mapData = vfs.ReadFile("maps/e1m1.bsp");



    if (mapData && paletteData) {
        m_map = std::make_unique<Map>(std::move(*mapData), *paletteData);
        // Hand the map over to the Renderer so it can upload everything to VRAM
        m_renderer->UploadMap(*m_map);

        // ---> NEW: Initialize Physics and Player
        m_physics = std::make_unique<Physics>(m_map.get());
        m_player = std::make_unique<Player>(m_physics.get(), m_camera.get());
    } else {
        throw std::runtime_error("Failed to load map or palette data.");
    }
        // Spawn the Camera using Entity data
        glm::vec3 spawnOrigin(0.0f, 0.0f, 0.0f);
        float spawnAngle = 0.0f;
        bool foundSpawn = false;

        for (const auto& ent : m_map->GetEntities()) {
            std::string cls = ent.GetClassname();

            // 1. Spawn Player
            if (cls == "info_player_start") {
                spawnOrigin = ent.GetVector("origin");
                spawnAngle = ent.GetFloat("angle");
                foundSpawn = true;
            }
            
            // 2. Simulate Server sending Brush Entities to the Client.
            // Any entity with a "model" key starting with "*" is a BSP Sub-Model.
            std::string modelStr = ent.GetString("model");
            if (!modelStr.empty() && modelStr[0] == '*') {
                RenderEntity rent;
                rent.modelId = std::stoi(modelStr.substr(1));
                rent.type = EntityModelType::BspBrush;
                rent.origin = ent.GetVector("origin", glm::vec3(0.0f));
                
                // rush entities are already rotated correctly in the BSP vertex data.
                // The "angle" key dictates the logic of which way it slides when opened, NOT its visual rotation
                rent.angles = glm::vec3(0.0f, 0.0f, 0.0f); 
                
                rent.frame = 0;
                m_renderEntities.push_back(rent);
            }
        }

        if (foundSpawn) {
            // ---> NEW: Use Player::Spawn!
            m_player->Spawn(spawnOrigin, spawnAngle);
            std::cout << "Player spawned at: " << spawnOrigin.x << ", " 
                      << spawnOrigin.y << ", " << spawnOrigin.z << "\n";
        } else {
            std::cerr << "WARNING: No info_player_start found. Spawning at 0,0,0.\n";
        }

    // Change "armor.mdl" to "shambler.mdl"
    auto monsterData = vfs.ReadFile("progs/shambler.mdl");
    if (monsterData && paletteData) {
        engine::AliasModel monster(*monsterData, *paletteData);
        uint32_t monsterId = m_renderer->UploadAliasModel(monster);

        float angleRad = glm::radians(spawnAngle);
        glm::vec3 forwardDir(std::cos(angleRad), std::sin(angleRad), 0.0f);
        
        RenderEntity monsterEnt;
        monsterEnt.type = EntityModelType::Alias;
        monsterEnt.modelId = monsterId;
        monsterEnt.origin = spawnOrigin + (forwardDir * 200.0f); 
        
        // Face him towards the player
        monsterEnt.angles = glm::vec3(0.0f, spawnAngle + 180.0f, 0.0f); 
        
        monsterEnt.frame = 0;
        monsterEnt.nextFrame = 1;
        monsterEnt.interp = 0.0f;
        
        m_renderEntities.push_back(monsterEnt);
        std::cout << "Successfully uploaded and spawned shambler.mdl!\n";
    }

    m_isRunning = true;
}

void Engine::Run() {
    MainLoop();
}

void Engine::MainLoop() {
    m_window->CaptureMouse(true);
    uint64_t lastTime = SDL_GetTicks();

    while (m_isRunning) {
        // 1. Process Window Events
        m_window->PollEvents(m_isRunning);

        // 2. Calculate Delta Time
        uint64_t currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        // 3. Process Input
        float mouseX = 0.0f, mouseY = 0.0f;
        SDL_GetRelativeMouseState(&mouseX, &mouseY);
        m_player->ProcessMouse(mouseX, mouseY); // <--- Goes to player now

        const bool* keys = SDL_GetKeyboardState(NULL);
        
        // Populate UserCmd (Quake's player movement command)
        UserCmd cmd{};
        cmd.msec = deltaTime;
        cmd.yaw = m_camera->GetYaw();

        // Standard movement scales
        // cl_forwardspeed = 200 * cl_movespeedkey(2.0) = 400
        // cl_sidespeed = 350
        if (keys[SDL_SCANCODE_W]) cmd.forwardmove += 400.0f;
        if (keys[SDL_SCANCODE_S]) cmd.forwardmove -= 400.0f;
        
        // ---> FIX: Asymmetric sidemove!
        if (keys[SDL_SCANCODE_D]) cmd.sidemove += 350.0f;
        if (keys[SDL_SCANCODE_A]) cmd.sidemove -= 350.0f;
        
        // Handle jump using Spacebar
        if (keys[SDL_SCANCODE_SPACE]) cmd.upmove = 400.0f;

        // ---> NEW: Player Physics Tick using UserCmd
        m_player->TickPhysics(cmd);

        // ========================================================================
        // ---> NEW: Entity Simulation (The Game Tick)
        // ========================================================================
        float animationSpeed = 10.0f; // 10 FPS
        for (auto& rent : m_renderEntities) {
            if (rent.type == EntityModelType::Alias) {
                // Animate the Shambler!
                rent.interp += animationSpeed * deltaTime;
                if (rent.interp >= 1.0f) {
                    rent.interp -= 1.0f;
                    rent.frame = rent.nextFrame;
                    rent.nextFrame = rent.frame + 1;
                    // Note: We clamp this inside DrawFrame so it loops perfectly!
                }
            }
        }
        // <--- END NEW

        // 4. Render
        m_renderer->DrawFrame(*m_camera, *m_map, m_renderEntities);
    }
}

} // namespace engine