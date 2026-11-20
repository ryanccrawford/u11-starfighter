#pragma once
#include "data.hpp"
#include "entities.hpp"
#include "view.hpp"
#include "ship.hpp"
#include "projectile.hpp"
#include <functional>
#include <vector>

class Particles;
class Audio;

enum EnemySurfaceFxChannel : int { kSurfacePlasmaHeat = 0, kSurfaceFlameHeat = 1, kSurfaceChar = 2, kSurfaceCharSpread = 3 };

struct Enemy : Ship {
    bool alive = false;
    const EnemyDef* def = nullptr;
    Vector2 origin{};
    float t = 0, fireT = 1, r = 20, amp = 60, freq = 2;
    // r is the physical radius in gameplay units. Perspective affects only the projected
    // projectile hit radius, never the physical size used for ship-to-ship contact.
    int phase = 0;      // boss phase index
    float minionT = 0, spin = 0;
    int group = -1;     // invaders formation index
    Vector2 gridOff{};  // offset within the formation
    float dent[8][4]{};  // hull dents: model-space centre xyz + depth (Render3D dent shader)
    float surfaceFx[8][4]{};  // per-dent plasma heat, flame scorch, char and char spread
    int nDent = 0, dentNext = 0;
    float shieldContact = 0;  // prevents one sustained touch from dealing damage every frame
    float spawnAge = 0;       // seconds alive; a ship found inside a solid this young is moved out, not crashed
    Vector2 avoidance{};      // eased formation separation, retained across scripted path updates
    const std::vector<Vector2>* path = nullptr;  // wave-authored Pattern::Path override (else def->path)
    float fireAnimT = -1;     // seconds since the last shot (drives the model's Fire clip); < 0 = never fired
    int burstLeft = 0;        // rotary: rounds still to fire in the current burst
    float burstT = 0;         // rotary: seconds until the next round
    float barSince = -1;      // seconds since the last damaging hit (< 0 = never hit); drives the faint health bar
    float barAlpha = 0;
    float leaveT = -1, leaveOff = 0, leaveLat = 0;  // L6 lull: disengage (seconds since ordered, pushes)       // health-bar opacity 0..1 (hit_feedback.hpp: fade in on hit, out ~1.4 s after the last)
};

// Space-Invaders style marching grid: moves laterally, steps forward at the
// edges and speeds up as members die.
// It enters from beyond the far edge and glides (no stop at the edge) to settleX, where every cell
// is inside the playfield; only then does it start stepping toward the player.
struct Formation { bool active = false; Vector2 origin{}; float dir = 1, speed = 60, step = 26; int initial = 1; Vector2 extent{};
                   bool entering = false; float settleX = 0, entrySpeed = 0, spacing = 0; };

class Enemies {
public:
    void reset();
    void spawnWave(const SpawnWave& w, const EnemyDef& d, const LevelDef& L, const View& v);
    void spawnBoss(const EnemyDef& d, const LevelDef& L, const View& v);
    void update(float dt, Vector2 player, const LevelDef& L, const View& v, const Database& db, Audio& sfx);
    std::size_t alive() const { return list.count(); }
    const Enemy* boss() const;

    Pool<Enemy, 160> list;
    Pool<EnemyShot, 1024> shots;
    // Optional: where an enemy's weapon muzzle is in playfield-local space (Game wires this to the
    // 3D model's sidecar muzzle). Unset = shots leave the ship's centre, as before.
    std::function<Vector2(const Enemy&)> muzzleOf;
    float scrollMul = 1.f;  // edge-throttle world speed: scales the forward travel of straight/sine/dive/circle movers
    // Spawn guards wired by Game. spawnBlocked: a new ship of radius r at (pos, depth) would sit in a
    // level solid (L3 door/turret); the wave is moved further out until it is clear. laneMin: lowest
    // lateral coordinate any part of a new ship's sweep may use (keeps lanes out of the turret).
    std::function<bool(Vector2 pos, float hidden, float r)> spawnBlocked;
    float laneMin = 0.f;
    bool retreat = false;  // L6 no-combat stretch: everyone stops firing and flies out past the player's edge

