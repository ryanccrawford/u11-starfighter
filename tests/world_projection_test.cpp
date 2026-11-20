#include "../world_position.hpp"
#include <cassert>

int main() {
    const WorldPosition position{12.f, 7.f, 3.f};
    const Vector2 side = projectWorld(position, WorldProjection::Side);
    const Vector2 top = projectWorld(position, WorldProjection::Top);
    assert(side.x == 12.f && side.y == -3.f);
    assert(top.x == 12.f && top.y == 7.f);
    const WorldPosition further{12.f, -20.f, 3.f};
    assert(projectWorld(further, WorldProjection::Side).y == side.y);
    const WorldPosition higher{12.f, 7.f, 90.f};
    assert(projectWorld(higher, WorldProjection::Top).y == top.y);
    assert(position.x == 12.f && position.y == 7.f && position.z == 3.f);
    const Vector2 legacy{23.f, 42.f};
    const Vector2 sideRoundTrip = projectWorld(worldFromLegacy(legacy, WorldProjection::Side, 9.f), WorldProjection::Side);
    const Vector2 topRoundTrip = projectWorld(worldFromLegacy(legacy, WorldProjection::Top, 9.f), WorldProjection::Top);
    assert(sideRoundTrip.x == legacy.x && sideRoundTrip.y == legacy.y);
    assert(topRoundTrip.x == legacy.x && topRoundTrip.y == legacy.y);
}
