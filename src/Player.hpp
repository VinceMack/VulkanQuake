#pragma once
#include "Physics.hpp"
#include "Camera.hpp"
#include "RenderEntity.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace engine {

struct UserCmd {
    float msec;
    float forwardmove;
    float sidemove;
    float upmove;
    float pitch;
    float yaw;
    float roll;
};

class Player {
public:
    Player(Physics* physics, Camera* camera);

    void Spawn(glm::vec3 origin, float yaw);

    void ProcessMouse(float xoffset, float yoffset);
    void TickPhysics(const UserCmd& cmd, const std::vector<RenderEntity>& entities);

    glm::vec3 GetPosition() const { return m_position; }

private:
    void CheckStuck();
    void ClipVelocity(const glm::vec3& in, const glm::vec3& normal, glm::vec3& out, float overbounce);
    int SlideMove(float deltaTime, TraceResult* stepTrace = nullptr);
    void StepSlideMove(float deltaTime);

    void WallFriction(const UserCmd& cmd, const TraceResult& trace);

    // Quake exactly defined constants
    const float sv_gravity = 800.0f;
    const float sv_maxspeed = 320.0f;
    const float sv_maxairspeed = 30.0f; 
    const float sv_accelerate = 10.0f;
    const float sv_airaccelerate = 10.0f; 
    const float sv_friction = 4.0f;
    const float sv_edgefriction = 2.0f;
    const float sv_stopspeed = 100.0f;
    const float sv_stepsize = 18.0f;

    void PM_Friction(float deltaTime);
    void PM_Accelerate(glm::vec3 wishdir, float wishspeed, float accel, float deltaTime);
    void PM_AirMove(const UserCmd& cmd, float deltaTime);
    void PM_GroundMove(const UserCmd& cmd, float deltaTime);

    Physics* m_physics;
    Camera* m_camera;

    glm::vec3 m_position;
    glm::vec3 m_velocity;

    bool m_onGround = false;

    const std::vector<RenderEntity>* m_currentEntities = nullptr;
};

} // namespace engine
