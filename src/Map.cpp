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
    ParseEntities(bspData);
    TriangulateFaces();
}

void Map::ParseLumps(std::span<const std::byte> data) {
    m_bspVertices  = GetLump<bsp::BspVertex>(data,  m_header->lumps[bsp::LUMP_VERTICES]);
    m_bspEdges     = GetLump<bsp::BspEdge>(data,    m_header->lumps[bsp::LUMP_EDGES]);
    m_bspSurfedges = GetLump<int32_t>(data,         m_header->lumps[bsp::LUMP_SURFEDGES]);
    m_bspFaces     = GetLump<bsp::BspFace>(data,    m_header->lumps[bsp::LUMP_FACES]);
    m_bspTexInfos  = GetLump<bsp::BspTexInfo>(data, m_header->lumps[bsp::LUMP_TEXINFO]);
    m_bspLighting  = GetLump<uint8_t>(data,         m_header->lumps[bsp::LUMP_LIGHTING]);

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

void Map::ParseEntities(std::span<const std::byte> data) {
    const bsp::BspEntry& entEntry = m_header->lumps[bsp::LUMP_ENTITIES];
    
    // Safety check
    if (entEntry.offset + entEntry.size > data.size()) return;

    const char* start = reinterpret_cast<const char*>(data.data() + entEntry.offset);
    const char* end = start + entEntry.size;

    Entity* currentEntity = nullptr;

    // Helper lambda to skip whitespace
    auto skipWhitespace = [&]() {
        while (start < end && std::isspace(static_cast<unsigned char>(*start))) {
            start++;
        }
    };

    // Helper lambda to read a quoted string
    auto readString = [&]() -> std::string {
        skipWhitespace();
        if (start >= end || *start != '"') return "";
        start++; // skip open quote
        
        const char* s = start;
        while (start < end && *start != '"') start++;
        
        std::string res(s, start);
        if (start < end && *start == '"') start++; // skip close quote
        return res;
    };

    while (start < end) {
        skipWhitespace();
        if (start >= end || *start == '\0') break; // End of file or null terminator

        if (*start == '{') {
            m_entities.emplace_back();
            currentEntity = &m_entities.back();
            start++; // skip '{'
        } else if (*start == '}') {
            currentEntity = nullptr;
            start++; // skip '}'
        } else if (*start == '"') {
            // We found a key-value pair!
            std::string key = readString();
            std::string value = readString();
            
            if (currentEntity && !key.empty()) {
                currentEntity->AddProperty(key, value);
            }
        } else {
            // Unexpected character (e.g., malformed map), just skip it to prevent infinite loops
            start++;
        }
    }

    std::cout << "Parsed " << m_entities.size() << " entities.\n";
}

void Map::TriangulateFaces() {
    m_renderVertices.clear();
    m_renderIndices.clear();
    m_renderBatches.clear();

    // Create a bucket for every texture
    std::vector<std::vector<uint32_t>> indicesByTexture(m_textures.size());

    // --- LIGHTMAP ATLAS SETUP ---
    m_lightmapAtlas.name = "LightmapAtlas";
    m_lightmapAtlas.width = 1024;
    m_lightmapAtlas.height = 1024;
    m_lightmapAtlas.pixelsRGBA.resize(1024 * 1024 * 4, std::byte{0});

    int atlasX = 0;
    int atlasY = 0;
    int atlasRowHeight = 0;

    // Reserve a 1x1 full-bright white pixel at (0,0) for surfaces without lightmaps (skies, slime)
    m_lightmapAtlas.pixelsRGBA[0] = std::byte{255}; // R
    m_lightmapAtlas.pixelsRGBA[1] = std::byte{255}; // G
    m_lightmapAtlas.pixelsRGBA[2] = std::byte{255}; // B
    m_lightmapAtlas.pixelsRGBA[3] = std::byte{255}; // A
    atlasX = 1; 
    atlasRowHeight = 1;
    // ----------------------------

    for (const auto& face : m_bspFaces) {
        const auto& texInfo = m_bspTexInfos[face.texinfo_id];
        uint32_t texID = texInfo.miptex_id >= m_textures.size() ? 0 : texInfo.miptex_id;
        
        uint32_t texWidth = m_textures[texID].width == 0 ? 1 : m_textures[texID].width;
        uint32_t texHeight = m_textures[texID].height == 0 ? 1 : m_textures[texID].height;

        // --- PASS 1: Calculate Lightmap Bounds ---
        float min_u = 999999.0f, max_u = -999999.0f;
        float min_v = 999999.0f, max_v = -999999.0f;

        for (int i = 0; i < face.num_edges; ++i) {
            int32_t surfedge = m_bspSurfedges[face.first_edge + i];
            uint32_t vIndex = (surfedge >= 0) ? m_bspEdges[surfedge].v[0] : m_bspEdges[-surfedge].v[1];
            const auto& bspVert = m_bspVertices[vIndex];

            float u = bspVert.x * texInfo.vecS[0] + bspVert.y * texInfo.vecS[1] + bspVert.z * texInfo.vecS[2] + texInfo.distS;
            float v = bspVert.x * texInfo.vecT[0] + bspVert.y * texInfo.vecT[1] + bspVert.z * texInfo.vecT[2] + texInfo.distT;
            
            min_u = std::min(min_u, u); max_u = std::max(max_u, u);
            min_v = std::min(min_v, v); max_v = std::max(max_v, v);
        }

        // Quake lightmaps are 1/16th the resolution of the diffuse texture
        int lm_min_u = (int)std::floor(min_u / 16.0f);
        int lm_max_u = (int)std::ceil(max_u / 16.0f);
        int lm_min_v = (int)std::floor(min_v / 16.0f);
        int lm_max_v = (int)std::ceil(max_v / 16.0f);

        int lm_width = (lm_max_u - lm_min_u) + 1;
        int lm_height = (lm_max_v - lm_min_v) + 1;

        int currentAtlasX = 0;
        int currentAtlasY = 0;

        // Pack into Atlas if it has a lightmap
        if (face.lightmap_offset != -1) {
            if (atlasX + lm_width > m_lightmapAtlas.width) {
                atlasX = 0;
                atlasY += atlasRowHeight;
                atlasRowHeight = 0;
            }
            
            currentAtlasX = atlasX;
            currentAtlasY = atlasY;
            
            // Copy pixels from BSP Lump 8 into our RGBA Atlas
            for (int y = 0; y < lm_height; ++y) {
                for (int x = 0; x < lm_width; ++x) {
                    uint8_t brightness = m_bspLighting[face.lightmap_offset + (y * lm_width) + x];
                    
                    int atlasPixelIndex = ((currentAtlasY + y) * m_lightmapAtlas.width + (currentAtlasX + x)) * 4;
                    m_lightmapAtlas.pixelsRGBA[atlasPixelIndex + 0] = static_cast<std::byte>(brightness);
                    m_lightmapAtlas.pixelsRGBA[atlasPixelIndex + 1] = static_cast<std::byte>(brightness);
                    m_lightmapAtlas.pixelsRGBA[atlasPixelIndex + 2] = static_cast<std::byte>(brightness);
                    m_lightmapAtlas.pixelsRGBA[atlasPixelIndex + 3] = std::byte{255};
                }
            }
            
            atlasX += lm_width;
            atlasRowHeight = std::max(atlasRowHeight, lm_height);
        }

        // --- PASS 2: Generate Vertices with UVs ---
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
            
            rv.uv.x = u / (float)texWidth;
            rv.uv.y = v / (float)texHeight;

            if (face.lightmap_offset != -1) {
                // Calculate position inside the specific lightmap block (offset by half a pixel for sampling accuracy)
                float u_offset = (u / 16.0f) - lm_min_u + 0.5f;
                float v_offset = (v / 16.0f) - lm_min_v + 0.5f;
                // Convert to global Atlas UVs
                rv.lightmapUV.x = (currentAtlasX + u_offset) / (float)m_lightmapAtlas.width;
                rv.lightmapUV.y = (currentAtlasY + v_offset) / (float)m_lightmapAtlas.height;
            } else {
                // Point to the fullbright white pixel we created at 0,0
                rv.lightmapUV.x = 0.5f / (float)m_lightmapAtlas.width;
                rv.lightmapUV.y = 0.5f / (float)m_lightmapAtlas.height;
            }

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
        if (indicesByTexture[t].empty()) continue;
        RenderBatch batch;
        batch.textureId = t;
        batch.firstIndex = static_cast<uint32_t>(m_renderIndices.size());
        batch.indexCount = static_cast<uint32_t>(indicesByTexture[t].size());
        m_renderBatches.push_back(batch);
        m_renderIndices.insert(m_renderIndices.end(), indicesByTexture[t].begin(), indicesByTexture[t].end());
    }

    std::cout << "Packed Lightmaps into Atlas. Max Y used: " << (atlasY + atlasRowHeight) << " / 1024\n";
}

} // namespace engine