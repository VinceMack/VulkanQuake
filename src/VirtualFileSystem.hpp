#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace qk::vfs {

// ============================================================================
// PAK Binary Structures
// ============================================================================

// Force 1-byte alignment so these structs map directly to the bytes on disk.
#pragma pack(push, 1)

struct PakHeader {
    char    magic[4];   // Must be 'P', 'A', 'C', 'K'
    int32_t dirOffset;  // Offset in file to the directory table
    int32_t dirLength;  // Size of the directory table in bytes
};
static_assert(sizeof(PakHeader) == 12, "PakHeader size must be 12 bytes");

struct PakEntry {
    char    name[56];   // Null-padded virtual file path
    int32_t fileOffset; // Offset in file to the raw data
    int32_t fileLength; // Size of the raw data in bytes
};
static_assert(sizeof(PakEntry) == 64, "PakEntry size must be 64 bytes");

#pragma pack(pop)

// ============================================================================
// VFS Types
// ============================================================================

enum class VfsError {
    FileNotFound,
    AccessDenied,
    InvalidPakFile,
    ReadError
};

// Internal metadata about where a file physically lives
struct FileLocation {
    std::string sourcePak; // Name of the pak file (e.g., "pak0.pak")
    size_t      offset;    // Offset within the pak file
    size_t      size;      // Size of the file in bytes
};

// ============================================================================
// VirtualFileSystem Class
// ============================================================================

class VirtualFileSystem {
public:
    /**
     * @brief Constructs the VFS rooted at a specific base directory.
     * @param baseDir The physical path to the game folder (e.g., "id1").
     */
    explicit VirtualFileSystem(std::filesystem::path baseDir);
    ~VirtualFileSystem() = default;

    // Prevent copying to maintain strict ownership of the file registry
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    /**
     * @brief Mounts a .pak file into the virtual filesystem.
     * @param pakFilename The name of the pak file (e.g., "pak0.pak") relative to baseDir.
     * @return true if successfully mounted, false otherwise.
     */
    bool MountPak(const std::string& pakFilename);

    /**
     * @brief Reads an entire file into memory. Resolves loose files before PAK files.
     * @param virtualPath The path requested by the engine (e.g., "maps/e1m1.bsp").
     * @return A vector of bytes, or a VfsError if the read failed.
     */
    [[nodiscard]] std::expected<std::vector<std::byte>, VfsError> ReadFile(const std::string& virtualPath) const;

private:
    /**
     * @brief Normalizes a file path (converts to lowercase and uses forward slashes).
     */
    [[nodiscard]] std::string NormalizePath(const std::string& path) const;

    std::filesystem::path m_baseDir;
    
    // Hash map linking a virtual path (e.g., "progs.dat") to its physical location.
    std::unordered_map<std::string, FileLocation> m_fileRegistry;
};

} // namespace qk::vfs