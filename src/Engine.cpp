#include "Engine.hpp"
#include "AliasModel.hpp"
#include "VirtualFileSystem.hpp"
#include "UI.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
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

uint32_t Engine::LoadAliasModel(const std::string& path, engine::vfs::VirtualFileSystem& vfs, std::span<const std::byte> palette) {
    // Check if we already loaded it
    auto it = m_modelCache.find(path);
    if (it != m_modelCache.end()) {
        return it->second;
    }

    // Try to read it from the PAK file
    auto data = vfs.ReadFile(path);
    if (!data) {
        std::cerr << "WARNING: Could not read " << path << " from VFS.\n";
        m_modelCache[path] = 0; // Cache the failure as 0 so we don't spam the VFS
        return 0;
    }

    try {
        engine::AliasModel model(*data, palette);
        uint32_t modelId = m_renderer->UploadAliasModel(model);
        m_modelCache[path] = modelId;
        std::cout << "Cached " << path << " as Model ID " << modelId << "\n";
        return modelId;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse " << path << ": " << e.what() << "\n";
        m_modelCache[path] = 0;
        return 0;
    }
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

        // ---> NEW: Extract Font Atlas from gfx.wad inside pak0.pak
        std::vector<uint8_t> concharsBytes;
        auto wadData = vfs.ReadFile("gfx.wad");
        if (wadData) {
#pragma pack(push, 1)
            struct WadHeader {
                char magic[4];
                int32_t numentries;
                int32_t diroffset;
            };
            struct WadEntry {
                int32_t offset;
                int32_t dsize;
                int32_t size;
                char type;
                char cmprs;
                int16_t dummy;
                char name[16];
            };
#pragma pack(pop)

            if (wadData->size() >= sizeof(WadHeader)) {
                const WadHeader* header = reinterpret_cast<const WadHeader*>(wadData->data());
                if (std::string(header->magic, 4) == "WAD2") {
                    int32_t dirOffset = header->diroffset;
                    int32_t numEntries = header->numentries;
                    
                    for (int32_t i = 0; i < numEntries; i++) {
                        size_t entryPos = dirOffset + i * sizeof(WadEntry);
                        if (entryPos + sizeof(WadEntry) <= wadData->size()) {
                            const WadEntry* entry = reinterpret_cast<const WadEntry*>(wadData->data() + entryPos);
                            std::string entryName(entry->name);
                            // Convert to lowercase for comparison
                            std::transform(entryName.begin(), entryName.end(), entryName.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            if (entryName == "conchars") {
                                if (entry->offset + entry->size <= wadData->size()) {
                                    concharsBytes.resize(entry->size);
                                    std::memcpy(concharsBytes.data(), wadData->data() + entry->offset, entry->size);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        } else {
            std::cerr << "ERROR: Failed to read gfx.wad from VFS! Error code: " 
                      << static_cast<int>(wadData.error()) << "\n";
        }

        if (!concharsBytes.empty() && paletteData) {
            engine::TextureData fontData;
            fontData.name = "conchars";
            fontData.width = 128;
            fontData.height = 128;
            fontData.pixelsRGBA.resize(128 * 128 * 4);
            
            const uint8_t* pal = reinterpret_cast<const uint8_t*>(paletteData->data());
            
            for (size_t i = 0; i < 16384; ++i) { // 128x128 = 16384 bytes
                uint8_t colorIndex = concharsBytes[i];
                
                // Font uses index 0 for transparent backgrounds!
                bool isTransparent = (colorIndex == 0); 
                
                fontData.pixelsRGBA[i * 4 + 0] = static_cast<std::byte>(pal[colorIndex * 3 + 0]);
                fontData.pixelsRGBA[i * 4 + 1] = static_cast<std::byte>(pal[colorIndex * 3 + 1]);
                fontData.pixelsRGBA[i * 4 + 2] = static_cast<std::byte>(pal[colorIndex * 3 + 2]);
                fontData.pixelsRGBA[i * 4 + 3] = isTransparent ? std::byte{0} : std::byte{255};
            }
            m_renderer->UploadFont(fontData);
            std::cout << "Successfully uploaded Quake Font (extracted from gfx.wad)\n";
        } else if (concharsBytes.empty()) {
            std::cerr << "ERROR: CONCHARS lump was not found inside gfx.wad!\n";
        }

        // Initialize Physics and Player
        m_physics = std::make_unique<Physics>(m_map.get());
        m_player = std::make_unique<Player>(m_physics.get(), m_camera.get());
    } else {
        throw std::runtime_error("Failed to load map or palette data.");
    }
        // Spawn the Camera using Entity data
        glm::vec3 spawnOrigin(0.0f, 0.0f, 0.0f);
        float spawnAngle = 0.0f;
        bool foundSpawn = false;

        // ---> NEW: Dictionary of Quake 1 Classnames to MDL files
        std::unordered_map<std::string, std::string> classnameToMdl = {
            {"monster_army", "progs/soldier.mdl"},
            {"monster_dog", "progs/dog.mdl"},
            {"monster_ogre", "progs/ogre.mdl"},
            {"monster_demon1", "progs/demon.mdl"},
            {"monster_shambler", "progs/shambler.mdl"},
            {"monster_knight", "progs/knight.mdl"},
            {"monster_zombie", "progs/zombie.mdl"},
            {"item_armor1", "progs/armor.mdl"},
            {"item_armor2", "progs/armor.mdl"},
            {"item_armorInv", "progs/armor.mdl"},
            {"weapon_nailgun", "progs/g_nail.mdl"},
            {"weapon_supershotgun", "progs/g_shot.mdl"},
            {"weapon_supernailgun", "progs/g_nail2.mdl"},
            {"weapon_rocketlauncher", "progs/g_rock.mdl"},
            {"weapon_grenadelauncher", "progs/g_rock.mdl"},
            {"weapon_lightning", "progs/g_light.mdl"},
            {"item_shells", "progs/m_shell.mdl"},
            {"item_spikes", "progs/m_nail.mdl"},
            {"item_rockets", "progs/m_rock.mdl"},
            {"item_cells", "progs/m_light.mdl"},
            {"item_health", "progs/m_health.mdl"} // Megahealth
        };

        for (const auto& ent : m_map->GetEntities()) {
            std::string cls = ent.GetClassname();

            // 1. Spawn Player
            if (cls == "info_player_start") {
                spawnOrigin = ent.GetVector("origin");
                spawnAngle = ent.GetFloat("angle");
                foundSpawn = true;
            }
            
            // 2. Brush Entities (Doors/Elevators)
            std::string modelStr = ent.GetString("model");
            if (!modelStr.empty() && modelStr[0] == '*') {
                RenderEntity rent;
                rent.type = EntityModelType::BspBrush;
                rent.modelId = std::stoi(modelStr.substr(1));
                rent.origin = ent.GetVector("origin", glm::vec3(0.0f));
                rent.angles = glm::vec3(0.0f, 0.0f, 0.0f); 
                rent.frame = 0;
                m_renderEntities.push_back(rent);
            }

            // 3. Dynamic Alias Models (Monsters & Items)
            auto it = classnameToMdl.find(cls);
            if (it != classnameToMdl.end()) {
                uint32_t mdlId = LoadAliasModel(it->second, vfs, *paletteData);
                
                if (mdlId != 0) {
                    RenderEntity rent;
                    rent.type = EntityModelType::Alias;
                    rent.modelId = mdlId;
                    
                    // Most Quake models are anchored at the floor
                    rent.origin = ent.GetVector("origin", glm::vec3(0.0f));
                    
                    // Point entities use 'angle' for visual Yaw rotation
                    rent.angles = glm::vec3(0.0f, ent.GetFloat("angle", 0.0f), 0.0f);
                    
                    rent.frame = 0;
                    rent.nextFrame = 1;
                    
                    // Give them a random starting interpolation so they don't animate in perfect unison
                    rent.interp = static_cast<float>(rand() % 100) / 100.0f;

                    m_renderEntities.push_back(rent);
                }
            }
        }

        if (foundSpawn) {
            m_player->Spawn(spawnOrigin, spawnAngle);
            std::cout << "Player spawned at: " << spawnOrigin.x << ", " 
                      << spawnOrigin.y << ", " << spawnOrigin.z << "\n";
        } else {
            std::cerr << "WARNING: No info_player_start found. Spawning at 0,0,0.\n";
        }

        // Load the shotgun view model
        uint32_t vShotId = LoadAliasModel("progs/v_shot.mdl", vfs, *paletteData);
        m_viewModel.type = EntityModelType::Alias;
        m_viewModel.modelId = vShotId;
        m_viewModel.origin = glm::vec3(0.0f);
        m_viewModel.angles = glm::vec3(0.0f);
        m_viewModel.frame = 0;
        m_viewModel.nextFrame = 0; // Don't animate the gun just yet
        m_viewModel.interp = 0.0f;

    m_console = std::make_unique<Console>();
    SDL_StartTextInput(m_window->GetHandle()); // Tell OS to capture text!

    // ========================================================================
    // ---> NEW: Register Console Commands
    // ========================================================================
    
    // Noclip Command
    m_console->RegisterCommand("noclip", [this](const std::vector<std::string>& args) {
        if (m_player) {
            m_player->ToggleNoclip();
            if (m_player->IsNoclip()) {
                m_console->Print("noclip ON");
            } else {
                m_console->Print("noclip OFF");
            }
        }
    });

    // Quit Command
    m_console->RegisterCommand("quit", [this](const std::vector<std::string>& args) {
        m_console->Print("Quitting...");
        m_isRunning = false; // This will safely break the MainLoop
    });

    // Clear Command (Bonus!)
    m_console->RegisterCommand("clear", [this](const std::vector<std::string>& args) {
        m_console->Clear();
    });

    m_isRunning = true;
}

void Engine::Run() {
    MainLoop();
}

void Engine::MainLoop() {
    m_window->CaptureMouse(true);
    uint64_t lastTime = SDL_GetTicks();

    while (m_isRunning) {
        // Pass the console to PollEvents
        m_window->PollEvents(m_isRunning, m_player.get(), m_console.get());

        // 2. Calculate Delta Time
        uint64_t currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        // 3. Process Input
        UserCmd cmd{};
        cmd.msec = deltaTime;
        cmd.yaw = m_camera->GetYaw();
        cmd.pitch = m_camera->GetPitch();
        cmd.forwardmove = 0.0f;
        cmd.sidemove = 0.0f;
        cmd.upmove = 0.0f;

        // ---> NEW: Block game input if the console is open!
        if (!m_console->IsActive()) {
            float mouseX = 0.0f, mouseY = 0.0f;
            SDL_GetRelativeMouseState(&mouseX, &mouseY);
            m_player->ProcessMouse(mouseX, mouseY);

            cmd.yaw = m_camera->GetYaw();     
            cmd.pitch = m_camera->GetPitch(); 

            const bool* keys = SDL_GetKeyboardState(NULL);
            if (keys[SDL_SCANCODE_W]) cmd.forwardmove += 400.0f;
            if (keys[SDL_SCANCODE_S]) cmd.forwardmove -= 400.0f;
            if (keys[SDL_SCANCODE_D]) cmd.sidemove += 350.0f;
            if (keys[SDL_SCANCODE_A]) cmd.sidemove -= 350.0f;
            if (keys[SDL_SCANCODE_SPACE]) cmd.upmove = 400.0f;
        }

        // Player Physics Tick using UserCmd
        m_player->TickPhysics(cmd, m_renderEntities);

        // ========================================================================
        // Entity Simulation (The Game Tick)
        // ========================================================================
        float animationSpeed = 10.0f; // 10 FPS animations
        
        for (auto& rent : m_renderEntities) {
            if (rent.type == EntityModelType::Alias) {
                // Animate the models
                rent.interp += animationSpeed * deltaTime;
                if (rent.interp >= 1.0f) {
                    rent.interp -= 1.0f;
                    rent.frame = rent.nextFrame;
                    rent.nextFrame = rent.frame + 1;
                }
            }
        }

        // ---> NEW: Generate Console Geometry instead of the Test Quad
        std::vector<UIVertex> uiVerts;
        m_console->GenerateGeometry(uiVerts, 1280.0f, 720.0f);

        // 4. Render
        m_renderer->DrawFrame(*m_camera, *m_map, m_renderEntities, &m_viewModel, uiVerts);
    }
}

} // namespace engine