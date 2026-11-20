#pragma once
#include "entities.hpp"

// Pooled particle effects. Three layers so each draws as a single batch:
//   soft  - additive, glow texture (fireballs, flashes, missile glow)
//   hard  - additive, solid quads  (sparks, debris, glitter)
//   smoke - alpha blend, glow texture tinted dark
struct Particle {
    bool alive = false;
    Vector2 pos{}, vel{};
    float life = 0, maxLife = 1, size = 3, grow = 0, drag = 0.96f;
    Color col = WHITE;
    bool twinkle = false;
};

struct FloatText { bool alive = false; Vector2 pos{}; float life = 0; float scale = 1.f; char text[24]{}; Color col = WHITE; };

class Particles {
public:
    void init();       // creates the shared radial glow texture
    void shutdown();
    void update(float dt);
    void clear();

    void explosion(Vector2 p, float scale, Color tint);   // flash + fireball + debris + smoke ring
    void pickupVaporize(Vector2 p, float scale, Color tint);  // compact colored energy dispersion
    void sparks(Vector2 p, Vector2 dir, int n, Color c, float speed, float sizeScale = 1.f);
    void embers(Vector2 p, int n, float size, float speed);  // short orange drips from a hot hull impact
    void smoke(Vector2 p, float size);
    void turretSmoke(Vector2 p, float size);
    void dust(Vector2 p, Vector2 vel, float size, float life, Color c);   // ground dust kicked up by an engine blast
    void glitter(Vector2 p, Vector2 vel, float size, float life);
    void text(Vector2 p, const char* t, Color c, float sizeScale = 1.f);

    void drawAdditive() const;  // call inside BLEND_ADDITIVE
    void drawSmoke() const;     // call with normal alpha blending
    void drawText(float s) const;
    std::size_t count() const { return soft_.count() + hard_.count() + smoke_.count(); }
    const Texture2D& glowTexture() const { return glow_; }
    const Texture2D& flameTexture() const { return flame_; }  // kFlameFrames rows, flame points +x, base at x=0
    void flameSpark(Vector2 p, Vector2 vel, float size, float life);  // white/lavender tail spark
    const Texture2D& dotTexture() const { return dot_; }  // solid anti-aliased disc: bullets as 1 quad instead of a 36-tri fan

private:
    Particle* add(Pool<Particle, 3000>& pool, Vector2 p, Vector2 v, float life, float size, Color c);
    Pool<Particle, 3000> soft_, hard_, smoke_;
    Pool<FloatText, 48> texts_;
    Texture2D flame_{}, dot_{}, glow_{};
};
