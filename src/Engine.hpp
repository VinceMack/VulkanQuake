#pragma once
#include "Window.hpp"
#include "Camera.hpp"
#include "Renderer.hpp"
#include "Map.hpp"
#include "Physics.hpp"
#include "Player.hpp"
#include "RenderEntity.hpp"
#include "VirtualFileSystem.hpp"
#include "Console.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <span>
#include <cstddef>

namespace engine {

class Engine {
public:
    Engine();
    ~Engine();

    void Run();

    // ---> NEW: Runtime map loader
    bool LoadMap(const std::string& mapName);

private:
    void Init();
    void MainLoop();

    // Change LoadAliasModel signature to remove the vfs parameter, since VFS is now a class member
    uint32_t LoadAliasModel(const std::string& path, std::span<const std::byte> palette);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Map> m_map;
    std::unique_ptr<Physics> m_physics;
    std::unique_ptr<Player> m_player;
    
    std::vector<RenderEntity> m_renderEntities;
    std::unordered_map<std::string, uint32_t> m_modelCache;
    RenderEntity m_viewModel;
    std::unique_ptr<Console> m_console;

    // ---> NEW: Promote VFS to member variable
    std::unique_ptr<engine::vfs::VirtualFileSystem> m_vfs;
    
    bool m_isRunning = false;
};

} // namespace engine