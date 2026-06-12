#include "Player.hpp"

namespace engine {

Player::Player(Physics* physics, Camera* camera) 
    : m_physics(physics), m_camera(camera), m_position(0.0f), m_velocity(0.0f) {}

void Player::Spawn(glm::vec3 origin, float yaw) {
    m_position = origin;
    m_velocity = glm::vec3(0.0f);
    m_camera->SetPositionAndYaw(m_position + glm::vec3(0.0f, 0.0f, 22.0f), yaw);
}

void Player::ProcessMouse(float xoffset, float yoffset) {
    m_camera->ProcessMouse(xoffset, yoffset);
}

void Player::CheckStuck() {
    TraceResult trace = m_physics->TraceHull(m_position, m_position, 1, *m_currentEntities);
    if (!trace.startSolid) return; // We are fine

    glm::vec3 org = m_position;
    
    // Test moving up to 18 units up, and 1 unit in each horizontal direction
    for (int z = 0; z < 18; z++) {
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                m_position.x = org.x + i;
                m_position.y = org.y + j;
                m_position.z = org.z + z;
                
                trace = m_physics->TraceHull(m_position, m_position, 1, *m_currentEntities);
                if (!trace.startSolid) {
                    return; // Unstuck! Keep the new m_position.
                }
            }
        }
    }
    
    // Failed to unstick
    m_position = org; 
}

void Player::ClipVelocity(const glm::vec3& in, const glm::vec3& normal, glm::vec3& out, float overbounce) {
    float backoff = glm::dot(in, normal) * overbounce;
    for (int i = 0; i < 3; i++) {
        float change = normal[i] * backoff;
        out[i] = in[i] - change;
        // STOP_EPSILON: kill tiny velocities to prevent micro-oscillations
        if (out[i] > -0.1f && out[i] < 0.1f) {
            out[i] = 0.0f;
        }
    }
}

int Player::SlideMove(float deltaTime, TraceResult* stepTrace) {
    int bumpcount, numbumps = 4;
    glm::vec3 dir;
    float d;
    int numplanes = 0;
    glm::vec3 planes[5];
    glm::vec3 primal_velocity = m_velocity;
    glm::vec3 original_velocity = m_velocity;
    glm::vec3 new_velocity;
    int blocked = 0;
    float time_left = deltaTime;

    for (bumpcount = 0; bumpcount < numbumps; bumpcount++) {
        if (m_velocity.x == 0.0f && m_velocity.y == 0.0f && m_velocity.z == 0.0f) {
            break;
        }

        glm::vec3 end = m_position + m_velocity * time_left;
        TraceResult trace = m_physics->TraceHull(m_position, end, 1, *m_currentEntities);

        if (trace.allSolid) {
            m_velocity = glm::vec3(0.0f);
            return 3;
        }

        if (trace.fraction > 0.0f) {
            m_position = trace.endPos;
            original_velocity = m_velocity;
            numplanes = 0;
        }

        if (trace.fraction == 1.0f) {
            break; // Moved the entire distance
        }

        if (trace.planeNormal.z > 0.7f) {
            blocked |= 1; // Floor
            m_onGround = true;
        }
        if (trace.planeNormal.z == 0.0f) {
            blocked |= 2; // Wall/Step
            if (stepTrace) *stepTrace = trace;
        }

        time_left -= time_left * trace.fraction;

        if (numplanes >= 5) {
            m_velocity = glm::vec3(0.0f);
            return 3;
        }

        planes[numplanes++] = trace.planeNormal;

        // Modify velocity to parallel ALL accumulated planes
        int i, j;
        for (i = 0; i < numplanes; i++) {
            ClipVelocity(original_velocity, planes[i], new_velocity, 1.0f);
            for (j = 0; j < numplanes; j++) {
                if (j != i) {
                    if (glm::dot(new_velocity, planes[j]) < 0.0f) {
                        break; // Not okay, violates another plane
                    }
                }
            }
            if (j == numplanes) {
                break; // Valid velocity found
            }
        }

        if (i != numplanes) {
            // Go along this plane
            m_velocity = new_velocity;
        } else {
            // We hit a corner, go along the CREASE
            if (numplanes != 2) {
                m_velocity = glm::vec3(0.0f);
                return 7;
            }
            dir = glm::cross(planes[0], planes[1]);
            d = glm::dot(dir, m_velocity);
            m_velocity = dir * d;
        }

        // If original velocity is against primal velocity, stop dead
        if (glm::dot(m_velocity, primal_velocity) <= 0.0f) {
            m_velocity = glm::vec3(0.0f);
            return blocked;
        }
    }
    return blocked;
}

