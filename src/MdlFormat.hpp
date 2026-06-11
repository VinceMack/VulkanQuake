#pragma once
#include <cstdint>

namespace engine::mdl {

#pragma pack(push, 1)

struct MdlHeader {
    char    magic[4];       // "IDPO" (ID Polygon Object)
    int32_t version;        // Must be 6
    float   scale[3];       // Multiply byte coordinates by this
    float   scale_origin[3];// Add this to get final 3D position
    float   bounding_radius;
    float   eye_position[3];
    int32_t num_skins;      // Number of textures
    int32_t skin_width;     // Texture width
    int32_t skin_height;    // Texture height
    int32_t num_verts;      // Vertices per frame
    int32_t num_tris;       // Number of triangles
    int32_t num_frames;     // Number of animation frames
    int32_t synctype;       // 0 = sync, 1 = random
    int32_t flags;          // State flags
    float   size;
};

struct MdlTexCoord {
    int32_t on_seam; // 1 if this vertex sits on the texture wrap seam
    int32_t s;       // U coordinate (in pixels, NOT 0.0 to 1.0!)
    int32_t t;       // V coordinate
};

struct MdlTriangle {
    int32_t faces_front; // 1 if it faces the front (used for the seam math)
    int32_t vertices[3]; // Indices into the vertex array
};

struct MdlVertex {
    uint8_t v[3];         // The compressed X, Y, Z coordinates
    uint8_t normal_index; // Index into a pre-computed lookup table of normals
};

struct MdlFrameData {
    MdlVertex min_bounds;
    MdlVertex max_bounds;
    char      name[16];   // e.g., "stand1", "walk1"
};

#pragma pack(pop)

} // namespace engine::mdl