    // ---- t251: The Warden (first boss) director: smooth spring motion, telegraphed attack cycles,
    // helper waves while it backs off behind its shield, escalation per return, and a geometric rest zone.
    enum class WardenState { Off, Engage, Retreat, Helpers, Return };
    struct Warden {
        WardenState st = WardenState::Off;
        int phase = 0;                 // 0..3 engage phase (escalates after each helper wave)
        float stT = 0;                 // seconds in the current state
        Vector2 p{}, v{};              // smooth position / velocity (critically damped spring, local px)
        Vector2 tgtS{}; bool tgtInit = false;   // eased station target (so even the spring's acceleration has no steps)
        float depth = 0, depthV = 0;   // hidden axis, same spring
        int station = 0;               // index in the phase's fixed station cycle
        int seqI = 0;                  // index in the phase's fixed attack cycle
        int atkState = 0;              // 0 cooldown, 1 telegraph, 2 firing
        float atkT = 0;                // seconds left in cooldown / telegraph, or seconds into firing
        int atkMask = 0;               // attack kinds of the current step
        int firedA = 0, firedS = 0, firedR = 0;   // rounds already fired in this step (aimed / sweep / rockets)
        Vector2 aimAt{};               // aimed bursts: the player's position when the telegraph completed
        float spin = 0;                // ring pattern rotation
        float shield = 0;              // 0..1 shield (no damage while > 0.5)
        float glow = 0;                // 0..1 telegraph glow
        Color glowCol{255, 120, 90, 255};
        Vector2 charge{}; float chargeK = 0;   // charge orb at the muzzle (local px) and its size 0..1
        float logT = 0;
        int blocked = 0;               // enemy shots removed at the rest-zone boundary (should stay ~0: spawn filter)
        int dropped = 0;               // pattern bullets never emitted because their path crossed the rest zone
    } warden;
    bool wardenOn() const { return warden.st != WardenState::Off; }
    bool bossShielded() const { return warden.st != WardenState::Off && warden.shield > 0.5f; }
    int bossHpFloor(const Enemy& e) const;   // the boss cannot drop below the next helper-wave threshold while engaged
    // The rest zone (local px). While on, no enemy shot is ever emitted along a path through it (inflated by the
    // bullet + player hit radii), and any shot that would still enter it (homing) is removed at its edge.
    bool safeOn = false; Rectangle safeRect{}; float playerHitR = 0;
    bool insideSafe(Vector2 p) const { return safeOn && p.x >= safeRect.x && p.x <= safeRect.x + safeRect.width && p.y >= safeRect.y && p.y <= safeRect.y + safeRect.height; }
    std::function<void(int ev)> onBossEvent;   // 1 helpers inbound, 2 warden returns (phase in warden.phase), 3 shield hit

private:
    Enemy* spawnOne(const EnemyDef& d, const LevelDef& L, const View& v, Vector2 pos);
    void fire(Enemy& e, EnemyWeapon w, int shots, Vector2 player, const View& v, Audio& sfx);
    void rotaryRound(Enemy& e, Vector2 player, const View& v, Audio& sfx);  // one round of a Hornet burst
    static constexpr int kMaxFormations = 4;
    Formation formations_[kMaxFormations];
    struct Minions { std::string enemy; int count; float y; std::string formation = "v"; };
    void updateWarden(Enemy& e, float dt, Vector2 player, const LevelDef& L, const View& v, Audio& sfx);
    void wardenFire(Enemy& e, int kind, int round, const View& v, Audio& sfx);
    bool pathCrossesSafe(Vector2 from, Vector2 vel, float r) const;
    bool segCrossesSafe(Vector2 a, Vector2 b, float r) const;
    std::vector<Minions> pendingMinions_;
    float waveDepth_ = 0;  // depth of the wave being spawned (gameplay units, + = nearer)
    bool spawnClear(Vector2 pos, float hidden, float r, const View& v, int ignoreGroup = -2) const;
};
