#define SDL_MAIN_HANDLED
#include "Engine.hpp"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        engine::Engine app;
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << '\n';
        return -1;
    }

    return 0;
}