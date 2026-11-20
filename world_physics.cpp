#include "world_physics.hpp"
#include <algorithm>
#include <cmath>

WorldPosition WorldPhysics::advance(WorldPosition p, WorldVelocity v, float dt) {
    const float step = std::max(0.f, dt);
    return {p.x + v.x * step, p.y + v.y * step, p.z + v.z * step};
}

bool WorldPhysics::overlaps(WorldPosition a, float radiusA, WorldPosition b, float radiusB) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    const float radius = std::max(0.f, radiusA) + std::max(0.f, radiusB);
    return x*x + y*y + z*z <= radius*radius;
}

bool WorldPhysics::overlapsBox(WorldPosition center, float radius, const WorldBox& box) {
    const float x = center.x - std::clamp(center.x, box.min.x, box.max.x);
    const float y = center.y - std::clamp(center.y, box.min.y, box.max.y);
    const float z = center.z - std::clamp(center.z, box.min.z, box.max.z);
    const float r = std::max(0.f, radius);
    return x*x + y*y + z*z <= r*r;
}

bool WorldPhysics::resolveOverlap(WorldPosition& a, float radiusA, WorldPosition& b, float radiusB) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    const float distanceSquared = dx*dx + dy*dy + dz*dz;
    const float radius = std::max(0.f, radiusA) + std::max(0.f, radiusB);
    if (distanceSquared >= radius * radius || radius <= 0.f) return false;
    const float distance = std::sqrt(distanceSquared);
    const float amount = (radius - distance) * .5f;
    const float nx = distance > 1e-6f ? dx / distance : 1.f;
    const float ny = distance > 1e-6f ? dy / distance : 0.f;
    const float nz = distance > 1e-6f ? dz / distance : 0.f;
    a.x += nx * amount; a.y += ny * amount; a.z += nz * amount;
    b.x -= nx * amount; b.y -= ny * amount; b.z -= nz * amount;
    return true;
}
