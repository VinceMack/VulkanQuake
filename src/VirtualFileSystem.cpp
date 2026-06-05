#include "VirtualFileSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <expected>
#include <fstream>

namespace engine::vfs {

VirtualFileSystem::VirtualFileSystem(std::filesystem::path baseDir)
    : m_baseDir(std::move(baseDir)) {
}

std::string VirtualFileSystem::NormalizePath(const std::string& path) const {
    std::string normalized = path;
    
    // Quake paths strictly use forward slashes
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    
    // Quake paths are case-insensitive. I normalize to lowercase.
    // Cast to unsigned char to prevent undefined behavior with std::tolower
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                   
    return normalized;
}

bool VirtualFileSystem::MountPak(const std::string& pakFilename) {
    std::filesystem::path fullPath = m_baseDir / pakFilename;
    
    // Open in binary mode and seek immediately to the end to get file size
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    const std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < static_cast<std::streamsize>(sizeof(PakHeader))) {
        return false; // File is too small to even be a PAK
    }

    PakHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(PakHeader));

    // Validate the magic identifier
    if (header.magic[0] != 'P' || header.magic[1] != 'A' || 
        header.magic[2] != 'C' || header.magic[3] != 'K') {
        return false;
    }

    // Safety check: Ensure directory table doesn't exceed file bounds
    if (header.dirOffset + header.dirLength > fileSize) {
        return false;
    }

    const int numEntries = header.dirLength / sizeof(PakEntry);
    
    // Seek to the beginning of the directory table
    file.seekg(header.dirOffset, std::ios::beg);

    // Read the entire directory table into memory temporarily
    std::vector<PakEntry> entries(numEntries);
    file.read(reinterpret_cast<char*>(entries.data()), header.dirLength);

    if (!file) {
        return false; // Failed to read the full directory
    }

    // Process each entry and add it to our registry
    for (const auto& entry : entries) {
        // strnlen ensures I don't overflow if the name fills exactly 56 chars without a null terminator
        std::string vPath(entry.name, strnlen(entry.name, sizeof(entry.name)));
        vPath = NormalizePath(vPath);

        // Note: Quake files are strictly Little-Endian. On modern x86_64/ARM64 this is fine.
        // If compiling for a Big-Endian system (e.g., PowerPC), I would need to byteswap 
        // entry.fileOffset and entry.fileLength here.
        m_fileRegistry[vPath] = FileLocation{
            .sourcePak = pakFilename,
            .offset    = static_cast<size_t>(entry.fileOffset),
            .size      = static_cast<size_t>(entry.fileLength)
        };
    }

    return true;
}

std::expected<std::vector<std::byte>, VfsError> VirtualFileSystem::ReadFile(const std::string& virtualPath) const {
    const std::string vPath = NormalizePath(virtualPath);

    // 1. Loose File Override Check
    // Quake prioritizes actual files on the hard drive over files packed in a .pak.
    // This allows developers to drop a new "progs.dat" into the id1 folder to override the PAK.
    std::filesystem::path loosePath = m_baseDir / vPath;
    if (std::filesystem::exists(loosePath) && std::filesystem::is_regular_file(loosePath)) {
        std::ifstream file(loosePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return std::unexpected(VfsError::AccessDenied);
        }

        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<std::byte> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        
        if (!file) {
            return std::unexpected(VfsError::ReadError);
        }
        return buffer;
    }

    // 2. PAK Registry Check
    auto it = m_fileRegistry.find(vPath);
    if (it == m_fileRegistry.end()) {
        return std::unexpected(VfsError::FileNotFound);
    }

    const FileLocation& loc = it->second;
    std::filesystem::path pakPath = m_baseDir / loc.sourcePak;

    std::ifstream file(pakPath, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(VfsError::AccessDenied);
    }

    // Seek directly to the file data chunk inside the PAK archive
    file.seekg(loc.offset, std::ios::beg);

    std::vector<std::byte> buffer(loc.size);
    file.read(reinterpret_cast<char*>(buffer.data()), loc.size);

    if (!file) {
        return std::unexpected(VfsError::ReadError);
    }

    return buffer;
}

} // namespace engine::vfs