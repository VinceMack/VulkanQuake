#include "RenderEntity.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

glm::mat4 RenderEntity::GetTransformMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    
    // 1. Translate to position
    model = glm::translate(model, origin);
    
    // 2. Rotate (Quake order: Yaw (Z), Pitch (Y), Roll (X))
    // Note: GLM takes radians!
    model = glm::rotate(model, glm::radians(angles.y), glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw
    model = glm::rotate(model, glm::radians(angles.x), glm::vec3(0.0f, 1.0f, 0.0f)); // Pitch
    model = glm::rotate(model, glm::radians(angles.z), glm::vec3(1.0f, 0.0f, 0.0f)); // Roll
    
    return model;
}

} // namespace engine
