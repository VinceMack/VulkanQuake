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

Map::Map(std::span<const std::byte> bspData, std::span<const std::byte> paletteData) {
    if (bspData.size() < sizeof(bsp::BspHeader)) {
        throw std::runtime_error("File too small to be a BSP");
    }
    if (paletteData.size() < 768) {
        throw std::runtime_error("Invalid palette file (must be at least 768 bytes)");
    }

    m_header = reinterpret_cast<const bsp::BspHeader*>(bspData.data());

    if (m_header->version != bsp::BSP_VERSION) {
        throw std::runtime_error("Unsupported BSP version (expected 29)");
    }

    ParseLumps(bspData);
    ParseTextures(bspData, paletteData);
    TriangulateFaces();
}

void Map::ParseLumps(std::span<const std::byte> data) {
    m_bspVertices  = GetLump<bsp::BspVertex>(data, m_header->lumps[bsp::LUMP_VERTICES]);
    m_bspEdges     = GetLump<bsp::BspEdge>(data,   m_header->lumps[bsp::LUMP_EDGES]);
    m_bspSurfedges = GetLump<int32_t>(data,        m_header->lumps[bsp::LUMP_SURFEDGES]);
    m_bspFaces     = GetLump<bsp::BspFace>(data,   m_header->lumps[bsp::LUMP_FACES]);
    m_bspTexInfos  = GetLump<bsp::BspTexInfo>(data, m_header->lumps[bsp::LUMP_TEXINFO]);

    std::cout << "Parsed BSP with " << m_bspFaces.size() << " faces.\n";
}

void Map::ParseTextures(std::span<const std::byte> data, std::span<const std::byte> palette) {
    const bsp::BspEntry& texEntry = m_header->lumps[bsp::LUMP_TEXTURES];
    const std::byte* texLumpStart = data.data() + texEntry.offset;

    const int32_t* numTexturesPtr = reinterpret_cast<const int32_t*>(texLumpStart);
    int32_t numTextures = *numTexturesPtr;
    const int32_t* offsets = numTexturesPtr + 1;

    m_textures.resize(numTextures);
    const uint8_t* pal = reinterpret_cast<const uint8_t*>(palette.data());

    for (int i = 0; i < numTextures; ++i) {
        if (offsets[i] == -1) continue; // Some animated textures are missing/blank

        const bsp::BspMiptex* miptex = reinterpret_cast<const bsp::BspMiptex*>(texLumpStart + offsets[i]);
        
        TextureData& td = m_textures[i];
        td.name = miptex->name;
        td.width = miptex->width;
        td.height = miptex->height;

        uint32_t numPixels = td.width * td.height;
        td.pixelsRGBA.resize(numPixels * 4); // 4 bytes per pixel (RGBA)

        // Point to mip level 0 (full resolution)
        const uint8_t* pixels8 = reinterpret_cast<const uint8_t*>(miptex) + miptex->offset1;

        // Convert 8-bit indices to 32-bit RGBA using the palette
        for (uint32_t p = 0; p < numPixels; ++p) {
            uint8_t colorIndex = pixels8[p];
            
            // Quake maps color index 255 to completely transparent.
            bool isTransparent = (colorIndex == 255);

            td.pixelsRGBA[p * 4 + 0] = static_cast<std::byte>(pal[colorIndex * 3 + 0]); // R
            td.pixelsRGBA[p * 4 + 1] = static_cast<std::byte>(pal[colorIndex * 3 + 1]); // G
            td.pixelsRGBA[p * 4 + 2] = static_cast<std::byte>(pal[colorIndex * 3 + 2]); // B
            td.pixelsRGBA[p * 4 + 3] = isTransparent ? std::byte{0} : std::byte{255};   // A
        }
    }
    std::cout << "Extracted " << numTextures << " textures from BSP.\n";
}

void Map::TriangulateFaces() {
    m_renderVertices.clear();
    m_renderIndices.clear();
    m_renderBatches.clear();

    // Create a bucket for every texture
    std::vector<std::vector<uint32_t>> indicesByTexture(m_textures.size());

    for (const auto& face : m_bspFaces) {
        const auto& texInfo = m_bspTexInfos[face.texinfo_id];
        uint32_t texID = texInfo.miptex_id;
        // Safety check
        if (texID >= m_textures.size()) texID = 0;

        uint32_t texWidth = m_textures[texID].width;
        uint32_t texHeight = m_textures[texID].height;
        // Since some animated textures are blank, protect against divide-by-zero
        if (texWidth == 0 || texHeight == 0) { texWidth = 1; texHeight = 1; }

        std::vector<uint32_t> faceIndices;

        for (int i = 0; i < face.num_edges; ++i) {
            int32_t surfedge = m_bspSurfedges[face.first_edge + i];
            uint32_t vIndex = (surfedge >= 0) ? m_bspEdges[surfedge].v[0] : m_bspEdges[-surfedge].v[1];
            const auto& bspVert = m_bspVertices[vIndex];

            // Create a unique vertex for this polygon corner
            RenderVertex rv;
            rv.position = glm::vec3(bspVert.x, bspVert.y, bspVert.z);
            rv.textureId = texID;

            // Compute UVs using Quake's projection math
            float u = bspVert.x * texInfo.vecS[0] + bspVert.y * texInfo.vecS[1] + bspVert.z * texInfo.vecS[2] + texInfo.distS;
            float v = bspVert.x * texInfo.vecT[0] + bspVert.y * texInfo.vecT[1] + bspVert.z * texInfo.vecT[2] + texInfo.distT;
            
            // Normalize the UVs for Vulkan (0.0 to 1.0)
            rv.uv.x = u / (float)texWidth;
            rv.uv.y = v / (float)texHeight;

            m_renderVertices.push_back(rv);
            faceIndices.push_back(m_renderVertices.size() - 1);
        }

        // Drop the triangulated indices directly into the correct Texture Bucket
        for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
            indicesByTexture[texID].push_back(faceIndices[0]);
            indicesByTexture[texID].push_back(faceIndices[i]);
            indicesByTexture[texID].push_back(faceIndices[i + 1]);
        }
    }

    // Now, sequentialize the buckets into our final index buffer and record the batches
    for (uint32_t t = 0; t < indicesByTexture.size(); ++t) {
        if (indicesByTexture[t].empty()) continue; // Skip textures not used in this map

        RenderBatch batch;
        batch.textureId = t;
        batch.firstIndex = static_cast<uint32_t>(m_renderIndices.size());
        batch.indexCount = static_cast<uint32_t>(indicesByTexture[t].size());
        
        m_renderBatches.push_back(batch);
        
        // Append all indices for this texture to the master buffer
        m_renderIndices.insert(m_renderIndices.end(), indicesByTexture[t].begin(), indicesByTexture[t].end());
    }

    std::cout << "Triangulated map into " << (m_renderIndices.size() / 3) << " unique Vulkan triangles.\n";
}

} // namespace engine