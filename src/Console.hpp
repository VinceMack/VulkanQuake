#pragma once
#include "UI.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace engine {

class Console {
public:
    Console();

    // Add a line of text to the history
    void Print(const std::string& text);

    // Input handling
    void Toggle() { m_isActive = !m_isActive; }
    bool IsActive() const { return m_isActive; }
    
    void CharInput(char c);
    void Backspace();
    void ExecuteCommand(); // When the user presses Enter

    // Register a command callback
    void RegisterCommand(const std::string& name, std::function<void(const std::vector<std::string>&)> callback);

    // Clear history
    void Clear();

    // Generates the 2D quads for the renderer
    void GenerateGeometry(std::vector<UIVertex>& outVerts, float screenWidth, float screenHeight);

private:
    void AddCharQuad(std::vector<UIVertex>& verts, char c, float x, float y, float scale);

    bool m_isActive = false;
    std::vector<std::string> m_history;
    std::string m_inputBuffer;

    const int MAX_HISTORY = 20; // How many lines to keep on screen

    std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>> m_commands;
};

} // namespace engine
