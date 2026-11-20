#include "../ship_physics.hpp"
#include "../camera_system.hpp"
#include <cassert>
#include <cmath>

int main() {
    Pool<Enemy, 160> ships;
    for (int i = 0; i < 3; ++i) { Enemy* e = ships.spawn(); assert(e); e->r = 10; e->pos = {float(i * 4), 0}; }
    ShipPhysics::separateEnemies(ships, .016f, 1.f, WorldProjection::Side);
    for (std::size_t i = 0; i < ships.n_; ++i) for (std::size_t j = i + 1; j < ships.n_; ++j) {
        const WorldPosition a = ships.items[i].worldPosition(WorldProjection::Side);
        const WorldPosition b = ships.items[j].worldPosition(WorldProjection::Side);
        const float x = a.x-b.x, y = a.y-b.y, z = a.z-b.z;
        assert(std::sqrt(x*x+y*y+z*z) > 19.99f);
    }
    Pool<Enemy, 160> depthShips;
    Enemy* a = depthShips.spawn(); Enemy* b = depthShips.spawn();
    a->r = b->r = 10; a->hiddenAxis = -20; b->hiddenAxis = 20;
    ShipPhysics::separateEnemies(depthShips, .016f, 1.f, WorldProjection::Side);
    assert(a->pos.x == 0 && b->pos.x == 0 && a->hiddenAxis == -20 && b->hiddenAxis == 20);
    View view; view.pf = {0, 0, 1400, 700};
    const float nearScale = CameraSystem::depthScale(view, 100.f);
    const float farScale = CameraSystem::depthScale(view, -100.f);
    assert(nearScale > farScale);
    assert(a->r == 10 && b->r == 10); // different projected sizes do not resize physical hulls
}
