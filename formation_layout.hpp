#pragma once
#include <algorithm>
#include <cmath>
// Invader grid layout (pure math, unit-tested in tests/formation_layout_test.cpp).
// The grid enters from beyond the far edge and settles where EVERY cell is fully inside the
// playfield; neighbouring cells are spaced so their projectile hit circles never touch.
struct FormationLayout {
    int rows = 1, cols = 1;
    float spacing = 0;          // centre-to-centre on both axes
    float extentX = 0, extentY = 0;
    float settleX = 0;          // forward coordinate of the front rank once formed up
    float originY = 0;          // lateral coordinate of column 0
    float edge = 0;             // lateral bounce margin used by the march (Enemies::update)
};

// hitRadius: the largest projected projectile contact radius of one cell (ship radius at the
// nearest rank's depth + the projectile pad).
inline FormationLayout layoutFormation(int count, float authoredSpacing, float hitRadius,
                                       float F, float Lat, float laneY, float s) {
    FormationLayout o;
    o.cols = std::max(1, std::min(count, 8));
    o.rows = (std::max(1, count) + o.cols - 1) / o.cols;
    o.edge = 30.f * s;
    const float minSp = 2.f * hitRadius + 6.f * s;                       // hit circles stay apart
    float sp = std::max(authoredSpacing, minSp);
    if (o.cols > 1) sp = std::max(minSp, std::min(sp, (Lat - 2.f * (o.edge + hitRadius)) / (o.cols - 1)));
    o.spacing = sp;
    o.extentX = (o.rows - 1) * sp;
    o.extentY = (o.cols - 1) * sp;
    const float back = 40.f * s + hitRadius;                             // back rank's far side stays inside
    o.settleX = std::max(F * 0.35f, F - back - o.extentX);
    const float lo = o.edge + hitRadius, hi = Lat - o.edge - hitRadius - o.extentY;
    o.originY = hi >= lo ? std::clamp(Lat * laneY - o.extentY * 0.5f, lo, hi) : (Lat - o.extentY) * 0.5f;
    return o;
}

// Entry glide: full speed while far out, easing into the settle line (never stops at the edge).
inline float formationEntryStep(float x, float goal, float maxSpeed, float dt) {
    if (x <= goal) return goal;
    const float v = std::min(maxSpeed, 2.5f * (x - goal) + 4.f);
    return std::max(goal, x - v * dt);
}