void Player::WallFriction(const UserCmd& cmd, const TraceResult& trace) {
    glm::vec3 forwardDir(std::cos(glm::radians(cmd.yaw)), std::sin(glm::radians(cmd.yaw)), 0.0f);
    float d = glm::dot(trace.planeNormal, forwardDir);
    d += 0.5f;
    if (d >= 0.0f) return;

    // Cut tangential velocity based on view angle into wall
    float i = glm::dot(trace.planeNormal, m_velocity);
    glm::vec3 into = trace.planeNormal * i;
    glm::vec3 side = m_velocity - into;

    m_velocity.x = side.x * (1.0f + d);
    m_velocity.y = side.y * (1.0f + d);
}

void Player::StepSlideMove(float deltaTime) {
    glm::vec3 start_o = m_position;
    glm::vec3 start_v = m_velocity;
    TraceResult steptrace;

    int clip = SlideMove(deltaTime, &steptrace);

    if (!(clip & 2)) return; // Didn't block on a step/wall
    if (!m_onGround) return; // Don't step up in mid-air

    glm::vec3 down_o = m_position;
    glm::vec3 down_v = m_velocity;
    
    m_position = start_o;
    m_velocity = start_v;

    // Step Up
    m_position.z += sv_stepsize;

    // Move Forward horizontally
    m_velocity.z = 0.0f;
    clip = SlideMove(deltaTime, &steptrace);

    if (clip & 2) {
        // We hit another wall while stepping, apply Quake's Wall Friction
        UserCmd dummyCmd; dummyCmd.yaw = m_camera->GetYaw(); // Re-grab intent
        WallFriction(dummyCmd, steptrace);
    }

    // Step Down
    m_position.z -= sv_stepsize; 
    // Quake mathematically handles the drop with precision tracing, here we snap down safely
    TraceResult traceDown = m_physics->TraceHull(m_position + glm::vec3(0,0,sv_stepsize), m_position, 1, *m_currentEntities);
    
    if (traceDown.planeNormal.z > 0.7f) {
        m_onGround = true;
        m_position = traceDown.endPos;
    } else {
        // Rejected step (slope too steep)
        m_position = down_o;
        m_velocity = down_v;
    }
}

void Player::TickPhysics(const UserCmd& cmd, const std::vector<RenderEntity>& entities) {
    m_currentEntities = &entities; // <--- Store temporarily for the duration of this tick

    CheckStuck();
    
    if (!m_onGround) {
        m_velocity.z -= (sv_gravity * cmd.msec) * 0.5f; // Quake Euler Gravity
    }

    // 2. Velocity Cap
    for (int i = 0; i < 3; i++) {
        if (m_velocity[i] > 2000.0f) m_velocity[i] = 2000.0f;
        if (m_velocity[i] < -2000.0f) m_velocity[i] = -2000.0f;
    }

    // 3. Ground check 
    TraceResult groundTrace = m_physics->TraceHull(m_position, m_position + glm::vec3(0.0f, 0.0f, -0.25f), 1, *m_currentEntities);
    m_onGround = (groundTrace.fraction < 1.0f && groundTrace.planeNormal.z > 0.7f);

    if (m_onGround && cmd.upmove > 0.0f) {
        m_velocity.z = 270.0f;
        m_onGround = false;
    }

    if (m_onGround) {
        PM_GroundMove(cmd, cmd.msec);
    } else {
        PM_AirMove(cmd, cmd.msec);
    }

    StepSlideMove(cmd.msec);

    // 8. Update Camera (Only update position so we don't overwrite mouse pitch!)
    m_camera->SetPosition(m_position + glm::vec3(0.0f, 0.0f, 22.0f));
    
    m_currentEntities = nullptr; // <--- Clear it to be safe
}

