#include "../hit_feedback.hpp"
#include <cassert>
#include <cstdio>

int main() {
    // Every hit damages: consecutive hits in consecutive frames each take 1 hp (no i-frames).
    int hp = 6; float flash = 0, since = -1, alpha = 0;
    for (int k = 1; k <= 5; k++) { assert(!applyEnemyHit(hp, flash, since, 1)); assert(hp == 6 - k); stepHitBar(since, alpha, 1 / 60.f); }
    assert(applyEnemyHit(hp, flash, since, 1) && hp == 0);
    // Health bar: fades in on a hit, visible through the hold, gone ~1.4 s after the last hit.
    since = -1; alpha = 0;
    for (int i = 0; i < 60; i++) stepHitBar(since, alpha, 1 / 60.f);
    assert(alpha == 0.f);                                   // never hit: hidden
    hp = 6; applyEnemyHit(hp, flash, since, 1);
    float t = 0;
    for (; t < 0.12f; t += 1 / 60.f) stepHitBar(since, alpha, 1 / 60.f);
    assert(alpha > 0.99f);
    for (; t < 0.95f; t += 1 / 60.f) stepHitBar(since, alpha, 1 / 60.f);
    assert(alpha > 0.99f);                                  // held while recently hit
    for (; t < 1.25f; t += 1 / 60.f) stepHitBar(since, alpha, 1 / 60.f);
    assert(alpha > 0.f && alpha < 1.f);                     // fading
    for (; t < 1.5f; t += 1 / 60.f) stepHitBar(since, alpha, 1 / 60.f);
    assert(alpha == 0.f);                                   // gone by 1.5 s
    // Hit sounds: every hit at the 10/s fire rate plays and stays audible; 60/s is capped near 24/s.
    HitSoundGate g; int played = 0; float vmin = 1;
    for (int i = 0; i < 50; i++) { float v; if (g.allow(i * 0.1, 0.8f, v)) { played++; vmin = v < vmin ? v : vmin; } }
    assert(played == 50 && vmin >= 0.8f * HitSoundGate::kMinMix - 1e-4f);
    HitSoundGate g2; played = 0;
    for (int i = 0; i < 120; i++) { float v; if (g2.allow(i / 60.0, 0.8f, v)) played++; }
    assert(played >= 40 && played <= 62);                   // 2 s at 60 hits/s
    std::printf("enemy_hit_test OK (10/s: 50/50 played, min vol %.2f; 60/s: %d/120 played)\n", vmin, played);
}
