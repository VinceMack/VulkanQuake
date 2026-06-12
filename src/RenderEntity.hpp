#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

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

    // ---> NEW FLAGS
    bool isSolid = true;   
    bool isVisible = true; 

    // ---> NEW: Trigger logic data
    bool isTrigger = false;
    std::string triggerTarget; // E.g., "e1m2"
    glm::vec3 bboxMin;         // The absolute minimum X,Y,Z bounds
    glm::vec3 bboxMax;         // The absolute maximum X,Y,Z bounds

    // Helper to generate the exact Vulkan transformation matrix
    glm::mat4 GetTransformMatrix() const;
};

} // namespace engine
