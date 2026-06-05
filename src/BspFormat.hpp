#pragma once
#include <cstdint>

namespace engine::bsp {

// Standard Quake 1 BSP Version
constexpr int32_t BSP_VERSION = 29;

#pragma pack(push, 1)

struct BspEntry {
    int32_t offset; // Byte offset from beginning of file
    int32_t size;   // Size of the lump in bytes
};

struct BspHeader {
    int32_t version;       // Must be 29
    BspEntry lumps[15];    // The directory
};

struct BspVertex {
    float x, y, z; // NOTE: Quake is Z-Up!
};

struct BspEdge {
    uint16_t v[2]; // Indices into the Vertex lump
};

struct BspFace {
    uint16_t plane_id;
    uint16_t side;
    int32_t  first_edge; // Index into the Surfedge lump
    int16_t  num_edges;  // How many Surfedges make up this face
    int16_t  texinfo_id; // Which texture is applied?
    uint8_t  lightmap_styles[4];
    int32_t  lightmap_offset;
};

#pragma pack(pop)

// Lump Indices
enum BspLumpIndex {
    LUMP_ENTITIES = 0,
    LUMP_PLANES = 1,
    LUMP_TEXTURES = 2,
    LUMP_VERTICES = 3,
    LUMP_VISIBILITY = 4,
    LUMP_NODES = 5,
    LUMP_TEXINFO = 6,
    LUMP_FACES = 7,
    LUMP_LIGHTING = 8,
    LUMP_CLIPNODES = 9,
    LUMP_LEAVES = 10,
    LUMP_MARKSURFACES = 11,
    LUMP_EDGES = 12,
    LUMP_SURFEDGES = 13,
    LUMP_MODELS = 14
};

} // namespace engine::bsp