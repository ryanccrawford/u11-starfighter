#pragma once
#include <algorithm>
#include <cmath>
// Enemy hit feedback (pure logic, unit-tested in tests/enemy_hit_test.cpp).

// Every projectile hit deals its damage: there are no hit i-frames. Returns true when it kills.
inline bool applyEnemyHit(int& hp, float& flash, float& barSince, int dmg) {
    hp -= dmg; flash = 0.06f; barSince = 0.f;
    return hp <= 0;
}

// Faint health bar under a damaged enemy: fades in on a hit, holds while hits keep landing, and
// fades out over kBarFade after kBarHold seconds without one (gone ~1.4 s after the last hit).
constexpr float kBarHold = 1.0f, kBarFadeIn = 0.08f, kBarFade = 0.4f;
inline void stepHitBar(float& barSince, float& barAlpha, float dt) {
    if (barSince >= 0.f) barSince += dt;
    const bool want = barSince >= 0.f && barSince < kBarHold;
    barAlpha = want ? std::min(1.f, barAlpha + dt / kBarFadeIn) : std::max(0.f, barAlpha - dt / kBarFade);
}

// Hit-sound limiter: at most one hit sound per kMinGap seconds (so dense volleys don't smear into
// noise), every slower repeat is played, and the level ducks gently with density but never below
// kMinMix of the base so repeat hits stay audible.
struct HitSoundGate {
    static constexpr double kMinGap = 1.0 / 24.0;
    static constexpr float kMinMix = 0.55f;
    double last = -1e9;
    float density = 0.f;  // decaying count of recent hit sounds
    bool allow(double now, float base, float& volume) {
        if (now - last < kMinGap) return false;
        density = density * (float)std::exp(-(now - last) / 0.35) + 1.f;
        last = now;
        volume = base * std::max(kMinMix, 1.f / std::sqrt(0.6f + 0.4f * density));
        return true;
    }
};
