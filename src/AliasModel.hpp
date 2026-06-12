#pragma once
#include "MdlFormat.hpp"
#include "Map.hpp" // Reusing TextureData
#include <vector>
#include <span>
#include <string>
#include <glm/glm.hpp>

namespace engine {

// A simpler vertex format, since models don't have lightmaps
struct ModelVertex {
    glm::vec3 position;
    glm::vec2 uv;
};

class AliasModel {
public:
    AliasModel(std::span<const std::byte> mdlData, std::span<const std::byte> palette);

    const std::vector<ModelVertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_indices; }
    const TextureData& GetTexture() const { return m_texture; }

    uint32_t GetNumFrames() const { return m_totalFrames; } // <--- UPDATED
    uint32_t GetVerticesPerFrame() const { return static_cast<uint32_t>(m_indices.size()); }

private:
    void ParseSkin(const uint8_t*& ptr, std::span<const std::byte> palette);
    void ParseGeometry(const uint8_t*& ptr);

    const mdl::MdlHeader* m_header = nullptr;
    
    // Raw binary arrays
    std::span<const mdl::MdlTexCoord> m_mdlTexCoords;
    std::span<const mdl::MdlTriangle> m_mdlTriangles;

    // Converted Vulkan arrays
    std::vector<ModelVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    TextureData m_texture;
    uint32_t m_totalFrames = 0; // <--- NEW: true frame count including group frames
};

} // namespace engine
