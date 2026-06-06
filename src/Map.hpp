#pragma once
#include "BspFormat.hpp"
#include <vector>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <glm/glm.hpp>

namespace engine {

// This is what we will eventually send to Vulkan
struct RenderVertex {
    glm::vec3 position;
    glm::vec2 uv;       // Texture coordinates
    uint32_t textureId; // Which texture does this vertex use?
};

// Holds our extracted, Vulkan-ready RGBA pixels
struct TextureData {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> pixelsRGBA; 
};

struct RenderBatch {
    uint32_t textureId;
    uint32_t firstIndex;
    uint32_t indexCount;
};

class Map {
public:
    // Takes the raw byte buffer loaded by the VirtualFileSystem
    Map(std::span<const std::byte> bspData, std::span<const std::byte> paletteData);

    const std::vector<RenderBatch>& GetRenderBatches() const { return m_renderBatches; }
    const std::vector<RenderVertex>& GetVertices() const { return m_renderVertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_renderIndices; }

    // Expose the extracted textures
    const std::vector<TextureData>& GetTextures() const { return m_textures; }

private:
    void ParseLumps(std::span<const std::byte> data);
    void ParseTextures(std::span<const std::byte> data, std::span<const std::byte> palette);
    void TriangulateFaces();

    // Raw pointers mapped directly over the binary data (Zero-copy parsing!)
    const bsp::BspHeader* m_header = nullptr;
    std::span<const bsp::BspVertex> m_bspVertices;
    std::span<const bsp::BspEdge>   m_bspEdges;
    std::span<const int32_t>        m_bspSurfedges;
    std::span<const bsp::BspFace>   m_bspFaces;
    std::span<const bsp::BspTexInfo> m_bspTexInfos;

    // Converted Vulkan-ready data
    std::vector<RenderBatch>  m_renderBatches;
    std::vector<RenderVertex> m_renderVertices;
    std::vector<uint32_t>     m_renderIndices;
    std::vector<TextureData>  m_textures;
};

} // namespace engine