void Player::PM_Friction(float deltaTime) {
    // 1. Quake Friction is strictly 2D!
    float speed = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);
    if (speed < 0.1f) {
        m_velocity.x = 0.0f;
        m_velocity.y = 0.0f;
        return;
    }

    float friction = sv_friction;

    // 2. Edge Friction (Trace 16 units ahead, 34 down)
    glm::vec3 start = m_position + (glm::vec3(m_velocity.x, m_velocity.y, 0.0f) / speed) * 16.0f;
    glm::vec3 stop = start - glm::vec3(0.0f, 0.0f, 34.0f);
    TraceResult edgeTrace = m_physics->TraceHull(start, stop, 1, *m_currentEntities);
    if (edgeTrace.fraction == 1.0f) {
        friction *= sv_edgefriction;
    }

    float control = (speed < sv_stopspeed) ? sv_stopspeed : speed;
    float drop = control * friction * deltaTime;

    float newspeed = speed - drop;
    if (newspeed < 0) newspeed = 0;
    newspeed /= speed; 

    m_velocity.x *= newspeed;
    m_velocity.y *= newspeed;
    // Note: Z velocity is untouched!
}

void Player::PM_Accelerate(glm::vec3 wishdir, float wishspeed, float accel, float deltaTime) {
    float currentspeed = glm::dot(m_velocity, wishdir);
    float addspeed = wishspeed - currentspeed;

    if (addspeed <= 0) return;

    float accelspeed = accel * deltaTime * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;

    m_velocity.x += accelspeed * wishdir.x;
    m_velocity.y += accelspeed * wishdir.y;
    m_velocity.z += accelspeed * wishdir.z;
}

void Player::PM_AirMove(const UserCmd& cmd, float deltaTime) {
    glm::vec3 forwardDir(std::cos(glm::radians(cmd.yaw)), std::sin(glm::radians(cmd.yaw)), 0.0f);
    glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, glm::vec3(0.0f, 0.0f, 1.0f)));

    glm::vec3 wishvel = forwardDir * cmd.forwardmove + rightDir * cmd.sidemove;
    wishvel.z = 0.0f; 

    glm::vec3 wishdir = glm::vec3(0.0f);
    float wishspeed = glm::length(wishvel);
    
    if (wishspeed > 0.0f) wishdir = glm::normalize(wishvel);
    
    if (wishspeed > sv_maxairspeed) wishspeed = sv_maxairspeed;

    PM_Accelerate(wishdir, wishspeed, sv_airaccelerate, deltaTime);
}

void Player::PM_GroundMove(const UserCmd& cmd, float deltaTime) {
    PM_Friction(deltaTime);

    glm::vec3 forwardDir(std::cos(glm::radians(cmd.yaw)), std::sin(glm::radians(cmd.yaw)), 0.0f);
    glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, glm::vec3(0.0f, 0.0f, 1.0f)));

    glm::vec3 wishvel = forwardDir * cmd.forwardmove + rightDir * cmd.sidemove;
    wishvel.z = 0.0f;

    glm::vec3 wishdir = glm::vec3(0.0f);
    float wishspeed = glm::length(wishvel);

    if (wishspeed > 0.0f) wishdir = glm::normalize(wishvel);
    if (wishspeed > sv_maxspeed) wishspeed = sv_maxspeed;

    PM_Accelerate(wishdir, wishspeed, sv_accelerate, deltaTime);
}

} // namespace engine
