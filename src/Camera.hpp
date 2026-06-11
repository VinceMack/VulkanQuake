#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

class Camera {
public:
    // We start looking slightly down, and spawn near E1M1's actual starting room
    Camera(glm::vec3 startPosition) : position(startPosition) {
        UpdateVectors();
    }

    void ProcessMouse(float xoffset, float yoffset);
    void ProcessKeyboard(float forward, float right, float deltaTime);
    void SetPositionAndYaw(glm::vec3 pos, float startYaw);

    glm::mat4 GetViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    glm::vec3 GetPosition() const { return position; }

private:
    void UpdateVectors();

    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    
    // Z is UP in Quake!
    glm::vec3 worldUp = glm::vec3(0.0f, 0.0f, 1.0f); 

    // Euler Angles
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Camera options
    float movementSpeed = 600.0f; // Quake units per second
    float mouseSensitivity = 0.15f;
};

} // namespace engine