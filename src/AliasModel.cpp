#include "AliasModel.hpp"
#include <stdexcept>
#include <iostream>

namespace engine {

AliasModel::AliasModel(std::span<const std::byte> mdlData, std::span<const std::byte> palette) {
    if (mdlData.size() < sizeof(mdl::MdlHeader)) {
        throw std::runtime_error("MDL file too small.");
    }

    m_header = reinterpret_cast<const mdl::MdlHeader*>(mdlData.data());

    if (m_header->magic[0] != 'I' || m_header->magic[1] != 'D' ||
        m_header->magic[2] != 'P' || m_header->magic[3] != 'O' || m_header->version != 6) {
        throw std::runtime_error("Invalid MDL magic or version.");
    }

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(mdlData.data()) + sizeof(mdl::MdlHeader);

    ParseSkin(ptr, palette);
    ParseGeometry(ptr);
}

void AliasModel::ParseSkin(const uint8_t*& ptr, std::span<const std::byte> palette) {
    for (int32_t i = 0; i < m_header->num_skins; ++i) {
        int32_t skinType = *reinterpret_cast<const int32_t*>(ptr);
        ptr += sizeof(int32_t);

        int32_t numSkinsToRead = 1;
        
        // Handle Group Skins!
        if (skinType != 0) {
            numSkinsToRead = *reinterpret_cast<const int32_t*>(ptr);
            ptr += sizeof(int32_t);
            ptr += sizeof(float) * numSkinsToRead; // Skip the array of float time intervals
        }

        uint32_t numPixels = m_header->skin_width * m_header->skin_height;

        if (i == 0) {
            m_texture.width = m_header->skin_width;
            m_texture.height = m_header->skin_height;
            m_texture.pixelsRGBA.resize(numPixels * 4);

            const uint8_t* pal = reinterpret_cast<const uint8_t*>(palette.data());

            // We only extract the FIRST skin in the first group to keep our single-texture rendering simple
            for (uint32_t p = 0; p < numPixels; ++p) {
                uint8_t colorIndex = ptr[p];
                bool isTransparent = (colorIndex == 255);

                m_texture.pixelsRGBA[p * 4 + 0] = static_cast<std::byte>(pal[colorIndex * 3 + 0]);
                m_texture.pixelsRGBA[p * 4 + 1] = static_cast<std::byte>(pal[colorIndex * 3 + 1]);
                m_texture.pixelsRGBA[p * 4 + 2] = static_cast<std::byte>(pal[colorIndex * 3 + 2]);
                m_texture.pixelsRGBA[p * 4 + 3] = isTransparent ? std::byte{0} : std::byte{255};
            }
        }
        
        ptr += numPixels; // Advance past the first skin of this skin slot

        // If this was a group skin, we MUST skip the remaining skins to keep the binary pointer aligned!
        if (numSkinsToRead > 1) {
            ptr += numPixels * (numSkinsToRead - 1);
        }
    }
}

void AliasModel::ParseGeometry(const uint8_t*& ptr) {
    m_mdlTexCoords = std::span<const mdl::MdlTexCoord>(reinterpret_cast<const mdl::MdlTexCoord*>(ptr), m_header->num_verts);
    ptr += m_header->num_verts * sizeof(mdl::MdlTexCoord);

    m_mdlTriangles = std::span<const mdl::MdlTriangle>(reinterpret_cast<const mdl::MdlTriangle*>(ptr), m_header->num_tris);
    ptr += m_header->num_tris * sizeof(mdl::MdlTriangle);

    m_vertices.clear();
    m_indices.clear();
    m_totalFrames = 0;

    // Loop through all top-level frames in the header
    for (int f = 0; f < m_header->num_frames; ++f) {
        int32_t frameType = *reinterpret_cast<const int32_t*>(ptr);
        ptr += sizeof(int32_t);

        int32_t numFramesInGroup = 1;

        // Handle Group Frames!
        if (frameType != 0) {
            numFramesInGroup = *reinterpret_cast<const int32_t*>(ptr);
            ptr += sizeof(int32_t);
            ptr += sizeof(mdl::MdlVertex) * 2;       // Skip group min/max bounding boxes
            ptr += sizeof(float) * numFramesInGroup; // Skip the array of float time intervals
        }

        // Flatten the group into sequential frames
        for (int gf = 0; gf < numFramesInGroup; ++gf) {
            ptr += sizeof(mdl::MdlFrameData); // Skip frame bounding box and name metadata

            const mdl::MdlVertex* verts = reinterpret_cast<const mdl::MdlVertex*>(ptr);
            std::span<const mdl::MdlVertex> frameVerts(verts, m_header->num_verts);
            ptr += m_header->num_verts * sizeof(mdl::MdlVertex);

            // Unroll the triangles
            for (int t = 0; t < m_header->num_tris; ++t) {
                const auto& tri = m_mdlTriangles[t];

                for (int v = 0; v < 3; ++v) {
                    int vertIndex = tri.vertices[v];
                    const auto& compressedVert = frameVerts[vertIndex];
                    const auto& st = m_mdlTexCoords[vertIndex];

                    ModelVertex rv;
                    rv.position.x = (compressedVert.v[0] * m_header->scale[0]) + m_header->scale_origin[0];
                    rv.position.y = (compressedVert.v[1] * m_header->scale[1]) + m_header->scale_origin[1];
                    rv.position.z = (compressedVert.v[2] * m_header->scale[2]) + m_header->scale_origin[2];

                    float s = (float)st.s;
                    if (st.on_seam && !tri.faces_front) {
                        s += (float)m_header->skin_width / 2.0f;
                    }
                    rv.uv.x = (s + 0.5f) / (float)m_header->skin_width;
                    rv.uv.y = ((float)st.t + 0.5f) / (float)m_header->skin_height;

                    m_vertices.push_back(rv);
                    
                    // Only build the index buffer once during the very first frame
                    if (m_totalFrames == 0) {
                        m_indices.push_back(m_vertices.size() - 1);
                    }
                }
            }
            m_totalFrames++;
        }
    }
}

} // namespace engine
