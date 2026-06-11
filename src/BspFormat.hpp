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

struct BspPlane {
    float normal[3];
    float dist;
    int32_t type;
};

struct BspNode {
    uint32_t plane_id;
    int16_t children[2]; // If negative, it's a leaf index: leaf = ~child
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t first_face;
    uint16_t num_faces;
};

struct BspLeaf {
    int32_t contents;
    int32_t visofs;      // -1 means no visibility data
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t first_marksurface;
    uint16_t num_marksurfaces;
    uint8_t ambient_level[4];
};

struct BspModel {
    float mins[3], maxs[3]; // Bounding box
    float origin[3];        // Center of the model
    int32_t headnode[4];    // BSP tree nodes
    int32_t visleafs;
    int32_t first_face;     // Index into the face lump
    int32_t num_faces;      // How many faces make up this model
};

#pragma pack(pop)

struct BspTexInfo {
    float vecS[3], distS; // S vector (U coordinate)
    float vecT[3], distT; // T vector (V coordinate)
    uint32_t miptex_id;   // Index into the Texture lump
    uint32_t flags;       // Animation flags (water, sky, etc)
};

struct BspMiptex {
    char name[16];        // Name of the texture (e.g. "metal1_3")
    uint32_t width;       // Width in pixels
    uint32_t height;      // Height in pixels
    uint32_t offset1;     // Offset to mipmap level 0 (full res)
    uint32_t offset2;     // Offset to mipmap level 1 (half res)
    uint32_t offset4;     // Offset to mipmap level 2 (quarter res)
    uint32_t offset8;     // Offset to mipmap level 3 (eighth res)
};

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