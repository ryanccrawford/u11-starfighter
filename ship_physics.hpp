#pragma once
#include "enemies.hpp"

// Shared ship movement helpers, separate from enemy spawning, weapons, and rendering.
class ShipPhysics {
public:
    static void separateEnemies(Pool<Enemy, 160>& ships, float dt, float scale, WorldProjection projection);
};
