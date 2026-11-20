#pragma once
#include "raylib.h"
#include "view.hpp"
#include "presentation_asset.hpp"
#include "world_position.hpp"
#include <map>
#include <string>
#include <vector>

// ---- Enemy definitions: assets/enemies/<id>.json -------------------------
enum class Pattern { Straight, Sine, Dive, Circle, Path, Invaders, Boss };
enum class EnemyWeapon { None, Aimed, Spread, Laser, Missile, Spiral, Sweep, Needle, Rotary };

struct Drop { std::string type; float weight = 1; };

// Boss phase: active while hp fraction <= `hp` (phases listed high -> low).
struct BossPhase {
    float hp = 1;
    Pattern pattern = Pattern::Boss;
    EnemyWeapon weapon = EnemyWeapon::Spread;
    float interval = 1; int shots = 5;
    std::string minion; float minionEvery = 0; int minionCount = 0;
};

struct EnemyDef {
    std::string id, name;
    PresentationAsset presentation;
    float scale = 1, speed = 130;      // speed in design px/s
    float modelScale = 1;              // t251: extra 3D model size multiplier (JSON modelScale; collision follows `scale`)
    Color tint = WHITE;
    Color engineTint{0, 0, 0, 0};      // engine flame colour (JSON engineTint RGB); alpha 0 = default blue-white flame
    int hp = 1, score = 100;
    bool boss = false;
    Pattern pattern = Pattern::Straight;
    float amp = 60, freq = 2;          // pattern amplitude (px) / frequency (rad/s)
    std::vector<Vector2> path;         // normalized screen points for Pattern::Path
    EnemyWeapon weapon = EnemyWeapon::None;
    float fireInterval = 2.0f;
    int shots = 1;                     // bullets per volley (spread)
    float shotSpeed = 1, shotSize = 1; // weapon.speed / weapon.size multipliers
    int shotDamage = 0;                // weapon.damage (0 = kBulletDamage)
    float telegraph = 0;               // weapon.telegraph: seconds the model's glow charges before each shot
    float spinUp = 0.1f, burstGap = 0.09f; // rotary: weapon.spinUp (s from trigger to 1st round), weapon.burstGap (s between rounds)
    float dropChance = 0.15f;
    std::vector<Drop> drops;
    std::vector<BossPhase> phases;     // bosses only
};

// ---- Player definitions: assets/players/<id>.json -------------------------
// controller: flight (implemented) | ground_side | ground_topdown (planned).
enum class ControllerType { Flight, GroundSide, GroundTopDown };

struct PlayerDef {
    std::string id = "red_fighter";
    PresentationAsset presentation{"sprites/red_fighter_pi3_sprites.json"};
    ControllerType controller = ControllerType::Flight;
    float size = 110, speed = 650;
    int hp = 100;
    // ground controllers (planned): gravity, jump impulse, walk/run speed
    float gravity = 1800, jump = 720, walk = 260, run = 420;
};

// ---- Level definitions: assets/levels/level_XX.json -----------------------
enum class LayerType { Texture, Stars, Nebula, Planet, Asteroids, Planet3D, Terrain3D };

struct BgLayer {
    LayerType type = LayerType::Stars;
    std::string texture;               // for Texture layers (tiled horizontally)
    float speed = 50;                  // scroll speed px/s (design units)
    Color color = WHITE;
    int density = 100;
    float y = 0.5f, size = 1;          // normalized position / scale for planets etc.
    float x = 0.7f, spin = 0.05f;      // planet3d: normalized start x (forward), surface rotation rad/s
    float cloudSpin = 0.0f;            // planet3d: optional cloud-shell rotation rad/s
    std::string model;                 // planet3d / terrain3d: models/<model> (GLB)
    std::string cloudModel;            // planet3d: optional transparent cloud-shell GLB
};

struct SpawnWave {
    float time = 0;                    // seconds since level start
    std::string enemy;
    int count = 1;
    std::string formation = "line";    // line | column | v | random | row
    float y = 0.5f, spacing = 70;      // normalized centre y, design px spacing
    float amp = -1, freq = -1;         // optional pattern overrides
    std::vector<Vector2> path;         // optional Pattern::Path override (normalized forward, lateral)
};

// Level-owned spawn in design-space units: X forward, Y across, Z up.
struct LevelSpawnPoint {
    std::string id;
    WorldPosition position{};
};

struct LevelDef {
    std::string id, name, music;       // music: path under assets/ (streamed, looped)
    float duration = 0;                // seconds of play; >0 = waves repeat until then (non-boss levels)
    Mode mode = Mode::SideScroll;
    std::vector<BgLayer> layers;
    std::vector<SpawnWave> waves;           // sorted by time
    std::vector<LevelSpawnPoint> spawnPoints;
    std::string boss;                  // enemy id, spawned after the last wave clears
    std::string player = "red_fighter"; // PlayerDef id
    std::string terrain;               // collision map for ground levels (planned)
    std::string intro, outro;          // cutscene ids (assets/cutscenes/<id>.json)
    float hpMul = 1, fireMul = 1, speedMul = 1;
    Color clear = {6, 6, 18, 255};
    bool approach = false;             // level 6: side view rotates overhead during a planet approach / entry
    // Approach timeline mapping (seconds of the authored L6 timeline, see game.cpp kAp*):
    //  approachEnd > 0: the level ends at that point of the timeline (at `duration`); everything before
    //                   the clouds (0..100) is stretched to fill the extra time, clouds + rise run 1:1.
    //  approachStart > 0: the level continues the timeline from that point (1:1), e.g. Level 4 picks
    //                   up overhead below the clouds where Planetfall ended.
    float approachStart = 0, approachEnd = 0;
    bool groundGuns = false;           // planet-terrain ground turrets (overhead part of the timeline)
    bool liftoff = false;              // "Liftoff": the approach timeline backwards (pad -> clouds -> space, 3D sun)
};

// ---- Campaign: assets/campaign.json ---------------------------------------
struct CampaignStep { std::string chapter, level, cutscene; };  // exactly one of level/cutscene set

class Database {
public:
    void load(const std::string& assetsRoot);  // (re)loads all enemies + levels
    const EnemyDef& enemy(const std::string& id) const;
    const std::map<std::string, EnemyDef>& enemies() const { return enemies_; }
    const std::vector<LevelDef>& levels() const { return levels_; }
    const PlayerDef& player(const std::string& id) const;
    const std::vector<CampaignStep>& campaign() const { return campaign_; }
    int levelIndex(const std::string& id) const;

private:
    std::map<std::string, EnemyDef> enemies_;
    std::map<std::string, PlayerDef> players_;
    PlayerDef defaultPlayer_;
    std::vector<LevelDef> levels_;
    std::vector<CampaignStep> campaign_;
    EnemyDef fallback_;
};
