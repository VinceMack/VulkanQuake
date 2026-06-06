#include "Camera.hpp"
#include <cmath>
#include <algorithm>

namespace engine {

void Camera::ProcessMouse(float xoffset, float yoffset) {
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    yaw -= xoffset;   // Subtract to look right when moving mouse right
    pitch -= yoffset; // Add to look down when moving mouse down // Subtracting because y-coordinates go from bottom to top

    // Constrain pitch so we don't break our neck (and the lookAt matrix doesn't flip)
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    UpdateVectors();
}

void Camera::ProcessKeyboard(float forward, float rightInput, float deltaTime) {
    float velocity = movementSpeed * deltaTime;
    
    // Move along the vectors we calculated from our look direction
    position += front * forward * velocity;
    position += right * rightInput * velocity;
}

void Camera::UpdateVectors() {
    // Convert Euler angles to directional vectors
    // Note: GLM expects radians for trigonometric functions
    glm::vec3 newFront;
    newFront.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
    newFront.y = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
    newFront.z = std::sin(glm::radians(pitch));
    
    front = glm::normalize(newFront);
    
    // Recalculate Right and Up vector
    // Cross product of front and worldUp gets the vector pointing to our mathematical Right
    right = glm::normalize(glm::cross(front, worldUp));  
    up    = glm::normalize(glm::cross(right, front));
}

} // namespace engine