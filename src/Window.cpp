#include "Window.hpp"
#include "Player.hpp"
#include "Console.hpp"
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

void Window::PollEvents(bool& isRunning, Player* player, Console* console) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) isRunning = false;
        
        if (e.type == SDL_EVENT_KEY_DOWN) {
            // Hard quit on ESC for now
            if (e.key.key == SDLK_ESCAPE) isRunning = false;

            // Toggle Console on Tilde/Grave
            if (e.key.key == SDLK_GRAVE) {
                if (console) {
                    console->Toggle();
                    // Release the mouse to the OS if console is active
                    SDL_SetWindowRelativeMouseMode(m_window, !console->IsActive());
                }
            }
            
            if (console && console->IsActive()) {
                // Console control keys
                if (e.key.key == SDLK_BACKSPACE) {
                    console->Backspace();
                } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_RETURN2) {
                    console->ExecuteCommand();
                }
            }
        }
        
        // Literal Text Input for the console typing
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            if (console && console->IsActive()) {
                for (int i = 0; e.text.text[i] != '\0'; i++) {
                    // Ignore the backtick character so it doesn't print when opening the console!
                    if (e.text.text[i] != '`') {
                        console->CharInput(e.text.text[i]);
                    }
                }
            }
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