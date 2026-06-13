#include "Physics.hpp"

namespace engine {

Physics::Physics(const Map* map) : m_map(map) {}

TraceResult Physics::TraceHull(glm::vec3 start, glm::vec3 end, int hull_id, const std::vector<RenderEntity>& entities, int32_t ignoreEdict) {
    TraceResult trace;
    trace.allSolid = true;
    trace.startSolid = false;
    trace.fraction = 1.0f;
    trace.contents = bsp::CONTENTS_EMPTY;

    // 1. Trace against Model 0 (The static world)
    const auto& worldModel = m_map->GetBspModel(0);
    trace.rootNode = worldModel.headnode[hull_id];

    RecursiveHullCheck(trace.rootNode, 0.0f, 1.0f, start, end, trace, hull_id);

    // 2. Trace against Entities
    for (const auto& ent : entities) {
        if (ent.edictIndex > 0 && ent.edictIndex == ignoreEdict) continue;
        if (!ent.isSolid) continue; 

        if (ent.type == EntityModelType::BspBrush && ent.modelId != 0) {

        const auto& bspModel = m_map->GetBspModel(ent.modelId);
        int rootNode = bspModel.headnode[hull_id];
        
        // Sometimes the map compiler decides a submodel doesn't need a specific hull size.
        if (rootNode == bsp::CONTENTS_EMPTY) continue;

        // Shift the trace into the entity's local coordinate space
        glm::vec3 localStart = start - ent.origin;
        glm::vec3 localEnd = end - ent.origin;

        TraceResult localTrace;
        localTrace.allSolid = true;
        localTrace.startSolid = false;
        localTrace.fraction = 1.0f; 
        localTrace.contents = bsp::CONTENTS_EMPTY;
        localTrace.rootNode = rootNode;

        RecursiveHullCheck(rootNode, 0.0f, 1.0f, localStart, localEnd, localTrace, hull_id);

        // If we hit this door, and it is CLOSER than the wall we hit earlier, keep it!
        if (localTrace.fraction < trace.fraction || (localTrace.startSolid && !trace.startSolid)) {
            trace.fraction = localTrace.fraction;
            trace.startSolid = localTrace.startSolid;
            trace.allSolid = localTrace.allSolid;
            trace.planeNormal = localTrace.planeNormal;
            // Shift the plane distance back into world space
            trace.planeDist = localTrace.planeDist + glm::dot(localTrace.planeNormal, ent.origin);
            trace.contents = localTrace.contents;
            trace.rootNode = rootNode; 
        }
    } else if (ent.type == EntityModelType::Alias) {
        // [DISABLED] AABB Raycast against Alias Models (Slab Method)
        // It appears monsters are getting instantly blocked by these bounds.
        /*
        // AABB Raycast against Alias Models (Slab Method)
            // Inflate target bounds by our own Hull size to simulate a sweeping box!
            glm::vec3 mins = ent.GetAbsMins();
            glm::vec3 maxs = ent.GetAbsMaxs();
            
            if (hull_id == 1) { // Standard player/monster hull
                mins -= glm::vec3(16, 16, 24);
                maxs += glm::vec3(16, 16, 32);
            }
            
            glm::vec3 dir = end - start;
            float tmin = 0.0f;
            float tmax = 1.0f;
            bool hit = true;
            
            for (int i = 0; i < 3; ++i) {
                if (std::abs(dir[i]) < 0.0001f) {
                    if (start[i] < mins[i] || start[i] > maxs[i]) hit = false;
                } else {
                    float ood = 1.0f / dir[i];
                    float t1 = (mins[i] - start[i]) * ood;
                    float t2 = (maxs[i] - start[i]) * ood;
                    if (t1 > t2) std::swap(t1, t2);
                    if (t1 > tmin) tmin = t1;
                    if (t2 < tmax) tmax = t2;
                    if (tmin > tmax) hit = false;
                }
            }
            
            if (hit && tmin >= 0.0f && tmin < trace.fraction) {
                trace.fraction = tmin;
                trace.startSolid = (tmin == 0.0f);
            }
        */
        }
    }

    // 3. Finalize the world-space end position
    if (trace.fraction == 1.0f) {
        trace.endPos = end;
    } else {
        trace.endPos = start + (end - start) * trace.fraction;
    }

    return trace;
}

int Physics::HullPointContents(int nodeIndex, const glm::vec3& p, int hull_id) const {
    if (hull_id > 0) {
        const auto& clipNodes = m_map->GetClipNodes();
        const auto& planes = m_map->GetPlanes();

        while (nodeIndex >= 0) {
            if (nodeIndex >= static_cast<int>(clipNodes.size())) return bsp::CONTENTS_EMPTY;
            const auto& node = clipNodes[nodeIndex];
            const auto& plane = planes[node.plane_id];
            
            float d;
            // Optimization: Quake axial planes (X, Y, Z) skip the dot product
            if (plane.type < 3) {
                d = p[plane.type] - plane.dist;
            } else {
                d = glm::dot(p, glm::vec3(plane.normal[0], plane.normal[1], plane.normal[2])) - plane.dist;
            }

            if (d < 0) {
                nodeIndex = node.children[1];
            } else {
                nodeIndex = node.children[0];
            }
        }
        return nodeIndex;
    } else {
        const auto& nodes = m_map->GetNodes();
        const auto& planes = m_map->GetPlanes();
        const auto& leaves = m_map->GetLeaves();

        while (nodeIndex >= 0) {
            if (nodeIndex >= static_cast<int>(nodes.size())) return bsp::CONTENTS_EMPTY;
            const auto& node = nodes[nodeIndex];
            const auto& plane = planes[node.plane_id];
            
            float d;
            if (plane.type < 3) {
                d = p[plane.type] - plane.dist;
            } else {
                d = glm::dot(p, glm::vec3(plane.normal[0], plane.normal[1], plane.normal[2])) - plane.dist;
            }

            if (d < 0) {
                nodeIndex = node.children[1];
            } else {
                nodeIndex = node.children[0];
            }
        }
        int leafIndex = ~nodeIndex;
        if (leafIndex >= 0 && leafIndex < static_cast<int>(leaves.size())) {
            return leaves[leafIndex].contents;
        }
        return bsp::CONTENTS_EMPTY;
    }
}

bool Physics::RecursiveHullCheck(int nodeIndex, float p1f, float p2f, glm::vec3 p1, glm::vec3 p2, TraceResult& trace, int hull_id) const {
    // If node is a leaf (negative)
    if (nodeIndex < 0) {
        int contents = bsp::CONTENTS_EMPTY;
        if (hull_id > 0) {
            contents = nodeIndex;
        } else {
            int leafIndex = ~nodeIndex;
            const auto& leaves = m_map->GetLeaves();
            if (leafIndex >= 0 && leafIndex < static_cast<int>(leaves.size())) {
                contents = leaves[leafIndex].contents;
            }
        }

        if (contents != bsp::CONTENTS_SOLID) {
            trace.allSolid = false;
        } else {
            trace.startSolid = true;
        }
        return true; // We hit a leaf, so this sector is clear
    }

    const auto& planes = m_map->GetPlanes();
    uint32_t plane_id;
    int children[2];

    if (hull_id > 0) {
        const auto& clipNodes = m_map->GetClipNodes();
        if (nodeIndex >= static_cast<int>(clipNodes.size())) return true;
        const auto& node = clipNodes[nodeIndex];
        plane_id = node.plane_id;
        children[0] = node.children[0];
        children[1] = node.children[1];
    } else {
        const auto& nodes = m_map->GetNodes();
        if (nodeIndex >= static_cast<int>(nodes.size())) return true;
        const auto& node = nodes[nodeIndex];
        plane_id = node.plane_id;
        children[0] = node.children[0];
        children[1] = node.children[1];
    }

    const auto& plane = planes[plane_id];
    glm::vec3 normal = glm::vec3(plane.normal[0], plane.normal[1], plane.normal[2]);

    float t1, t2;
    if (plane.type < 3) {
        t1 = p1[plane.type] - plane.dist;
        t2 = p2[plane.type] - plane.dist;
    } else {
        t1 = glm::dot(p1, normal) - plane.dist;
        t2 = glm::dot(p2, normal) - plane.dist;
    }

    if (t1 >= 0.0f && t2 >= 0.0f) {
        return RecursiveHullCheck(children[0], p1f, p2f, p1, p2, trace, hull_id);
    }
    if (t1 < 0.0f && t2 < 0.0f) {
        return RecursiveHullCheck(children[1], p1f, p2f, p1, p2, trace, hull_id);
    }

    // Put the crosspoint DIST_EPSILON pixels on the near side
    float DIST_EPSILON = 0.03125f; // 1/32nd
    int side;
    float frac;

    if (t1 < 0.0f) {
        frac = (t1 + DIST_EPSILON) / (t1 - t2);
        side = 1;
    } else {
        frac = (t1 - DIST_EPSILON) / (t1 - t2);
        side = 0;
    }

    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    float midf = p1f + (p2f - p1f) * frac;
    glm::vec3 mid = p1 + (p2 - p1) * frac;

    // Move up to the node on the near side
    if (!RecursiveHullCheck(children[side], p1f, midf, p1, mid, trace, hull_id)) {
        return false;
    }

    // Check if the far side is actually solid
    if (HullPointContents(children[side ^ 1], mid, hull_id) != bsp::CONTENTS_SOLID) {
        // Go past the node, we didn't hit a wall!
        return RecursiveHullCheck(children[side ^ 1], midf, p2f, mid, p2, trace, hull_id);
    }

    if (trace.allSolid) {
        return false; // Never got out of the solid area
    }

    // ==========================================================
    // The other side of the node IS solid, this is the IMPACT POINT
    // ==========================================================
    if (side == 0) {
        trace.planeNormal = normal;
        trace.planeDist = plane.dist;
    } else {
        trace.planeNormal = -normal;
        trace.planeDist = -plane.dist;
    }

    // Back up if floating point precision pushed us into the wall
    while (HullPointContents(trace.rootNode, mid, hull_id) == bsp::CONTENTS_SOLID) {
        frac -= 0.1f;
        if (frac < 0.0f) {
            trace.fraction = midf;
            trace.endPos = mid;
            return false;
        }
        midf = p1f + (p2f - p1f) * frac;
        mid = p1 + (p2 - p1) * frac;
    }

    trace.fraction = midf;
    trace.endPos = mid;
    return false;
}

} // namespace engine
