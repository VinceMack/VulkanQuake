#pragma once
#include "Window.hpp"
#include "Camera.hpp"
#include "Renderer.hpp"
#include "Map.hpp"
#include <memory>

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
    
    bool m_isRunning = false;
};

} // namespace engine