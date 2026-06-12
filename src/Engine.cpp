#include "Engine.hpp"
#include "AliasModel.hpp"
#include "VirtualFileSystem.hpp"
#include "UI.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <cctype>
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

uint32_t Engine::LoadAliasModel(const std::string& path, std::span<const std::byte> palette) {
    // Check if we already loaded it
    auto it = m_modelCache.find(path);
    if (it != m_modelCache.end()) {
        return it->second;
    }

    // Try to read it from the PAK file
    auto data = m_vfs->ReadFile(path);
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

    // Initialize lightstyles
    for (int i = 0; i < 64; ++i) m_lightstyles[i] = "m";
    m_lightstyles[0] = "m"; // Normal
    m_lightstyles[1] = "mmnmmommommnonmmonqnmmo"; // Flicker
    m_lightstyles[2] = "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba"; // Slow strong pulse
    m_lightstyles[3] = "mmmmmaaaaammmmmaaaaaabcdefgabcdefg"; // Candle
    m_lightstyles[4] = "mamamamamama"; // Fast strobe
    m_lightstyles[5] = "jklmnopqrstuvwxyzyxwvutsrqponmlkj"; // Gentle pulse
    m_lightstyles[6] = "nmonqnmomnmomomno"; // Flicker 2
    m_lightstyles[7] = "mmmaaaabcdefgmmmmaaaammmaamm"; // Candle 2
    m_lightstyles[8] = "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa"; // Candle 3
    m_lightstyles[9] = "aaaaaaaazzzzzzzz"; // Slow strobe
    m_lightstyles[10] = "mmamammmmammamamaaamammma"; // Fluorescent
    m_lightstyles[11] = "abcdefghijklmnopqrs"; // Slow pulse (no fade back)

    m_window = std::make_unique<Window>("VulkanQuake", 1280, 720);
    m_camera = std::make_unique<Camera>(glm::vec3(0.0f));

    std::filesystem::path exeDir;
    const char* basePath = SDL_GetBasePath();
    if (basePath) exeDir = std::filesystem::path(basePath);
    else exeDir = std::filesystem::current_path();

    m_renderer = std::make_unique<Renderer>(m_window.get(), exeDir.string());

    // 1. Initialize VFS as a member variable
    std::filesystem::path dataPath = find_data_directory(std::filesystem::current_path(), exeDir);
    if (dataPath.empty()) dataPath = std::filesystem::current_path() / "data"; 

    m_vfs = std::make_unique<engine::vfs::VirtualFileSystem>(dataPath);
    if (!m_vfs->MountPak("pak0.pak")) {
        throw std::runtime_error("ERROR: Could not find or read pak0.pak!");
    }

    // 2. Initialize Console
    m_console = std::make_unique<Console>();
    SDL_StartTextInput(m_window->GetHandle());

    // 3. Register Console Commands
    m_console->RegisterCommand("noclip", [this](const std::vector<std::string>& args) {
        if (m_player) {
            m_player->ToggleNoclip();
            m_console->Print(m_player->IsNoclip() ? "noclip ON" : "noclip OFF");
        }
    });

    m_console->RegisterCommand("quit", [this](const std::vector<std::string>& args) {
        m_isRunning = false; 
    });
    
    // ---> NEW: Map Command!
    m_console->RegisterCommand("map", [this](const std::vector<std::string>& args) {
        if (args.empty()) {
            m_console->Print("Usage: map <filename>");
            return;
        }
        LoadMap(args[0]); // Execute our new runtime loader!
    });

    m_console->RegisterCommand("clear", [this](const std::vector<std::string>& args) {
        m_console->Clear();
    });

    // ---> NEW: Triggers Command
    m_console->RegisterCommand("triggers", [this](const std::vector<std::string>& args) {
        m_showTriggers = !m_showTriggers;
        
        // Pass the state to the Renderer
        if (m_renderer) {
            m_renderer->SetShowTriggers(m_showTriggers);
        }
        
        m_console->Print(m_showTriggers ? "Trigger Visualization: ON" : "Trigger Visibility: OFF");
    });

    // 4. Load UI Font and View Model
    auto paletteData = m_vfs->ReadFile("gfx/palette.lmp");
    
    // Extract Font Atlas from gfx.wad inside pak0.pak
    std::vector<uint8_t> concharsBytes;
    auto wadData = m_vfs->ReadFile("gfx.wad");
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
    }

    if (paletteData) {
        uint32_t vShotId = LoadAliasModel("progs/v_shot.mdl", *paletteData);
        m_viewModel.type = EntityModelType::Alias;
        m_viewModel.modelId = vShotId;
        m_viewModel.origin = glm::vec3(0.0f);
        m_viewModel.angles = glm::vec3(0.0f);
        m_viewModel.frame = 0;
        m_viewModel.nextFrame = 0;
        m_viewModel.interp = 0.0f;
    }

    // 5. Load the initial map
    if (!LoadMap("e1m1")) {
        throw std::runtime_error("Failed to load initial map e1m1");
    }

    m_isRunning = true;
}

