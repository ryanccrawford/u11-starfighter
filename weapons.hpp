#pragma once
#include "entities.hpp"
#include "enemies.hpp"
#include "view.hpp"
#include "projectile.hpp"
#include <array>
#include <cstdint>

class Particles;

// Precomputed laser-bolt body (makeLaserBolt shader math). Points +x; nose at u = 0.96. enemy = warm palette.
const Texture2D& boltTexture(bool enemy);
class Audio;

struct BoltSpark { bool alive = false; Vector2 pos{}, vel{}; float life = 1, dur = 0.2f, size = 2; };  // screen space
// Flamethrower particle (local space): a fire blob that swells, cools white -> orange -> red, then fades.
struct FlameBit { bool alive = false; Vector2 pos{}, vel{}; float age = 0, life = 0.45f, size = 10, spin = 0; int rayIndex = 0; };

// Primary weapons the player can own and cycle through (bit i of the owned mask = weapon i).
enum PrimaryWeapon { kPrimaryLaser = 0, kPrimaryFlame = 1, kPrimaryPlasma = 2, kPrimaryCount = 3 };
const char* primaryName(int w);

// Player weapons in local space: requested primary volleys + player-triggered homing missiles.
class Weapons {
public:
    void reset();
    void update(float dt, Vector2 ship, Pool<Enemy, 160>& enemies, const View& v, Particles& fx, Audio& sfx,
                bool primaryFire, bool secondaryFire, float aim = 0,
                const ProjectileTarget* extraTarget = nullptr); // aim: local angle (rad) of the nose
    void draw(const View& v, const Texture2D& glow) const;  // call inside BLEND_ADDITIVE
    void drawFlameSmoke(const View& v, const Texture2D& glow) const; // call before the additive weapon pass
    void drawBeamImpact(const View& v, const Texture2D& glow) const; // call after hulls are composited

    Pool<Laser, 512> lasers;
    Pool<Missile, 48> missiles;
    Pool<BoltSpark, 1024> sparks;
    int level = 1, ammo = 0;
    float rapid = 0;  // seconds of rapid-fire left
    float heat = 0;   // 0..1 primary weapon heat
    float overheat = 0; // cooldown lockout seconds
    bool flameBeam = false; // flamethrower roar on (set by update while the flamer fires)

    // ---- selectable primaries (t219). `primary` survives reset(): ownership is a save-game property.
    int primary = kPrimaryLaser;
    // Plasma lance: pulsed beam, heat builds while firing, forced cooldown at 100 %.
    float plasmaHeat = 0, plasmaLock = 0;      // 0..1 heat, seconds of forced cooldown left
    bool beamOn = false;                       // firing this frame (Game resolves the hit and beamLen)
    Vector2 beamFrom{}, beamDir{1, 0};         // local space muzzle + unit direction
    float beamMax = 0, beamLen = 0, beamAcc = 0, beamHitT = 0;   // reach, resolved length, damage accumulator, impact flash
    float beamDepthScale = 1.f;                 // projected scale at the current hull contact
    Vector2 beamHitPos{};                       // local contact point for the post-hull impact glow
    bool beamHit = false;
    // Flamethrower: short cone, damage over time to everything inside it.
    bool flameOn = false;
    Vector2 flameFrom{}, flameDir{1, 0};
    float flameRange = 0, flameTick = 0;
    static constexpr int kFlameRayCount = 6;
    std::array<float, kFlameRayCount> flameStops{}; // nearest contact distance per deterministic flame ray
    Pool<FlameBit, 512> flames;
    static constexpr float kPlasmaDps = 26.f, kPlasmaHeatRate = 0.30f, kPlasmaCoolRate = 0.40f, kPlasmaLockTime = 1.5f;
    static constexpr float kPlasmaPulseOn = 1.5f, kPlasmaPulseOff = 0.6f;
    static constexpr float kFlameTickRate = 16.f, kFlameHalfAngle = 0.30f, kFlameRangeK = 0.36f;   // 1 dmg per tick per target

private:
    float fireT_ = 0, missileT_ = 0, sparkDebt_ = 0, flameDebt_ = 0, plasmaCycleT_ = 0;
};
