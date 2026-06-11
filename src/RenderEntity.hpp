#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace engine {

enum class EntityModelType {
    BspBrush,
    Alias
};

// This is the data structure that will eventually be populated by network packets!
struct RenderEntity {
    uint32_t modelId;      // Which BSP Sub-Model (or .mdl later) to draw
    EntityModelType type;  // Tells the renderer which pipeline to use
    glm::vec3 origin;      // 3D position
    glm::vec3 angles;      // Pitch, Yaw, Roll
    uint32_t frame;        // For animation
    uint32_t nextFrame;
    float interp;

    // Helper to generate the exact Vulkan transformation matrix
    glm::mat4 GetTransformMatrix() const;
};

} // namespace engine
