#pragma once
#include "raylib.h"

// One instance of a rendered explosion; Vfx owns its update, batching and particles.
struct Explosion {
    bool on = false;
    Vector2 p{};
    float K = 1, prog = 1, ringProg = 1, gain = 1;
};
