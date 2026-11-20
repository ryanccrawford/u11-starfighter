#include "../projectile.hpp"
#include <cassert>
#include <cmath>

int main() {
    const Vector2 start{0, 0}, end{100, 0};
    const float nearHit = ProjectileTargeting::firstContact(start, end, {30, 0}, 5);
    const float farHit = ProjectileTargeting::firstContact(start, end, {70, 0}, 5);
    assert(std::fabs(nearHit - .25f) < .0001f && nearHit < farHit);
    assert(ProjectileTargeting::firstContact(start, end, {50, 10}, 5) < 0);
    assert(ProjectileTargeting::firstContact({30, 0}, {30, 0}, {30, 0}, 5) == 0);
    assert(ProjectileTargeting::firstContact(start, start, {30, 0}, 5) < 0);
    const ProjectileTarget enemy{{}, Faction::Enemy, true};
    assert(ProjectileTargeting::canTarget(Faction::Player, enemy));
    assert(!ProjectileTargeting::canTarget(Faction::Enemy, enemy));
    assert(!ProjectileTargeting::canTarget(Faction::Neutral, enemy));
    const WorldPosition worldStart{0, 0, 0}, worldEnd{100, 0, 0}, offset{30, 50, 0};
    assert(std::fabs(ProjectileTargeting::firstProjectedContact(worldStart, worldEnd, offset, 5, WorldProjection::Side) - .25f) < .0001f);
    assert(ProjectileTargeting::firstProjectedContact(worldStart, worldEnd, offset, 5, WorldProjection::Top) < 0);
    assert(std::fabs(ProjectileTargeting::firstCapsuleContact({0,0},{100,0},{30,-10},{30,10},5) - .25f) < .0001f);
    assert(ProjectileTargeting::firstCapsuleContact({0,30},{100,30},{30,-10},{30,10},5) < 0);
    assert(ProjectileTargeting::firstCapsuleContact({30,0},{30,0},{30,-10},{30,10},5) == 0.f);
}
