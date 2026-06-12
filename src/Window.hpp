#pragma once
#include <SDL3/SDL.h>
#include <string>

namespace engine {

class Player;

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    // Prevent copying
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Polling and Input
    void PollEvents(bool& isRunning, Player* player = nullptr);
    void CaptureMouse(bool capture);

    // Getters for Vulkan
    SDL_Window* GetHandle() const { return m_window; }
    
    // Extensions required by Vulkan
    const char* const* GetVulkanExtensions(uint32_t* count) const;

    // Viewport dimensions
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    int m_width;
    int m_height;
};

} // namespace engine