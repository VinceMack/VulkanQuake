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
    // We will add UVs, Normals, and Lightmap coords later
};

class Map {
public:
    // Takes the raw byte buffer loaded by the VirtualFileSystem
    Map(std::span<const std::byte> bspData);

    const std::vector<RenderVertex>& GetVertices() const { return m_renderVertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_renderIndices; }

private:
    void ParseLumps(std::span<const std::byte> data);
    void TriangulateFaces();

    // Raw pointers mapped directly over the binary data (Zero-copy parsing!)
    const bsp::BspHeader* m_header = nullptr;
    std::span<const bsp::BspVertex> m_bspVertices;
    std::span<const bsp::BspEdge>   m_bspEdges;
    std::span<const int32_t>        m_bspSurfedges;
    std::span<const bsp::BspFace>   m_bspFaces;

    // Converted Vulkan-ready data
    std::vector<RenderVertex> m_renderVertices;
    std::vector<uint32_t>     m_renderIndices;
};

} // namespace engine