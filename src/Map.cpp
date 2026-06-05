#include "Map.hpp"
#include <iostream>

namespace engine {

// Helper to extract a strongly-typed span from the binary buffer based on Lump info
template<typename T>
std::span<const T> GetLump(std::span<const std::byte> data, const bsp::BspEntry& entry) {
    if (entry.offset + entry.size > data.size()) {
        throw std::runtime_error("BSP Lump out of bounds");
    }
    const T* ptr = reinterpret_cast<const T*>(data.data() + entry.offset);
    size_t count = entry.size / sizeof(T);
    return std::span<const T>(ptr, count);
}

Map::Map(std::span<const std::byte> bspData) {
    if (bspData.size() < sizeof(bsp::BspHeader)) {
        throw std::runtime_error("File too small to be a BSP");
    }

    m_header = reinterpret_cast<const bsp::BspHeader*>(bspData.data());

    if (m_header->version != bsp::BSP_VERSION) {
        throw std::runtime_error("Unsupported BSP version (expected 29)");
    }

    ParseLumps(bspData);
    TriangulateFaces();
}

void Map::ParseLumps(std::span<const std::byte> data) {
    m_bspVertices  = GetLump<bsp::BspVertex>(data, m_header->lumps[bsp::LUMP_VERTICES]);
    m_bspEdges     = GetLump<bsp::BspEdge>(data,   m_header->lumps[bsp::LUMP_EDGES]);
    m_bspSurfedges = GetLump<int32_t>(data,        m_header->lumps[bsp::LUMP_SURFEDGES]);
    m_bspFaces     = GetLump<bsp::BspFace>(data,   m_header->lumps[bsp::LUMP_FACES]);

    std::cout << "Parsed BSP with " << m_bspFaces.size() << " faces.\n";
}

void Map::TriangulateFaces() {
    // 1. Convert BSP Vertices directly into our RenderVertices
    m_renderVertices.reserve(m_bspVertices.size());
    for (const auto& v : m_bspVertices) {
        m_renderVertices.push_back({ glm::vec3(v.x, v.y, v.z) });
    }

    // 2. Iterate every face and triangulate its edges
    for (const auto& face : m_bspFaces) {
        std::vector<uint32_t> faceVertices;
        faceVertices.reserve(face.num_edges);

        // A. Walk the perimeter of the face
        for (int i = 0; i < face.num_edges; ++i) {
            int32_t surfedge = m_bspSurfedges[face.first_edge + i];
            
            uint32_t vertexIndex = 0;
            if (surfedge >= 0) {
                // Forward edge: take the first vertex
                vertexIndex = m_bspEdges[surfedge].v[0];
            } else {
                // Backward edge: take the second vertex (invert the index)
                vertexIndex = m_bspEdges[-surfedge].v[1];
            }
            faceVertices.push_back(vertexIndex);
        }

        // B. Triangle Fan generation
        // A polygon with N vertices produces (N - 2) triangles.
        // We lock the first vertex as the pivot point.
        for (size_t i = 1; i < faceVertices.size() - 1; ++i) {
            m_renderIndices.push_back(faceVertices[0]);     // Pivot
            m_renderIndices.push_back(faceVertices[i]);     // Next
            m_renderIndices.push_back(faceVertices[i + 1]); // Next + 1
        }
    }

    std::cout << "Triangulated map into " << (m_renderIndices.size() / 3) << " Vulkan triangles.\n";
}

} // namespace engine