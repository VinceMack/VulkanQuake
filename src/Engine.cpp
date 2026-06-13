#include "Engine.hpp"
#include <glm/gtc/constants.hpp>
#include "AliasModel.hpp"
#include "VirtualFileSystem.hpp"
#include "UI.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cmath>
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

    // Save player parameters before we destroy the VM!
    std::unordered_map<std::string, qc::eval_t> savedParms;
    if (m_vm) {
        int32_t setChangeParmsIdx = m_vm->FindFunction("SetChangeParms");
        if (setChangeParmsIdx != -1) {
            int32_t ofs_self = m_vm->FindGlobalOffset("self");
            if (ofs_self != -1) {
                m_vm->SetGlobalEdict(ofs_self, 1); // self must be player (Edict 1)
            }
            m_vm->Execute(setChangeParmsIdx);
        }
        for (int i = 1; i <= 16; ++i) {
            std::string parmName = "parm" + std::to_string(i);
            int32_t offset = m_vm->FindGlobalOffset(parmName);
            if (offset != -1) {
                savedParms[parmName] = m_vm->m_globalData[offset];
            }
        }
    }

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

        // Restore saved parms if we have them (for level transitions)
        for (const auto& [parmName, val] : savedParms) {
            int32_t offset = m_vm->FindGlobalOffset(parmName);
            if (offset != -1) {
                m_vm->m_globalData[offset] = val;
            }
        }
        
        // 1. Cache the specific VM offsets we need for fast Built-in access
        int32_t ofs_self = m_vm->FindGlobalOffset("self");
        int32_t ofs_origin = m_vm->FindFieldOffset("origin");
        int32_t ofs_mins = m_vm->FindFieldOffset("mins");
        int32_t ofs_maxs = m_vm->FindFieldOffset("maxs");
        int32_t ofs_flags = m_vm->FindFieldOffset("flags");
        int32_t ofs_absmin = m_vm->FindFieldOffset("absmin");
        int32_t ofs_absmax = m_vm->FindFieldOffset("absmax");

        // Cache Trace offsets for Built-in #16
        int32_t ofs_trace_allsolid = m_vm->FindGlobalOffset("trace_allsolid");
        int32_t ofs_trace_startsolid = m_vm->FindGlobalOffset("trace_startsolid");
        int32_t ofs_trace_fraction = m_vm->FindGlobalOffset("trace_fraction");
        int32_t ofs_trace_endpos = m_vm->FindGlobalOffset("trace_endpos");
        int32_t ofs_trace_plane_normal = m_vm->FindGlobalOffset("trace_plane_normal");
        int32_t ofs_trace_plane_dist = m_vm->FindGlobalOffset("trace_plane_dist");
        int32_t ofs_trace_ent = m_vm->FindGlobalOffset("trace_ent");

        // Cache makevectors outputs for Built-in #1
        int32_t ofs_v_forward = m_vm->FindGlobalOffset("v_forward");
        int32_t ofs_v_up = m_vm->FindGlobalOffset("v_up");
        int32_t ofs_v_right = m_vm->FindGlobalOffset("v_right");

        // 2. Attach the C++ Router to the VM
        m_vm->SetBuiltinHandler([this, ofs_self, ofs_origin, ofs_mins, ofs_maxs, ofs_flags, ofs_absmin, ofs_absmax,
                                 ofs_trace_allsolid, ofs_trace_startsolid, ofs_trace_fraction, 
                                 ofs_trace_endpos, ofs_trace_plane_normal, ofs_trace_plane_dist, ofs_trace_ent,
                                 ofs_v_forward, ofs_v_up, ofs_v_right]
                                (VirtualMachine& vm, int32_t bIdx) {
            switch (bIdx) {
                case 1: { // makevectors(vector angles)
                    // Takes a pitch/yaw/roll angle vector and computes v_forward, v_right, v_up
                    glm::vec3 angles = vm.GetParmVector(0);
                    
                    // Quake uses: pitch = angles[0], yaw = angles[1], roll = angles[2]
                    // Quake's coordinate system: forward is along +X when yaw=0
                    float pitch = glm::radians(angles.x);
                    float yaw   = glm::radians(angles.y);
                    float roll  = glm::radians(angles.z);
                    
                    float cp = std::cos(pitch), sp = std::sin(pitch);
                    float cy = std::cos(yaw),   sy = std::sin(yaw);
                    float cr = std::cos(roll),   sr = std::sin(roll);
                    
                    // Quake's AngleVectors:
                    glm::vec3 forward(
                        cp * cy,
                        cp * sy,
                        -sp
                    );
                    glm::vec3 right(
                        (-sr * sp * cy + cr * sy),
                        (-sr * sp * sy - cr * cy),
                        (-sr * cp)
                    );
                    // Negate right to match Quake convention
                    right = -right;
                    glm::vec3 up(
                        (cr * sp * cy + sr * sy),
                        (cr * sp * sy - sr * cy),
                        (cr * cp)
                    );
                    
                    if (ofs_v_forward != -1) vm.SetGlobalVector(ofs_v_forward, forward);
                    if (ofs_v_right != -1)   vm.SetGlobalVector(ofs_v_right, right);
                    if (ofs_v_up != -1)      vm.SetGlobalVector(ofs_v_up, up);
                    break;
                }
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
                            // Success! Update origin (lift by 1 unit to prevent floor snagging), set ONGROUND flag, return 1
                            glm::vec3 safeOrigin = trace.endPos;
                            safeOrigin.z += 1.0f;
                            vm.SetEdictFieldVector(selfIdx, ofs_origin, safeOrigin);
                            
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
                case 33: { // objerror(string e)
                    std::cout << "[QC ObjError] " << vm.GetParmString(0) << "\n";
                    std::cout.flush();
                    throw std::runtime_error("VM Error: objerror: " + vm.GetParmString(0));
                    break;
                }
                
                // ---> NEW: Client connection stubs
                case 21: // stuffcmd (entity client, string s)
                    // QuakeC uses this to force the client to execute a console command.
                    // We can just log it for now!
                    std::cout << "[QC StuffCmd] " << vm.GetParmString(1) << "\n";
                    break;
                case 73: // centerprint (entity client, string s)
                    // QuakeC sending a big message to the center of the screen
                    m_console->Print("[CENTERPRINT] " + vm.GetParmString(1));
                    break;
                case 11: // bprint (string s)
                    // Broadcast print to all clients
                    m_console->Print("[BPRINT] " + vm.GetParmString(0));
                    break;
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
                    float yaw = vm.GetParmFloat(0);
                    float dist = vm.GetParmFloat(1);
                    int32_t selfIdx = vm.GetGlobalEdict(ofs_self);

                    if (dist == 0.0f) {
                        vm.SetReturnFloat(1.0f); // True! Not stuck!
                        break;
                    }

                    bool success = StepDirection(selfIdx, yaw, dist);
                    vm.SetReturnFloat(success ? 1.0f : 0.0f);
                    break;
                }
                case 67: { // movetogoal(float step)
                    int32_t selfIdx = vm.GetGlobalEdict(ofs_self);
                    float stepDist = vm.GetParmFloat(0);
                    
                    int32_t ofs_enemy = vm.FindFieldOffset("enemy");
                    int32_t enemyIdx = 0;
                    if (ofs_enemy != -1) {
                        enemyIdx = vm.m_edicts[selfIdx].v[ofs_enemy].edict;
                    }

                    int32_t ofs_origin = vm.FindFieldOffset("origin");
                    if (ofs_origin != -1 && ofs_origin + 2 < static_cast<int32_t>(vm.m_edicts[selfIdx].v.size())) {

                        glm::vec3 origin = vm.GetEdictFieldVector(selfIdx, ofs_origin);
                        float yaw = 0.0f;
                        
                        if (enemyIdx > 0 && enemyIdx < static_cast<int32_t>(vm.m_edicts.size())) {
                            glm::vec3 enemyOrigin = vm.GetEdictFieldVector(enemyIdx, ofs_origin);
                            glm::vec3 dir = enemyOrigin - origin;
                            dir.z = 0.0f; // Keep movement horizontal
                            
                            if (glm::length(dir) > 0.1f) {
                                glm::vec3 forwardDir = glm::normalize(dir);
                                yaw = static_cast<float>(std::atan2(forwardDir.y, forwardDir.x) * 180.0f / glm::pi<float>());
                                if (yaw < 0.0f) yaw += 360.0f;
                                
                                int32_t ofs_angles = vm.FindFieldOffset("angles");
                                if (ofs_angles != -1) {
                                    vm.SetEdictFieldVector(selfIdx, ofs_angles, glm::vec3(0.0f, yaw, 0.0f));
                                }
                                
                                int32_t ofs_ideal_yaw = vm.FindFieldOffset("ideal_yaw");
                                if (ofs_ideal_yaw != -1) {
                                    vm.SetEdictFieldFloat(selfIdx, ofs_ideal_yaw, yaw);
                                }
                            }
                        } else {
                            int32_t ofs_angles = vm.FindFieldOffset("angles");
                            if (ofs_angles != -1) {
                                yaw = vm.GetEdictFieldVector(selfIdx, ofs_angles).y;
                            }
                        }

                        std::cout << "[Engine] movetogoal(self:" << selfIdx << ") enemy=" << enemyIdx << " dist=" << stepDist << " yaw=" << yaw << "\n";
                        StepDirection(selfIdx, yaw, stepDist);
                    }
                    break;
                }
                case 39: { // changeyaw()
                    int32_t selfIdx = vm.GetGlobalEdict(ofs_self);
                    int32_t ofs_angles = vm.FindFieldOffset("angles");
                    int32_t ofs_ideal_yaw = vm.FindFieldOffset("ideal_yaw");
                    
                    if (ofs_angles != -1 && ofs_ideal_yaw != -1) {
                        glm::vec3 angles = vm.GetEdictFieldVector(selfIdx, ofs_angles);
                        float ideal = vm.GetEdictFieldFloat(selfIdx, ofs_ideal_yaw);
                        // Instantly snap to ideal yaw
                        angles.y = ideal; 
                        vm.SetEdictFieldVector(selfIdx, ofs_angles, angles);
                    }
                    break;
                }
                case 49: { // checkbottom(entity)
                    // HACK: Always return 1.0f (True) so monsters don't think they are falling off a cliff!
                    vm.SetReturnFloat(1.0f);
                    break;
                }
                case 9: { // normalize(vector v)
                    glm::vec3 v = vm.GetParmVector(0);
                    float len = glm::length(v);
                    if (len > 0.0f) vm.SetReturnVector(glm::normalize(v));
                    else vm.SetReturnVector(glm::vec3(0.0f));
                    break;
                }
                case 12: { // vlen(vector v)
                    vm.SetReturnFloat(glm::length(vm.GetParmVector(0)));
                    break;
                }
                case 13: { // vectoyaw(vector v) -> float
                    glm::vec3 v13 = vm.GetParmVector(0);
                    float yaw13 = 0.0f;
                    if (std::abs(v13.x) > 0.001f || std::abs(v13.y) > 0.001f) {
                        yaw13 = std::atan2(v13.y, v13.x) * 180.0f / glm::pi<float>();
                        if (yaw13 < 0.0f) yaw13 += 360.0f;
                    }
                    vm.SetReturnFloat(yaw13);
                    break;
                }
                case 27: { // spawn()
                    int32_t newEdict = vm.AllocateEdict();
                    vm.SetReturnEdict(newEdict);
                    break;
                }
                case 16: { // traceline(vector v1, vector v2, float nomonsters, entity forent)
                    glm::vec3 v1 = vm.GetParmVector(0);
                    glm::vec3 v2 = vm.GetParmVector(1);
                    float nomonsters = vm.GetParmFloat(2);
                    int32_t forent = vm.GetParmEdict(3); // The entity to ignore (usually 'self')

                    // Hull 0 is the 1D Raycast hull!
                    TraceResult trace = m_physics->TraceHull(v1, v2, 0, m_renderEntities, forent);

                    // Write the results to QuakeC Global memory!
                    vm.SetGlobalFloat(ofs_trace_allsolid, trace.allSolid ? 1.0f : 0.0f);
                    vm.SetGlobalFloat(ofs_trace_startsolid, trace.startSolid ? 1.0f : 0.0f);
                    vm.SetGlobalFloat(ofs_trace_fraction, trace.fraction);
                    vm.SetGlobalVector(ofs_trace_endpos, trace.endPos);
                    vm.SetGlobalVector(ofs_trace_plane_normal, trace.planeNormal);
                    vm.SetGlobalFloat(ofs_trace_plane_dist, trace.planeDist);
                    
                    // Note: We don't have Alias Model (monster) hitboxes implemented in TraceHull yet.
                    // For now, return 0 (World) for the hit entity. We will upgrade this when we implement shooting!
                    vm.SetGlobalEdict(ofs_trace_ent, 0); 
                    break;
                }
                case 44: { // cvar(string name) -> float
                    std::string cvarName = vm.GetParmString(0);
                    // Return sensible defaults for common cvars
                    if (cvarName == "skill") vm.SetReturnFloat(1.0f);
                    else if (cvarName == "teamplay") vm.SetReturnFloat(0.0f);
                    else if (cvarName == "deathmatch") vm.SetReturnFloat(0.0f);
                    else if (cvarName == "coop") vm.SetReturnFloat(0.0f);
                    else if (cvarName == "noexit") vm.SetReturnFloat(0.0f);
                    else vm.SetReturnFloat(0.0f);
                    break;
                }
                case 40: { // vectoangles(vector v) -> vector
                    glm::vec3 v40 = vm.GetParmVector(0);
                    float yaw40 = 0.0f, pitch40 = 0.0f;
                    if (std::abs(v40.x) > 0.001f || std::abs(v40.y) > 0.001f) {
                        yaw40 = std::atan2(v40.y, v40.x) * 180.0f / glm::pi<float>();
                        if (yaw40 < 0.0f) yaw40 += 360.0f;
                        float forward40 = std::sqrt(v40.x * v40.x + v40.y * v40.y);
                        pitch40 = -std::atan2(v40.z, forward40) * 180.0f / glm::pi<float>();
                    } else if (v40.z > 0.0f) {
                        pitch40 = -90.0f;
                    } else {
                        pitch40 = 90.0f;
                    }
                    vm.SetReturnVector(glm::vec3(pitch40, yaw40, 0.0f));
                    break;
                }
                case 41: { // rint(float v) -> float
                    vm.SetReturnFloat(std::round(vm.GetParmFloat(0)));
                    break;
                }
                case 42: { // floor(float v) -> float
                    vm.SetReturnFloat(std::floor(vm.GetParmFloat(0)));
                    break;
                }
                case 46: { // ceil(float v) -> float
                    vm.SetReturnFloat(std::ceil(vm.GetParmFloat(0)));
                    break;
                }
                case 36: { // etos(entity e) -> string (debug)
                    int32_t e = vm.GetParmEdict(0);
                    std::string s = std::to_string(e);
                    vm.SetReturnStringOffset(vm.AllocateString(s));
                    break;
                }
                case 48: { // pointcontents(vector v) -> float
                    glm::vec3 v = vm.GetParmVector(0);
                    int contents = m_physics->HullPointContents(0, v, 0);
                    vm.SetReturnFloat(static_cast<float>(contents));
                    break;
                }
                case 23: { // aim(entity e, float speed) -> vector
                    // Return forward vector of the entity
                    int32_t entIdx = vm.GetParmEdict(0);
                    int32_t ofs_angles_aim = vm.FindFieldOffset("angles");
                    glm::vec3 angles(0.0f);
                    if (ofs_angles_aim != -1) {
                        angles = vm.GetEdictFieldVector(entIdx, ofs_angles_aim);
                    }
                    float ay = glm::radians(angles.y);
                    vm.SetReturnVector(glm::vec3(std::cos(ay), std::sin(ay), 0.0f));
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
        // ---> NEW: Virtualize the Player (Edict 1)
        // ========================================================================
        m_console->Print("Executing PutClientInServer...");

        // Tell the VM that Edict 1 is the active entity
        m_vm->SetGlobalEdict(m_ofs_self, 1);

        // Give the player their starting 3D coordinates from the info_player_start entity!
        m_vm->SetEdictFieldVector(1, fieldOriginOffset, spawnOrigin);
        m_vm->SetEdictFieldVector(1, fieldAnglesOffset, glm::vec3(0.0f, spawnAngle, 0.0f));

        // Execute the QuakeC login sequence!
        // Only run SetNewParms if we didn't restore any saved parameters (meaning it's the first map load/new game).
        if (savedParms.empty()) {
            int32_t setNewParmsIdx = m_vm->FindFunction("SetNewParms");
            if (setNewParmsIdx != -1) {
                m_vm->Execute(setNewParmsIdx);
            }
        }

        int32_t putClientIdx = m_vm->FindFunction("PutClientInServer");
        if (putClientIdx != -1) {
            m_vm->Execute(putClientIdx);
        } else {
            std::cerr << "WARNING: PutClientInServer not found in progs.dat!\n";
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

                // ---> NEW: Hide the local player's 3D model in first-person view!
                if (rent.edictIndex == 1) {
                    rent.isVisible = false;
                }

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

            // ---> NEW: Sync C++ Player State into VM RAM (Edict 1)
            // QuakeC uses pitch (x), yaw (y), roll (z). Note that Quake pitch is inverted in some math, but standard here.
            glm::vec3 playerAngles(m_camera->GetPitch(), m_camera->GetYaw(), 0.0f);
            
            // We use static offsets 1, 2, 3 instead of named offsets for speed if we don't have them cached,
            // but let's use the ones we cached! (Make sure you cached m_ofs_velocity and m_ofs_v_angle in LoadMap!)
            // Actually, let's just look them up if we haven't cached them:
            static int32_t ofs_velocity = m_vm->FindFieldOffset("velocity");
            static int32_t ofs_v_angle = m_vm->FindFieldOffset("v_angle");

            m_vm->SetEdictFieldVector(1, m_ofs_origin, m_player->GetPosition());
            m_vm->SetEdictFieldVector(1, m_ofs_angles, glm::vec3(0.0f, m_camera->GetYaw(), 0.0f));
            m_vm->SetEdictFieldVector(1, ofs_v_angle, playerAngles);
            m_vm->SetEdictFieldVector(1, ofs_velocity, m_player->GetVelocity());

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
                    if (rent.nextFrame != vmFrame) {
                        rent.frame = rent.nextFrame; // Transition current frame to the old nextFrame
                        rent.nextFrame = vmFrame;    // Set new target frame
                        rent.interp = 0.0f;          // Reset interpolation factor
                    }

                    // Advance interpolation factor
                    if (rent.interp < 1.0f) {
                        rent.interp += deltaTime / 0.1f; // Animation state transitions typically take 0.1s in QuakeC
                        if (rent.interp > 1.0f) {
                            rent.interp = 1.0f;
                            rent.frame = rent.nextFrame; // Fully snap to the new frame
                        }
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

bool Engine::StepDirection(int32_t edictIdx, float yaw, float dist) {
    if (m_ofs_origin == -1) return false;

    glm::vec3 origin = m_vm->GetEdictFieldVector(edictIdx, m_ofs_origin);

    float angleRad = glm::radians(yaw);
    glm::vec3 forwardDir(std::cos(angleRad), std::sin(angleRad), 0.0f);
    glm::vec3 end = origin + (forwardDir * dist);

    auto TryStep = [&](const glm::vec3& startPos, const glm::vec3& targetEnd, TraceResult& outTrace) -> bool {
        // 1. Try moving straight first
        outTrace = m_physics->TraceHull(startPos, targetEnd, 1, m_renderEntities, edictIdx);
        if (!outTrace.startSolid && outTrace.fraction == 1.0f) {
            return true;
        }
        if (outTrace.startSolid) {
            return false;
        }

        // 2. Try stepping up
        const float sv_stepsize = 18.0f;
        glm::vec3 stepUpStart = startPos + glm::vec3(0.0f, 0.0f, sv_stepsize);
        TraceResult traceUp = m_physics->TraceHull(startPos, stepUpStart, 1, m_renderEntities, edictIdx);
        if (traceUp.startSolid) {
            return false;
        }

        glm::vec3 elevatedStart = traceUp.endPos;
        glm::vec3 elevatedEnd = elevatedStart + (targetEnd - startPos);
        TraceResult traceForward = m_physics->TraceHull(elevatedStart, elevatedEnd, 1, m_renderEntities, edictIdx);
        if (traceForward.startSolid) {
            return false;
        }

        glm::vec3 stepDownStart = traceForward.endPos;
        glm::vec3 stepDownEnd = stepDownStart - glm::vec3(0.0f, 0.0f, sv_stepsize);
        TraceResult traceDown = m_physics->TraceHull(stepDownStart, stepDownEnd, 1, m_renderEntities, edictIdx);
        if (traceDown.startSolid) {
            return false;
        }

        if (traceForward.fraction > 0.0f) {
            outTrace = traceDown;
            outTrace.fraction = traceForward.fraction;
            return true;
        }
        return false;
    };

    // 1. Try moving straight
    TraceResult trace;
    if (TryStep(origin, end, trace)) {
        m_vm->SetEdictFieldVector(edictIdx, m_ofs_origin, trace.endPos);
        return true;
    }

    // 2. Straight movement blocked! Try Step-and-Slide (Wall Hugging)
    // Try moving purely along X
    glm::vec3 endX = origin + glm::vec3(forwardDir.x * dist, 0.0f, 0.0f);
    TraceResult traceX;
    
    // Try moving purely along Y
    glm::vec3 endY = origin + glm::vec3(0.0f, forwardDir.y * dist, 0.0f);
    TraceResult traceY;

    if (std::abs(forwardDir.x) > 0.0f && TryStep(origin, endX, traceX) && traceX.fraction == 1.0f) {
        m_vm->SetEdictFieldVector(edictIdx, m_ofs_origin, traceX.endPos);
        return true;
    }
    
    if (std::abs(forwardDir.y) > 0.0f && TryStep(origin, endY, traceY) && traceY.fraction == 1.0f) {
        m_vm->SetEdictFieldVector(edictIdx, m_ofs_origin, traceY.endPos);
        return true;
    }

    // 3. If we can't move the full distance cleanly anywhere, just move as much as we can straight!
    if (!trace.startSolid && trace.fraction > 0.0f) {
        m_vm->SetEdictFieldVector(edictIdx, m_ofs_origin, trace.endPos);
        return true;
    }

    std::cout << "[Engine] StepDirection Completely Blocked for Edict " << edictIdx << " dist=" << dist << "\n";
    // Completely blocked
    return false;
}

} // namespace engine