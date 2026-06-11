#pragma once
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

namespace engine {

class Entity {
public:
    void AddProperty(const std::string& key, const std::string& value);

    // Safe getters
    std::string GetString(const std::string& key, const std::string& fallback = "") const;
    float       GetFloat(const std::string& key, float fallback = 0.0f) const;
    glm::vec3   GetVector(const std::string& key, const glm::vec3& fallback = glm::vec3(0.0f)) const;

    std::string GetClassname() const { return GetString("classname"); }

private:
    std::unordered_map<std::string, std::string> m_properties;
};

} // namespace engine