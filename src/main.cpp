#define SDL_MAIN_HANDLED
#include "Engine.hpp"
#include <iostream>
#include <fstream>
#include <streambuf>
#include <exception>
#include <filesystem>
#include <vector>

// A custom streambuf that duplicates output to two streams
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* sb1, std::streambuf* sb2)
        : m_sb1(sb1), m_sb2(sb2) {}

protected:
    virtual int overflow(int c) override {
        if (c == EOF) {
            return !EOF;
        }
        int const r1 = m_sb1->sputc(c);
        int const r2 = m_sb2->sputc(c);
        return (r1 == EOF || r2 == EOF) ? EOF : c;
    }

    virtual int sync() override {
        int const r1 = m_sb1->pubsync();
        int const r2 = m_sb2->pubsync();
        return (r1 == 0 && r2 == 0) ? 0 : -1;
    }

private:
    std::streambuf* m_sb1;
    std::streambuf* m_sb2;
};

// Helper function to find the project root containing the "data" directory
static std::filesystem::path find_project_root(const std::filesystem::path& start, const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> starts = { start, exeDir };
    for (const auto& s : starts) {
        if (s.empty()) continue;
        std::filesystem::path p = s;
        while (true) {
            std::filesystem::path cand = p / "data";
            if (std::filesystem::exists(cand) && std::filesystem::is_directory(cand) &&
                std::filesystem::exists(cand / "pak0.pak")) {
                return p;
            }
            auto parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    }
    return {};
}

int main(int argc, char* argv[]) {
    std::filesystem::path exeDir;
    if (argc > 0 && argv[0]) {
        try {
            exeDir = std::filesystem::path(argv[0]).parent_path();
        } catch (...) {}
    }
    if (exeDir.empty()) {
        try {
            exeDir = std::filesystem::current_path();
        } catch (...) {}
    }

    bool enableLogging = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::string(argv[i]) == "-log") {
            enableLogging = true;
            break;
        }
    }

    std::filesystem::path rootDir = find_project_root(std::filesystem::current_path(), exeDir);
    std::filesystem::path logPath = "out.log";
    if (!rootDir.empty()) {
        logPath = rootDir / "out.log";
    } else if (!exeDir.empty()) {
        logPath = exeDir / "out.log";
    }

    std::ofstream logFile;
    if (enableLogging) {
        logFile.open(logPath, std::ios::out | std::ios::trunc);
    }
    
    std::streambuf* originalCout = std::cout.rdbuf();
    std::streambuf* originalCerr = std::cerr.rdbuf();

    TeeBuf teeCout(originalCout, logFile.rdbuf());
    TeeBuf teeCerr(originalCerr, logFile.rdbuf());

    if (logFile.is_open()) {
        std::cout.rdbuf(&teeCout);
        std::cerr.rdbuf(&teeCerr);
    }

    int result = 0;
    try {
        engine::Engine app;
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << '\n';
        result = -1;
    }

    // Restore original streambufs
    std::cout.rdbuf(originalCout);
    std::cerr.rdbuf(originalCerr);

    return result;
}