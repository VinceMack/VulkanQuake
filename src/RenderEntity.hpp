#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace engine {

enum class EntityModelType { BspBrush, Alias };

// ---> NEW: State machine for moving brushes
enum class BrushState { Static, Closed, Opening, Open, Closing };

struct RenderEntity {
    uint32_t modelId;
    EntityModelType type;
    glm::vec3 origin;
    glm::vec3 angles;
    uint32_t frame;
    uint32_t nextFrame;
    float interp;

    bool isSolid = true;   
    bool isVisible = true; 
    bool isTrigger = false;
    std::string triggerTarget; 

    // ---> NEW: Entity Targeting System
    std::string targetname; // The name this entity listens for
    std::string target;     // The event this entity fires when activated
    bool requireTrigger = false; // True if it has a targetname (cannot be proximity-opened) 

    // ---> UPDATED: Local bounds so they can move with the origin
    glm::vec3 localMins; 
    glm::vec3 localMaxs;

    // ---> NEW: Kinematic state variables
    BrushState brushState = BrushState::Static;
    glm::vec3 pos1; // Closed position (usually 0,0,0 local)
    glm::vec3 pos2; // Open position
    float speed;
    float wait;
    float stateTimer;

    glm::mat4 GetTransformMatrix() const;
    
    // Helper to get absolute bounds
    glm::vec3 GetAbsMins() const { return origin + localMins; }
    glm::vec3 GetAbsMaxs() const { return origin + localMaxs; }
};

} // namespace engine
