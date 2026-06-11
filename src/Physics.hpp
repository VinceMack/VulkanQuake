#pragma once
#include "Map.hpp"
#include <glm/glm.hpp>

namespace engine {

struct TraceResult {
    bool allSolid;   
    bool startSolid; 
    float fraction;  
    glm::vec3 endPos;
    glm::vec3 planeNormal; 
    float planeDist;
    int contents;    
    int rootNode; // Required so the trace can back up out of walls
};

class Physics {
public:
    Physics(const Map* map);
    TraceResult TraceHull(glm::vec3 start, glm::vec3 end, int hull_id);

private:
    // Required to check if a specific point is inside a wall
    int HullPointContents(int nodeIndex, const glm::vec3& p) const;
    
    // Now returns bool (true if empty/clear, false if hit)
    bool RecursiveHullCheck(int nodeIndex, float p1f, float p2f, glm::vec3 p1, glm::vec3 p2, TraceResult& trace) const;

    const Map* m_map;
};

} // namespace engine
