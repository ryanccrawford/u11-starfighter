#pragma once
#include "raylib.h"
#include "corridor.hpp"
#include <array>

inline constexpr int kTurretBarrelCount = 3;
inline constexpr float kTurretBarrelAngularSpeed = 8.f;
inline constexpr float kTurretBarrelFireInterval = 2.f * PI /
    (kTurretBarrelAngularSpeed * static_cast<float>(kTurretBarrelCount));

// Gameplay state is independent of the model and can later be reused with another asset.
struct Turret {
    bool alive = false, spawned = false;
    int hp = 24;
    float fireCooldown = 0.f, recoil = 0.f, barrelAngle = 0.f, wreckTime = 0.f, sparkCooldown = 0.f;
    float muzzleFlash = 0.f;
    int firingBarrel = 0, flashBarrel = 0;
    float mountStartX = 0.f, corridorScrollAtMount = 0.f;
    float age = 0.f;  // seconds since mounting; ships it appears on are moved out rather than crashed
    Vector2 pos{}, aim{1.f, 0.f};
    float mountedX(float corridorScrollTime, float pixelsPerWorldUnit) const {
        return mountStartX - (corridorScrollTime - corridorScrollAtMount) *
                             Corridor::kWorldUnitsPerScrollSecond * pixelsPerWorldUnit;
    }
};
