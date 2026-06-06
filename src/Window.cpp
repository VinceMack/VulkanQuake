#include "Window.hpp"
#include <SDL3/SDL_vulkan.h>
#include <stdexcept>

namespace engine {

Window::Window(const std::string& title, int width, int height) 
    : m_width(width), m_height(height) {
    
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error("Failed to initialize SDL: " + std::string(SDL_GetError()));
    }

    m_window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        throw std::runtime_error("Failed to create SDL Window: " + std::string(SDL_GetError()));
    }
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

void Window::PollEvents(bool& isRunning) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            isRunning = false;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            isRunning = false;
        }
    }
}

void Window::CaptureMouse(bool capture) {
    SDL_SetWindowRelativeMouseMode(m_window, capture);
}

const char* const* Window::GetVulkanExtensions(uint32_t* count) const {
    return SDL_Vulkan_GetInstanceExtensions(count);
}

} // namespace engine