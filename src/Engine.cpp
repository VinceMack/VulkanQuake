#include "Engine.hpp"
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
    } else {
        throw std::runtime_error("Failed to load map or palette data.");
    }
        // Spawn the Camera using Entity data
        glm::vec3 spawnOrigin(0.0f, 0.0f, 0.0f);
        float spawnAngle = 0.0f;
        bool foundSpawn = false;

        for (const auto& ent : m_map->GetEntities()) {
            if (ent.GetClassname() == "info_player_start") {
                spawnOrigin = ent.GetVector("origin");
                spawnAngle = ent.GetFloat("angle");
                foundSpawn = true;
                break;
            }
        }

        if (foundSpawn) {
            // Quake maps origins to the floor where the player stands.
            // We need to raise the camera to "eye level" (roughly 22 units up in Quake).
            spawnOrigin.z += 22.0f; 
            m_camera->SetPositionAndYaw(spawnOrigin, spawnAngle);
            std::cout << "Player spawned at: " << spawnOrigin.x << ", " 
                      << spawnOrigin.y << ", " << spawnOrigin.z << "\n";
        } else {
            std::cerr << "WARNING: No info_player_start found. Spawning at 0,0,0.\n";
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
        m_camera->ProcessMouse(mouseX, mouseY);

        const bool* keys = SDL_GetKeyboardState(NULL);
        float moveForward = 0.0f, moveRight = 0.0f;
        if (keys[SDL_SCANCODE_W]) moveForward += 1.0f;
        if (keys[SDL_SCANCODE_S]) moveForward -= 1.0f;
        if (keys[SDL_SCANCODE_D]) moveRight += 1.0f;
        if (keys[SDL_SCANCODE_A]) moveRight -= 1.0f;
        
        m_camera->ProcessKeyboard(moveForward, moveRight, deltaTime);

        // 4. Render
        m_renderer->DrawFrame(*m_camera, *m_map);
    }
}

} // namespace engine