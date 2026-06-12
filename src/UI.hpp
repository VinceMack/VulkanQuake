#pragma once
#include <glm/glm.hpp>

namespace engine {

struct UIVertex {
    glm::vec2 position; // Screen pixel coordinates (X, Y)
    glm::vec2 uv;       // Font Atlas texture coordinates
};

} // namespace engine
