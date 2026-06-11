#include "Entity.hpp"
#include <sstream>
#include <cstdio>

namespace engine {

void Entity::AddProperty(const std::string& key, const std::string& value) {
    m_properties[key] = value;
}

std::string Entity::GetString(const std::string& key, const std::string& fallback) const {
    auto it = m_properties.find(key);
    if (it != m_properties.end()) {
        return it->second;
    }
    return fallback;
}

float Entity::GetFloat(const std::string& key, float fallback) const {
    std::string val = GetString(key);
    if (val.empty()) return fallback;

    try {
        return std::stof(val);
    } catch (...) {
        return fallback; // Safe catch for non-numeric data
    }
}

glm::vec3 Entity::GetVector(const std::string& key, const glm::vec3& fallback) const {
    std::string val = GetString(key);
    if (val.empty()) return fallback;

    glm::vec3 result = fallback;
    // sscanf is incredibly robust and fast for this exact format: "x y z"
    // We expect up to 3 floats. If only 1 or 2 are present, sscanf handles it cleanly.
    if (sscanf(val.c_str(), "%f %f %f", &result.x, &result.y, &result.z) < 3) {
        // Optionally, could log a warning if less than 3 components were parsed.
    }
    return result;
}

} // namespace engine