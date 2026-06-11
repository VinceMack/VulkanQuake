#pragma once
#include "Window.hpp"
#include "Camera.hpp"
#include "Renderer.hpp"
#include "Map.hpp"
#include "RenderEntity.hpp"
#include <memory>
#include <vector>

namespace engine {

class Engine {
public:
    Engine();
    ~Engine();

    void Run();

private:
    void Init();
    void MainLoop();

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Map> m_map;
    
    std::vector<RenderEntity> m_renderEntities;
    
    bool m_isRunning = false;
};

} // namespace engine