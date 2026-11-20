#include "../world_physics.hpp"
#include <cassert>

int main() {
    const WorldPosition moved = WorldPhysics::advance({1, 2, 3}, {2, -4, 6}, .5f);
    assert(moved.x == 2 && moved.y == 0 && moved.z == 6);
    assert(WorldPhysics::overlaps({0, 0, 0}, 1, {0, 0, 2}, 1));
    assert(!WorldPhysics::overlaps({0, 0, 0}, 1, {0, 0, 3}, 1));
    // Projectiles may hit in side view even when physical ships are separated in depth.
    const WorldPosition distant{0, 3, 0};
    assert(!WorldPhysics::overlaps({0, 0, 0}, 1, distant, 1));
    assert(projectWorld(distant, WorldProjection::Side).y == 0);
    WorldPosition a{0, 0, 0}, b{0, 1, 0};
    assert(WorldPhysics::resolveOverlap(a, 1, b, 1));
    assert(a.y == -.5f && b.y == 1.5f);
    assert(!WorldPhysics::resolveOverlap(a, 1, b, 1));
    WorldPosition c{}, d{};
    assert(WorldPhysics::resolveOverlap(c, 1, d, 1));
    assert(c.x == 1 && d.x == -1);
    const WorldBox door{{0, -2, -3}, {1, 2, 3}};
    assert(WorldPhysics::overlapsBox({-.5f, 0, 0}, .5f, door));
    assert(!WorldPhysics::overlapsBox({-1.f, 0, 0}, .5f, door));
    assert(!WorldPhysics::overlapsBox({.5f, 4, 0}, .5f, door));
}
