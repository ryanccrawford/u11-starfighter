#pragma once
#include "world_position.hpp"

struct WorldVelocity { float x = 0.f, y = 0.f, z = 0.f; };
struct WorldBox { WorldPosition min{}, max{}; };

// View-independent movement and spherical collision for world-space entities.
class WorldPhysics {
public:
    static WorldPosition advance(WorldPosition position, WorldVelocity velocity, float dt);
    static bool overlaps(WorldPosition a, float radiusA, WorldPosition b, float radiusB);
    // Separates an intersecting pair along the line between their centers; a fixed normal
    // handles coincident centers. Returns false when there is no penetration.
    static bool resolveOverlap(WorldPosition& a, float radiusA, WorldPosition& b, float radiusB);
    static bool overlapsBox(WorldPosition center, float radius, const WorldBox& box);
};
