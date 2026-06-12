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

private:
    void Init();
    void MainLoop();

    uint32_t LoadAliasModel(const std::string& path, engine::vfs::VirtualFileSystem& vfs, std::span<const std::byte> palette);

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
    
    bool m_isRunning = false;
};

} // namespace engine