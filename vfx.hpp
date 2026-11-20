#pragma once
#include "raylib.h"
#include "explosion.hpp"
#include <functional>
#include <vector>

// Ryan's Three.js procedural effects, ported to raylib (screen space, additive, batched):
//   enemy explosion  - assets/fx/enemy_explosion.json (blast core + shockwave ring + embers + smoke)
//   impact damage    - assets/fx/impact_damage.json  (plasma flash + scorch/ring + directional sparks)
// Shader quads carry their animation state in the vertex colour (r/g = progress), so every layer
// draws as two batches: one uber-shader for all effect quads, then all point sprites (one atlas, CPU-tinted).
class Vfx {
public:
    void init(const char* assetsDir);
    void shutdown();
    void clear();
    void update(float dt, float s);
    void draw() const;  // screen space, after the 3D ships

    // shipPx = on-screen ship size; pos in screen px
    void explode(Vector2 pos, float shipPx, float gain = 1);  // gain 0..1 dims blast/ring + fewer points
    // hit: screen pos, direction the shot was travelling (screen), ship it sticks to (followed via *anchor)
    // hitLocal / *anchor are gameplay-local coords (mapped through toScreen every frame so the impact rides the ship)
    void impact(Vector2 hitLocal, Vector2 shotDirScreen, float shipPx, const Vector2* anchor, const void* owner, bool warm);
    std::function<Vector2(Vector2)> toScreen = [](Vector2 v) { return v; };
    void detach(const void* owner);
    void follow();  // re-pin impacts to their ships without advancing time (frozen test captures)  // ship died: drop its impacts (the explosion replaces them)
    int liveSparks() const { return nSparks_; }
    // breakup: tiny spinning hull fragments (alpha-blended triangles, one ember-hot vertex), sprayed along dir
    struct ShardCfg { int count; float sizeMin, sizeMax, speedMin, speedMax, drag, life; int maxShards; };
    void shards(Vector2 pos, Vector2 dir, Color hull, const ShardCfg& c);

    struct Col { float r, g, b; };
private:
    struct Imp { bool on = false; Vector2 off{}, p{}; const Vector2* anchor = nullptr; const void* owner = nullptr;
                 float K = 1, prog = 1, ringProg = 1, scorchProg = 1; bool warm = false; };
    struct Pt { Vector2 p, v; float life, dur, size, drag; unsigned char kind; };  // kind 0 ember 1 smoke 2 spark 3 warm spark

    Shader fx_{};
    Texture2D atlas_{}, white_{};
    bool ok_ = false;
    Explosion ex_[8];
    Imp im_[16];
    std::vector<Pt> pts_;
    struct Shard { Vector2 p, v; float ang, spin, size, life, dur, drag; Color hull; float shape[3]; };
    std::vector<Shard> shards_;
    int nSparks_ = 0;
    float s_ = 1;
    // settings
    struct { float blastRadius, duration, blastGain, turbulence, ringRadius, ringThickness, ringGain, ringSpeed;
             int debrisCount; float dSpMin, dSpMax, dSzMin, dSzMax, dLifeMin, dLifeMax, dDrag, debrisGain;
             int smokeCount; float smokeSize, smokeOpacity, smokeLife, shipMul, pointScale;
             Col core, fire, outer, ring, ember, hot, smoke; } E_{};
    struct { float flashRadius, flashDuration, flashGain, sharp; int sparkCount; float spMin, spMax, spread, angle,
             szMin, szMax, lifeMin, lifeMax, sparkGain, drag, scorchDuration, scorchGain, ringRadius, ringThickness, ringSpeed,
             shipMul, pointScale; int maxSparks; Col flash, plasma, spark, hot, scorch, ring; } I_{};
};
