#pragma once
#include "raylib.h"
#include "combat_types.hpp"
#include "world_position.hpp"

// Common physical and combat state for player and enemy spacecraft. Individual controllers,
// weapons, and death effects remain separate systems so either side can reuse them.
struct Ship {
    Ship() = default;
    explicit Ship(int health) : hp(health), maxHp(health) {}
    Vector2 pos{}, vel{};
    float hiddenAxis = 0.f;
    WorldPosition worldPosition(WorldProjection projection) const { return worldFromLegacy(pos, projection, hiddenAxis); }
    int hp = 1, maxHp = 1;
    float flash = 0;
    int row = 0;
};
