#include "Console.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace engine {

Console::Console() {
    Print("Welcome to VulkanQuake!");
    Print("Type 'noclip' to toggle flight.");
}

void Console::Print(const std::string& text) {
    m_history.push_back(text);
    if (m_history.size() > MAX_HISTORY) {
        m_history.erase(m_history.begin());
    }
}

void Console::CharInput(char c) {
    // Only accept standard printable ASCII
    if (c >= 32 && c <= 126) {
        m_inputBuffer += c;
    }
}

void Console::Backspace() {
    if (!m_inputBuffer.empty()) {
        m_inputBuffer.pop_back();
    }
}

void Console::RegisterCommand(const std::string& name, std::function<void(const std::vector<std::string>&)> callback) {
    m_commands[name] = callback;
}

void Console::Clear() {
    m_history.clear();
}

void Console::ExecuteCommand() {
    if (m_inputBuffer.empty()) return;

    // Echo the command to the screen
    Print("] " + m_inputBuffer);

    // Split the string by spaces
    std::istringstream iss(m_inputBuffer);
    std::string cmdName;
    iss >> cmdName; // The first word is the command

    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) {
        args.push_back(arg); // The rest are arguments
    }

    // Look up the command in our registry
    auto it = m_commands.find(cmdName);
    if (it != m_commands.end()) {
        // Execute the callback function!
        it->second(args);
    } else {
        Print("Unknown command: " + cmdName);
    }

    // Clear the input line
    m_inputBuffer.clear();
}

void Console::AddCharQuad(std::vector<UIVertex>& verts, char c, float x, float y, float scale) {
    unsigned char uc = static_cast<unsigned char>(c);
    
    // Quake font is 16 columns by 16 rows.
    int col = uc % 16;
    int row = uc / 16;

    float u0 = (col * 8.0f) / 128.0f;
    float v0 = (row * 8.0f) / 128.0f;
    float u1 = u0 + (8.0f / 128.0f);
    float v1 = v0 + (8.0f / 128.0f);

    float size = 8.0f * scale;

    // Triangle 1 (Top-Left, Bottom-Left, Bottom-Right)
    verts.push_back({{x, y},               {u0, v0}}); 
    verts.push_back({{x, y + size},        {u0, v1}}); 
    verts.push_back({{x + size, y + size}, {u1, v1}}); 

    // Triangle 2 (Top-Left, Bottom-Right, Top-Right)
    verts.push_back({{x, y},               {u0, v0}}); 
    verts.push_back({{x + size, y + size}, {u1, v1}}); 
    verts.push_back({{x + size, y},        {u1, v0}}); 
}

void Console::GenerateGeometry(std::vector<UIVertex>& outVerts, float screenWidth, float screenHeight) {
    if (!m_isActive) return;

    float scale = 2.0f; // Scale up the tiny 8x8 font so it's readable on modern monitors
    float charSize = 8.0f * scale;
    float startX = 10.0f;
    float startY = 10.0f;

    // 1. Draw History
    for (const auto& line : m_history) {
        float x = startX;
        for (char c : line) {
            AddCharQuad(outVerts, c, x, startY, scale);
            x += charSize;
        }
        startY += charSize + 2.0f; // 2 pixels padding between lines
    }

    // 2. Draw Input Prompt
    startY += charSize; 
    float x = startX;
    
    AddCharQuad(outVerts, ']', x, startY, scale);
    x += charSize * 1.5f;

    for (char c : m_inputBuffer) {
        AddCharQuad(outVerts, c, x, startY, scale);
        x += charSize;
    }
    
    // Blinking cursor
    AddCharQuad(outVerts, '_', x, startY, scale);
}

} // namespace engine