bool Engine::LoadMap(const std::string& mapName) {
    std::cout << "Loading " << mapName << "\n";
    std::string bspPath = "maps/" + mapName + ".bsp";
    auto mapData = m_vfs->ReadFile(bspPath);
    
    if (!mapData) {
        m_console->Print("ERROR: Could not find map " + mapName);
        return false;
    }

    auto paletteData = m_vfs->ReadFile("gfx/palette.lmp");
    if (!paletteData) return false;

    // 1. Unload old map from the GPU
    m_renderer->UnloadMap();

    // 2. Parse and Upload new map
    m_map = std::make_unique<Map>(std::move(*mapData), *paletteData);
    m_renderer->UploadMap(*m_map);

    // 3. Clear dynamic entities
    m_renderEntities.clear();

    // 4. Rebuild Physics and Player (Critical! Prevents dangling m_map pointers)
    m_physics = std::make_unique<Physics>(m_map.get());
    m_player = std::make_unique<Player>(m_physics.get(), m_camera.get());

    // 5. Parse Entities
    glm::vec3 spawnOrigin(0.0f);
    float spawnAngle = 0.0f;
    bool foundSpawn = false;

    std::unordered_map<std::string, std::string> classnameToMdl = {
        {"monster_army", "progs/soldier.mdl"}, {"monster_dog", "progs/dog.mdl"},
        {"monster_ogre", "progs/ogre.mdl"}, {"monster_demon1", "progs/demon.mdl"},
        {"monster_shambler", "progs/shambler.mdl"}, {"monster_knight", "progs/knight.mdl"},
        {"monster_zombie", "progs/zombie.mdl"}, {"item_armor1", "progs/armor.mdl"},
        {"item_armor2", "progs/armor.mdl"}, {"item_armorInv", "progs/armor.mdl"},
        {"weapon_nailgun", "progs/g_nail.mdl"}, {"weapon_supershotgun", "progs/g_shot.mdl"},
        {"weapon_supernailgun", "progs/g_nail2.mdl"}, {"weapon_rocketlauncher", "progs/g_rock.mdl"},
        {"weapon_grenadelauncher", "progs/g_rock.mdl"}, {"weapon_lightning", "progs/g_light.mdl"},
        {"item_shells", "progs/m_shell.mdl"}, {"item_spikes", "progs/m_nail.mdl"},
        {"item_rockets", "progs/m_rock.mdl"}, {"item_cells", "progs/m_light.mdl"},
        {"item_health", "progs/m_health.mdl"},
        // ---> NEW: Flame Models!
        {"light_torch_small_walltorch", "progs/flame.mdl"},
        {"light_flame_large_yellow", "progs/flame2.mdl"},
        {"light_flame_small_yellow", "progs/flame2.mdl"},
        {"light_flame_small_white", "progs/flame2.mdl"}
    };

    for (const auto& ent : m_map->GetEntities()) {
        std::string cls = ent.GetClassname();

        if (cls == "info_player_start") {
            spawnOrigin = ent.GetVector("origin");
            spawnAngle = ent.GetFloat("angle");
            foundSpawn = true;
        }
        
        std::string modelStr = ent.GetString("model");
        if (!modelStr.empty() && modelStr[0] == '*') {
            RenderEntity rent;
            rent.type = EntityModelType::BspBrush;
            rent.modelId = std::stoi(modelStr.substr(1));
            rent.origin = ent.GetVector("origin", glm::vec3(0.0f));
            rent.angles = glm::vec3(0.0f, 0.0f, 0.0f); 
            rent.frame = 0;
            
            std::string cls = ent.GetClassname();
            rent.isSolid = true;
            rent.isVisible = true;
            rent.isTrigger = false;
            rent.brushState = BrushState::Static; 

            // ---> NEW: Extract Targeting Strings
            rent.targetname = ent.GetString("targetname");
            rent.target = ent.GetString("target");
            
            // In Quake, if an entity has a targetname, it ignores player touch!
            rent.requireTrigger = !rent.targetname.empty(); 

            if (cls.find("trigger_") != std::string::npos) {
                rent.isSolid = false;
                rent.isVisible = false;
                rent.isTrigger = true;
                if (cls == "trigger_changelevel") {
                    rent.triggerTarget = ent.GetString("map");
                }
            } else if (cls == "func_illusionary") {
                rent.isSolid = false;
            } 
            // ---> UPDATED: Added func_button and dynamic defaults
            else if (cls == "func_door" || cls == "func_water" || cls == "func_door_secret" || cls == "func_button") {
                rent.brushState = BrushState::Closed;
                rent.pos1 = rent.origin; 
                
                // Buttons have different defaults than doors!
                float defaultSpeed = (cls == "func_button") ? 40.0f : 100.0f;
                float defaultWait  = (cls == "func_button") ? 1.0f : 3.0f;
                float defaultLip   = (cls == "func_button") ? 4.0f : 8.0f;

                float angle = ent.GetFloat("angle", 0.0f);
                rent.speed = ent.GetFloat("speed", defaultSpeed);
                rent.wait = ent.GetFloat("wait", defaultWait);
                float lip = ent.GetFloat("lip", defaultLip);
                
                glm::vec3 dir(0.0f);
                if (angle == -1.0f) dir = glm::vec3(0.0f, 0.0f, 1.0f);       // UP
                else if (angle == -2.0f) dir = glm::vec3(0.0f, 0.0f, -1.0f); // DOWN
                else {
                    dir.x = std::cos(glm::radians(angle));
                    dir.y = std::sin(glm::radians(angle));
                }
                
                const auto& bspModel = m_map->GetBspModel(rent.modelId);
                glm::vec3 extents = glm::vec3(bspModel.maxs[0] - bspModel.mins[0],
                                              bspModel.maxs[1] - bspModel.mins[1],
                                              bspModel.maxs[2] - bspModel.mins[2]);
                
                float moveDist = std::abs(glm::dot(extents, dir)) - lip;
                rent.pos2 = rent.pos1 + (dir * moveDist); 
                rent.stateTimer = 0.0f;
            }

            const auto& bspModel = m_map->GetBspModel(rent.modelId);
            rent.localMins = glm::vec3(bspModel.mins[0], bspModel.mins[1], bspModel.mins[2]);
            rent.localMaxs = glm::vec3(bspModel.maxs[0], bspModel.maxs[1], bspModel.maxs[2]);

            m_renderEntities.push_back(rent);
        }

        auto it = classnameToMdl.find(cls);
        if (it != classnameToMdl.end()) {
            uint32_t mdlId = LoadAliasModel(it->second, *paletteData);
            if (mdlId != 0) {
                RenderEntity rent;
                rent.type = EntityModelType::Alias;
                rent.modelId = mdlId;
                rent.origin = ent.GetVector("origin", glm::vec3(0.0f));
                rent.angles = glm::vec3(0.0f, ent.GetFloat("angle", 0.0f), 0.0f);
                rent.frame = 0;
                rent.nextFrame = 1;
                rent.interp = static_cast<float>(rand() % 100) / 100.0f;
                m_renderEntities.push_back(rent);
            }
        }
    }

    // 6. Spawn the player
    if (foundSpawn) {
        m_player->Spawn(spawnOrigin, spawnAngle);
    } else {
        m_player->Spawn(glm::vec3(0.0f), 0.0f);
        m_console->Print("WARNING: No spawn point found.");
    }

    return true;
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

        // ---> NEW: Check if the player stepped into a trigger
        std::string levelTransition = m_player->CheckTriggers(m_renderEntities);
        if (!levelTransition.empty()) {
            m_console->Print("Triggered Changelevel: " + levelTransition);
            LoadMap(levelTransition);
            continue; // Safely restart the while-loop for the new map!
        }

        // ========================================================================
        // Entity Simulation (The Game Tick)
        // ========================================================================
        float animationSpeed = 10.0f; 
        
        glm::vec3 pTouchMins = m_player->GetPosition() + glm::vec3(-48.0f, -48.0f, -10.0f);
        glm::vec3 pTouchMaxs = m_player->GetPosition() + glm::vec3(48.0f, 48.0f, 66.0f);

        // We use a list to queue up events so we don't modify the state of other entities 
        // while we are currently iterating through the array!
        std::vector<std::string> firedEvents;

        for (auto& rent : m_renderEntities) {
            if (rent.type == EntityModelType::Alias) {
                rent.interp += animationSpeed * deltaTime;
                if (rent.interp >= 1.0f) {
                    rent.interp -= 1.0f;
                    rent.frame = rent.nextFrame;
                    rent.nextFrame = rent.frame + 1;
                }
            } 
            else if (rent.type == EntityModelType::BspBrush) {
                
                // 1. Proximity Trigger check
                // ---> NEW: We ONLY allow touch triggers if the entity doesn't have a targetname!
                if (rent.brushState == BrushState::Closed && !rent.requireTrigger) {
                    glm::vec3 dMins = rent.GetAbsMins();
                    glm::vec3 dMaxs = rent.GetAbsMaxs();
                    
                    if (pTouchMins.x <= dMaxs.x && pTouchMaxs.x >= dMins.x &&
                        pTouchMins.y <= dMaxs.y && pTouchMaxs.y >= dMins.y &&
                        pTouchMins.z <= dMaxs.z && pTouchMaxs.z >= dMins.z) {
                        
                        rent.brushState = BrushState::Opening; 
                        
                        // ---> NEW: If this was a button, fire its target!
                        if (!rent.target.empty()) {
                            firedEvents.push_back(rent.target);
                        }
                    }
                }
                
                // State Machine Execution
                if (rent.brushState == BrushState::Opening) {
                    glm::vec3 dir = glm::normalize(rent.pos2 - rent.pos1);
                    rent.origin += dir * rent.speed * deltaTime;
                    
                    // Did we reach the destination?
                    if (glm::distance(rent.origin, rent.pos1) >= glm::distance(rent.pos2, rent.pos1)) {
                        rent.origin = rent.pos2; // Clamp
                        rent.brushState = BrushState::Open;
                        rent.stateTimer = 0.0f; // Reset wait timer
                    }
                } 
                else if (rent.brushState == BrushState::Open) {
                    rent.stateTimer += deltaTime;
                    if (rent.stateTimer >= rent.wait) {
                        rent.brushState = BrushState::Closing;
                    }
                } 
                else if (rent.brushState == BrushState::Closing) {
                    glm::vec3 dir = glm::normalize(rent.pos1 - rent.pos2);
                    rent.origin += dir * rent.speed * deltaTime;
                    
                    // Did we reach the start?
                    if (glm::distance(rent.origin, rent.pos2) >= glm::distance(rent.pos1, rent.pos2)) {
                        rent.origin = rent.pos1; // Clamp
                        rent.brushState = BrushState::Closed;
                    }
                }
            }
        }

        // ---> NEW: Broadcast Fired Events to all listening entities!
        for (const std::string& eventName : firedEvents) {
            for (auto& rent : m_renderEntities) {
                if (rent.targetname == eventName && rent.brushState == BrushState::Closed) {
                    rent.brushState = BrushState::Opening;
                    m_console->Print("Event Triggered: " + eventName);
                }
            }
        }

        // ---> NEW: Generate Console Geometry instead of the Test Quad
        std::vector<UIVertex> uiVerts;
        m_console->GenerateGeometry(uiVerts, 1280.0f, 720.0f);

        // 4. Render
        float totalTime = SDL_GetTicks() / 1000.0f;

        // Calculate Lightstyles! (Updates at 10 FPS)
        float lightstyleValues[64];
        for (int i = 0; i < 64; ++i) {
            if (m_lightstyles[i].empty()) {
                lightstyleValues[i] = 1.0f;
            } else {
                int frame = static_cast<int>(totalTime * 10.0f) % m_lightstyles[i].length();
                // 'm' is 109 in ASCII. 'a' is 97. (109-97)/12 = 1.0f.
                lightstyleValues[i] = (m_lightstyles[i][frame] - 'a') / 12.0f; 
            }
        }

        m_renderer->DrawFrame(*m_camera, *m_map, m_renderEntities, &m_viewModel, uiVerts, totalTime, lightstyleValues);
    }
}

} // namespace engine