#pragma once
#include "BspFormat.hpp"
#include "Entity.hpp"
#include "RenderEntity.hpp"
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
    glm::vec2 lightmapUV; // For future lightmap support
    uint32_t textureId; // Which texture does this vertex use?
    uint8_t styles[4];  // <--- NEW: Stores the 4 lightstyle IDs for this face
};

// Holds our extracted, Vulkan-ready RGBA pixels
struct TextureData {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> pixelsRGBA; 
    uint32_t surfaceType = 0; // <--- NEW (0=Normal, 1=Liquid, 2=Sky)
};

struct RenderBatch {
    uint32_t textureId;
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t surfaceType = 0; // <--- NEW
};

struct FaceData {
    uint32_t textureId;
    uint32_t firstIndex;
    uint32_t indexCount;
};

struct SubModel {
    std::vector<RenderBatch> batches;
};

class Map {
public:
    Map(std::vector<std::byte> bspData, std::span<const std::byte> paletteData);

    const std::vector<RenderVertex>& GetVertices() const { return m_renderVertices; }
    const std::vector<TextureData>& GetTextures() const { return m_textures; }
    const TextureData& GetLightmapAtlas() const { return m_lightmapAtlas; }
    const std::vector<Entity>& GetEntities() const { return m_entities; }

    std::span<const bsp::BspPlane> GetPlanes() const { return m_bspPlanes; }
    std::span<const bsp::BspClipNode> GetClipNodes() const { return m_bspClipNodes; }
    const bsp::BspModel& GetBspModel(uint32_t index) const { return m_bspModels[index]; }

    uint32_t GetMaxIndexCount() const { return static_cast<uint32_t>(m_masterIndices.size()); }
    const std::vector<uint32_t>& GetMasterIndices() const { return m_masterIndices; }

    // Get a specific Sub-Model to draw it
    const SubModel& GetSubModel(uint32_t modelId) const;

    // Dynamic PVS querying
    // Takes the camera position and populates the output arrays with only visible geometry
    void BuildVisibleBatches(const glm::vec3& cameraPos, 
                             std::vector<uint32_t>& outIndices, 
                             std::vector<RenderBatch>& outBatches) const;

private:
    void ParseLumps(std::span<const std::byte> data);
    void ParseTextures(std::span<const std::byte> data, std::span<const std::byte> palette);
    void ParseEntities(std::span<const std::byte> data);
    void TriangulateFaces();

    // PVS Math Helpers
    int FindCameraLeaf(const glm::vec3& cameraPos) const;
    std::vector<uint8_t> DecompressPVS(int leafIndex) const;
    bool CheckBit(const std::vector<uint8_t>& pvs, int leafIndex) const;

    const bsp::BspHeader* m_header = nullptr;
    std::span<const bsp::BspVertex>  m_bspVertices;
    std::span<const bsp::BspEdge>    m_bspEdges;
    std::span<const int32_t>         m_bspSurfedges;
    std::span<const bsp::BspFace>    m_bspFaces;
    std::span<const bsp::BspTexInfo> m_bspTexInfos;
    std::span<const uint8_t>         m_bspLighting;
    std::vector<std::byte> m_bspRawData;
    
    // Span maps for the tree traversal lumps
    std::span<const bsp::BspPlane>   m_bspPlanes;
    std::span<const bsp::BspNode>    m_bspNodes;
    std::span<const bsp::BspLeaf>    m_bspLeaves;
    std::span<const bsp::BspClipNode> m_bspClipNodes;
    std::span<const uint8_t>         m_bspVisibility;
    std::span<const uint16_t>        m_bspMarkSurfaces;
    std::span<const bsp::BspModel>   m_bspModels; // LUMP
    std::vector<SubModel>            m_subModels;   // STORAGE

    std::vector<RenderVertex> m_renderVertices;
    std::vector<TextureData>  m_textures;
    TextureData m_lightmapAtlas;
    std::vector<Entity> m_entities;

    // Master Index storage
    std::vector<uint32_t> m_masterIndices;
    std::vector<FaceData> m_faceData;
};

} // namespace engine