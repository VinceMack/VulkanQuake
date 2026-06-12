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

    // 5. Parse Entities (to find info_player_start first)
    glm::vec3 spawnOrigin(0.0f);
    float spawnAngle = 0.0f;
    bool foundSpawn = false;

    for (const auto& ent : m_map->GetEntities()) {
        std::string cls = ent.GetClassname();
        if (cls == "info_player_start") {
            spawnOrigin = ent.GetVector("origin");
            spawnAngle = ent.GetFloat("angle");
            foundSpawn = true;
            break;
        }
    }

    if (foundSpawn) {
        m_player->Spawn(spawnOrigin, spawnAngle);
    } else {
        m_player->Spawn(glm::vec3(0.0f), 0.0f);
        m_console->Print("WARNING: No spawn point found.");
    }

    // 7. Load the VM and run spawning loop
    auto progsData = m_vfs->ReadFile("progs.dat");
    if (progsData) {
        m_vm = std::make_unique<VirtualMachine>(std::move(*progsData));
        
        // 1. Cache the specific VM offsets we need for fast Built-in access
        int32_t ofs_self = m_vm->FindGlobalOffset("self");
        int32_t ofs_origin = m_vm->FindFieldOffset("origin");
        int32_t ofs_mins = m_vm->FindFieldOffset("mins");
        int32_t ofs_maxs = m_vm->FindFieldOffset("maxs");
        int32_t ofs_flags = m_vm->FindFieldOffset("flags");
        int32_t ofs_absmin = m_vm->FindFieldOffset("absmin");
        int32_t ofs_absmax = m_vm->FindFieldOffset("absmax");

        // 2. Attach the C++ Router to the VM
        m_vm->SetBuiltinHandler([this, ofs_self, ofs_origin, ofs_mins, ofs_maxs, ofs_flags, ofs_absmin, ofs_absmax](VirtualMachine& vm, int32_t bIdx) {
            switch (bIdx) {
                case 2: { // setorigin(entity e, vector v)
                    int32_t targetEnt = vm.GetParmEdict(0);
                    glm::vec3 v = vm.GetParmVector(1);
                    if (targetEnt >= 0 && targetEnt < static_cast<int32_t>(vm.m_edicts.size())) {
                        vm.SetEdictFieldVector(targetEnt, ofs_origin, v);
                        if (ofs_absmin != -1 && ofs_absmax != -1) {
                            glm::vec3 mins = vm.GetEdictFieldVector(targetEnt, ofs_mins);
                            glm::vec3 maxs = vm.GetEdictFieldVector(targetEnt, ofs_maxs);
                            vm.SetEdictFieldVector(targetEnt, ofs_absmin, v + mins);
                            vm.SetEdictFieldVector(targetEnt, ofs_absmax, v + maxs);
                        }
                    }
                    break;
                }
                case 3: { // setmodel(entity e, string m)
                    int32_t targetEnt = vm.GetParmEdict(0);
                    std::string modelName = vm.GetParmString(1);
                    
                    if (targetEnt >= 0 && targetEnt < static_cast<int32_t>(vm.m_edicts.size())) {
                        int32_t ofs_modelindex = vm.FindFieldOffset("modelindex");
                        if (ofs_modelindex != -1) {
                            vm.SetEdictFieldFloat(targetEnt, ofs_modelindex, 1.0f);
                        }
                        
                        int32_t ofs_model = vm.FindFieldOffset("model");
                        if (ofs_model >= 0 && ofs_model < static_cast<int32_t>(vm.m_edicts[targetEnt].v.size())) {
                            int32_t strOffset = vm.m_globalData[7].string; // Parm 1 string offset
                            vm.m_edicts[targetEnt].v[ofs_model].string = strOffset;
                        }

                        // If it is a BSP brush model, set sizes automatically!
                        if (!modelName.empty() && modelName[0] == '*') {
                            try {
                                int32_t modelId = std::stoi(modelName.substr(1));
                                const auto& bspModel = m_map->GetBspModel(modelId);
                                
                                glm::vec3 mins(bspModel.mins[0], bspModel.mins[1], bspModel.mins[2]);
                                glm::vec3 maxs(bspModel.maxs[0], bspModel.maxs[1], bspModel.maxs[2]);
                                
                                vm.SetEdictFieldVector(targetEnt, ofs_mins, mins);
                                vm.SetEdictFieldVector(targetEnt, ofs_maxs, maxs);
                                
                                if (ofs_absmin != -1 && ofs_absmax != -1) {
                                    glm::vec3 origin = vm.GetEdictFieldVector(targetEnt, ofs_origin);
                                    vm.SetEdictFieldVector(targetEnt, ofs_absmin, origin + mins);
                                    vm.SetEdictFieldVector(targetEnt, ofs_absmax, origin + maxs);
                                }
                            } catch (...) {
                                // ignore parse/indexing errors
                            }
                        }
                    }
                    break;
                }
                case 4: { // setsize(entity e, vector min, vector max)
                    int32_t targetEnt = vm.GetParmEdict(0);
                    if (targetEnt >= 0 && targetEnt < static_cast<int32_t>(vm.m_edicts.size())) {
                        glm::vec3 mins = vm.GetParmVector(1);
                        glm::vec3 maxs = vm.GetParmVector(2);
                        vm.SetEdictFieldVector(targetEnt, ofs_mins, mins);
                        vm.SetEdictFieldVector(targetEnt, ofs_maxs, maxs);
                        if (ofs_absmin != -1 && ofs_absmax != -1) {
                            glm::vec3 origin = vm.GetEdictFieldVector(targetEnt, ofs_origin);
                            vm.SetEdictFieldVector(targetEnt, ofs_absmin, origin + mins);
                            vm.SetEdictFieldVector(targetEnt, ofs_absmax, origin + maxs);
                        }
                    }
                    break;
                }
                case 19: // precache_sound
                case 20: // precache_model
                case 68: // precache_file
                case 75: // precache_model2
                case 76: // precache_sound2
                case 77: // precache_file2
                    // QuakeC expects precache builtins to return the string index they were passed!
                    // This is a weird Quake quirk.
                    vm.SetReturnStringOffset(vm.m_globalData[4].string); // Return Parm0 string offset
                    break;
                case 25: { // dprint(string)
                    m_console->Print("[QC] " + vm.GetParmString(0));
                    break;
                }
                case 35: { // lightstyle(float style, string value)
                    int32_t styleIdx = static_cast<int32_t>(vm.GetParmFloat(0));
                    std::string val = vm.GetParmString(1);
                    if (styleIdx >= 0 && styleIdx < 64) {
                        m_lightstyles[styleIdx] = val;
                    }
                    break;
                }
                case 34: { // droptofloor()
                    // Traces down 256 units from `self.origin`.
                    int32_t selfIdx = vm.GetGlobalEdict(ofs_self);
                    if (selfIdx >= 0 && selfIdx < static_cast<int32_t>(vm.m_edicts.size())) {
                        glm::vec3 origin = vm.GetEdictFieldVector(selfIdx, ofs_origin);
                        
                        // Trace down! (We pass an empty entity list because during spawn, m_renderEntities isn't populated yet)
                        std::vector<RenderEntity> emptyList; 
                        TraceResult trace = m_physics->TraceHull(origin, origin - glm::vec3(0.0f, 0.0f, 256.0f), 1, emptyList);
                        
                        if (trace.allSolid || trace.fraction == 1.0f) {
                            vm.SetReturnFloat(0.0f); // Failed to find floor
                        } else {
                            // Success! Update origin, set ONGROUND flag, return 1
                            vm.SetEdictFieldVector(selfIdx, ofs_origin, trace.endPos);
                            
                            // Bitwise OR the FL_ONGROUND flag (512)
                            int32_t flags = static_cast<int32_t>(vm.GetEdictFieldFloat(selfIdx, ofs_flags));
                            vm.SetEdictFieldFloat(selfIdx, ofs_flags, static_cast<float>(flags | 512));
                            
                            vm.SetReturnFloat(1.0f);
                        }
                    } else {
                        vm.SetReturnFloat(0.0f);
                    }
                    break;
                }
                case 11: { // objerror(string e)
                    std::cout << "[QC ObjError] " << vm.GetParmString(0) << "\n";
                    std::cout.flush();
                    throw std::runtime_error("VM Error: objerror: " + vm.GetParmString(0));
                    break;
                }
                case 14: { // spawn() -> returns entity
                    int32_t newEnt = vm.AllocateEdict();
                    vm.SetReturnEdict(newEnt);
                    break;
                }
                case 15: { // remove(entity e)
                    int32_t targetEnt = vm.GetParmEdict(0);
                    if (targetEnt > 0 && targetEnt < static_cast<int32_t>(vm.m_edicts.size())) {
                        vm.m_edicts[targetEnt].isFree = true;
                    }
                    break;
                }
                case 43: { // fabs(float f)
                    vm.SetReturnFloat(std::abs(vm.GetParmFloat(0)));
                    break;
                }
                case 69: { // makestatic(entity e)
                    // QuakeC pushes an entity to the static list, we ignore for now
                    break;
                }
                case 72: { // cvar_set(string var, string val)
                    // Stub for console variable set
                    break;
                }
                case 74: { // ambientsound(vector pos, string sample, float vol, float attn)
                    // Stub for ambient sound
                    break;
                }
                case 7: { // random() -> returns float [0, 1)
                    vm.SetReturnFloat(static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
                    break;
                }
                case 8: { // sound(entity e, float chan, string samp, float vol, float attn)
                    // Stub for sound playing
                    break;
                }
                case 17: { // checkclient()
                    // Returns the player entity (index 1)
                    vm.SetReturnEdict(1);
                    break;
                }
                case 18: { // find(entity start, .string field, string match)
                    int32_t startIdx = vm.GetParmEdict(0);
                    int32_t fieldOffset = vm.GetParmEdict(1);
                    std::string matchStr = vm.GetParmString(2);
                    
                    int32_t foundIdx = 0; // default to world/not found
                    
                    if (fieldOffset >= 0) {
                        const auto& edicts = vm.GetEdicts();
                        for (size_t i = startIdx + 1; i < edicts.size(); ++i) {
                            if (edicts[i].isFree) continue;
                            
                            if (fieldOffset < static_cast<int32_t>(edicts[i].v.size())) {
                                int32_t stringOffset = edicts[i].v[fieldOffset].string;
                                std::string val = vm.GetProgsString(stringOffset);
                                if (val == matchStr) {
                                    foundIdx = static_cast<int32_t>(i);
                                    break;
                                }
                            }
                        }
                    }
                    vm.SetReturnEdict(foundIdx);
                    break;
                }
                case 32: { // walkmove(float yaw, float dist)
                    vm.SetReturnFloat(1.0f);
                    break;
                }
                case 67: { // movetogoal(float dist)
                    vm.SetReturnFloat(1.0f);
                    break;
                }
                default: {
                    std::cout << "[QC Builtin Warning] Unhandled Built-in #" << bIdx << "\n";
                    std::cout.flush();
                    break;
                }
            }
        });

        m_console->Print("Executing worldspawn...");
        m_vm->Execute("worldspawn");

        // Cache offsets for the Think Loop!
        m_ofs_time = m_vm->FindGlobalOffset("time");
        m_ofs_self = m_vm->FindGlobalOffset("self");
        m_ofs_nextthink = m_vm->FindFieldOffset("nextthink");
        m_ofs_think = m_vm->FindFieldOffset("think");
        m_ofs_origin = m_vm->FindFieldOffset("origin");
        m_ofs_angles = m_vm->FindFieldOffset("angles");
        m_ofs_frame = m_vm->FindFieldOffset("frame");

        // Cache necessary VM offsets for speed
        int32_t globalSelfOffset = m_ofs_self;
        int32_t fieldOriginOffset = m_ofs_origin;
        int32_t fieldAnglesOffset = m_vm->FindFieldOffset("angles");

        m_console->Print("VM Spawning Entities...");

        // Loop over the C++ parsed map entities
        for (const auto& ent : m_map->GetEntities()) {
            std::string cls = ent.GetClassname();
            
            // Skip worldspawn, as we already executed it!
            if (cls == "worldspawn") continue;

            // Allocate memory inside the VM for this entity
            int32_t edictIdx = m_vm->AllocateEdict();

            // Set explicit fields
            m_vm->SetEdictFieldVector(edictIdx, fieldOriginOffset, ent.GetVector("origin"));
            if (fieldAnglesOffset != -1) {
                m_vm->SetEdictFieldFloat(edictIdx, fieldAnglesOffset + 1, ent.GetFloat("angle"));
            }
            
            // Pass spawnflags into QuakeC so it knows if a door is START_OPEN!
            int32_t ofs_spawnflags = m_vm->FindFieldOffset("spawnflags");
            m_vm->SetEdictFieldFloat(edictIdx, ofs_spawnflags, ent.GetFloat("spawnflags"));

            // Write the properties from the C++ Entity into the QuakeC Edict memory!
            for (const auto& [key, value] : ent.GetProperties()) {
                m_vm->SetEdictFieldFromString(edictIdx, key, value);
            }
            
            // Tell the VM: "The entity you are currently acting on is THIS one."
            m_vm->SetGlobalEdict(globalSelfOffset, edictIdx);

            // Tell the VM: "Run the initialization script for this classname!"
            m_vm->Execute(cls);
        }

        // ========================================================================
        // ---> NEW: Sync VM Edicts to C++ RenderEntities
        // ========================================================================
        m_console->Print("Syncing VM Memory to Renderer...");
        
        int32_t ofs_model = m_vm->FindFieldOffset("model");
        int32_t ofs_frame = m_vm->FindFieldOffset("frame");

        if (ofs_model != -1 && ofs_frame != -1) {
            const auto& edicts = m_vm->GetEdicts();
            for (size_t edictIdx = 0; edictIdx < edicts.size(); ++edictIdx) {
                const auto& edict = edicts[edictIdx];
                if (edict.isFree) continue;

                // Extract the model string offset
                int32_t modelStrOffset = edict.v[ofs_model].string;
                if (modelStrOffset <= 0) continue; // Invisible entity (like triggers!)

                std::string modelName = m_vm->GetProgsString(modelStrOffset);
                if (modelName.empty()) continue;

                RenderEntity rent;
                rent.edictIndex = static_cast<int32_t>(edictIdx);
                rent.origin = m_vm->GetEdictFieldVector(static_cast<int32_t>(edictIdx), fieldOriginOffset);
                rent.angles = m_vm->GetEdictFieldVector(static_cast<int32_t>(edictIdx), m_ofs_angles);
                rent.frame = static_cast<uint32_t>(m_vm->GetEdictFieldFloat(static_cast<int32_t>(edictIdx), ofs_frame));
                rent.nextFrame = rent.frame + 1;
                rent.interp = static_cast<float>(rand() % 100) / 100.0f;
                rent.isSolid = true;
                rent.isVisible = true;

                if (modelName[0] == '*') {
                    // It's a BSP Brush Entity! (Door, platform)
                    rent.type = EntityModelType::BspBrush;
                    rent.modelId = std::stoi(modelName.substr(1));

                    // Grab the bounds for collision
                    const auto& bspModel = m_map->GetBspModel(rent.modelId);
                    rent.localMins = glm::vec3(bspModel.mins[0], bspModel.mins[1], bspModel.mins[2]);
                    rent.localMaxs = glm::vec3(bspModel.maxs[0], bspModel.maxs[1], bspModel.maxs[2]);
                    
                    // Set up C++ Kinematics (We will move this to QuakeC later)
                    rent.brushState = BrushState::Closed;
                    rent.pos1 = rent.origin;
                    
                    // Note: Because we haven't ported 'targetname' strings out of QuakeC yet, 
                    // all doors will open on proximity for this specific test phase.
                    rent.requireTrigger = false; 

                } else {
                    // It's an Alias Model! (Monster, Item)
                    uint32_t mdlId = LoadAliasModel(modelName, *paletteData);
                    if (mdlId == 0) continue; // Skip if failed to load
                    
                    rent.type = EntityModelType::Alias;
                    rent.modelId = mdlId;
                }

                m_renderEntities.push_back(rent);
            }
        }
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
        // ---> NEW: The QuakeC Think Loop
        // ========================================================================
        if (m_vm) {
            // 1. Advance QuakeC Time
            float currentQCTime = m_vm->GetGlobalFloat(m_ofs_time) + deltaTime;
            m_vm->SetGlobalFloat(m_ofs_time, currentQCTime);

            // 2. Execute Entity Thinks
            const auto& edicts = m_vm->GetEdicts();
            for (size_t i = 1; i < edicts.size(); ++i) {
                if (edicts[i].isFree) continue;

                float nextthink = m_vm->GetEdictFieldFloat(static_cast<int32_t>(i), m_ofs_nextthink);
                
                // If the entity is scheduled to think, and time has passed...
                if (nextthink > 0.0f && currentQCTime >= nextthink) {
                    
                    // Clear nextthink (the QC script will reset it if it wants to loop)
                    m_vm->SetEdictFieldFloat(static_cast<int32_t>(i), m_ofs_nextthink, 0.0f);
                    
                    // Tell the VM which entity is currently acting
                    m_vm->SetGlobalEdict(m_ofs_self, static_cast<int32_t>(i));

                    // Execute the function pointer!
                    int32_t thinkFunc = m_vm->GetEdictFieldFunction(static_cast<int32_t>(i), m_ofs_think);
                    if (thinkFunc > 0) {
                        m_vm->Execute(thinkFunc);
                    }
                }
            }

            // 3. Sync VM RAM back to our C++ RenderEntities!
            for (auto& rent : m_renderEntities) {
                if (rent.edictIndex > 0) {
                    rent.origin = m_vm->GetEdictFieldVector(rent.edictIndex, m_ofs_origin);
                    rent.angles = m_vm->GetEdictFieldVector(rent.edictIndex, m_ofs_angles);
                    
                    uint32_t vmFrame = static_cast<uint32_t>(m_vm->GetEdictFieldFloat(rent.edictIndex, m_ofs_frame));
                    
                    // If QuakeC changed the frame, update our interpolation logic
                    if (rent.frame != vmFrame) {
                        rent.frame = vmFrame;
                        rent.nextFrame = vmFrame; 
                        rent.interp = 0.0f;
                    }
                }
            }
        }

        // ========================================================================
        // Entity Simulation (The Game Tick - Brush Kinematics)
        // ========================================================================
        glm::vec3 pTouchMins = m_player->GetPosition() + glm::vec3(-48.0f, -48.0f, -10.0f);
        glm::vec3 pTouchMaxs = m_player->GetPosition() + glm::vec3(48.0f, 48.0f, 66.0f);

        // We use a list to queue up events so we don't modify the state of other entities 
        // while we are currently iterating through the array!
        std::vector<std::string> firedEvents;

        for (auto& rent : m_renderEntities) {
            if (rent.type == EntityModelType::BspBrush) {
                // 1. Proximity Trigger check
                if (rent.brushState == BrushState::Closed && !rent.requireTrigger) {
                    glm::vec3 dMins = rent.GetAbsMins();
                    glm::vec3 dMaxs = rent.GetAbsMaxs();
                    
                    if (pTouchMins.x <= dMaxs.x && pTouchMaxs.x >= dMins.x &&
                        pTouchMins.y <= dMaxs.y && pTouchMaxs.y >= dMins.y &&
                        pTouchMins.z <= dMaxs.z && pTouchMaxs.z >= dMins.z) {
                        
                        rent.brushState = BrushState::Opening; 
                        
                        // If this was a button, fire its target!
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