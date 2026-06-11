#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace engine {

// This is the data structure that will eventually be populated by network packets!
struct RenderEntity {
    uint32_t modelId;      // Which BSP Sub-Model (or .mdl later) to draw
    glm::vec3 origin;      // 3D position
    glm::vec3 angles;      // Pitch, Yaw, Roll
    uint32_t frame;        // For animation (unused for doors, but needed for monsters later)

    // Helper to generate the exact Vulkan transformation matrix
    glm::mat4 GetTransformMatrix() const;
};

} // namespace engine
