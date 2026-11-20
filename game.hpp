#pragma once
#include <vector>
#include "assets.hpp"
#include "audio.hpp"
#include "background.hpp"
#include "cutscene.hpp"
#include "data.hpp"
#include "enemies.hpp"
#include "particles.hpp"
#include "vfx.hpp"
#include "player.hpp"
#include "view.hpp"
#include "weapons.hpp"
#include "render3d.hpp"
#include "turret.hpp"
#include <memory>
#include <string>

enum class PickupType { Weapon, Missiles, Shield, Repair, Rapid, Flamer, Plasma };   // Flamer / Plasma: primary weapon unlocks (t219)
enum class PickupState { Active, CollectedDisperse, MissedFlash };
struct Pickup {
    bool alive = false;
    Vector2 pos{};
    PickupType type = PickupType::Weapon;
    PickupState state = PickupState::Active;
    float t = 0, stateTime = 0;
};

class Game {
public:
    Game();
    ~Game();
    void update(float dt);
    void draw();

private:
    enum class State { Title, LevelSelect, Options, Development, Cutscene, Playing, LevelClear, GameOver, Victory };
    enum class After { None, StartLevel, NextStep };

    // flow
    void newCampaign(int step);
    void runStep();                 // execute campaign_[step]
    int campaignPos(int levelIdx) const;
    std::vector<int> levelOrder() const;
    void startLevel(int level, bool freePlay);
    void beginPlaying();
    void finishLevel();
    void playCutscene(const std::string& id, After then);
    void updateLevel(float dt);
    void reloadData();
    void loadSave();
    void writeSave() const;

    // gameplay
    void updatePlayer(float dt);
    void collide();
    void damagePlayer(int dmg, Vector2 at);
    void killEnemy(Enemy& e, bool award = true);  // award = false: a crash into a hazard (no score/drop)
    void dropPickup(const Enemy& e);
    void applyPickup(PickupType t);
    void updateLevel3Hazards(float dt);
    // Level 3 solids (bulkhead + turret) for ships, the player and projectiles. Shapes follow
    // the drawn art; see BulkheadModel and turretSolids().
    struct HazardCapsule { Vector3 a{}, b{}; float r = 0; };  // local x, y, hidden-axis depth
    bool doorSolid() const;
    Vector3 doorModel(Vector2 pos, float hidden) const;
    float doorModelRadius(float radius) const;
    float doorFaceX(Vector2 pos, float hidden, float modelV) const;
    bool turretSolid() const;
    int turretSolids(HazardCapsule out[3]) const;
    bool turretContact(Vector2 pos, float hidden, float radius, Vector3* normal, float* penetration) const;
    void updateHazardAvoidance(float dt);
    void resolveHazardSpawns();
    void emitGlitter(float dt);
    void updateCombatInput();

    // rendering
    void drawWorld() const;
    void drawHud() const;
    void drawTitle();
    void drawLevelSelect();
    void drawOptions();
    void drawDevelopment();
    void drawTouchControls() const;
    void drawEnemyHealthBars() const;
    // Level 6 planet approach (LevelDef::approach): timeline state, recomputed from levelTime
    struct Approach { bool on = false; float dist = 30, spin = 0, heat = 0, clouds = 0, haze = 0, shakeAmp = 0, panel = 1;
                      float sunTheta = 0, cityK = 0; bool terrain = false, scenery = false, lull = false;
                      float t = 0;   // position on the authored approach timeline (see apTime)
                      // chase camera (see updateApproach): orbit progress o, rise-to-overhead rho
                      float o = 0, rho = 0, stars3d = 0, fovy = 38.6f, shipPx = 60; bool cam3d = false, handDone = false;
                      Vector2 qHand{0, 0}, shipScr{0, 0}, bowScr{0, 0}, wingL{0, 0}, wingR{0, 0}, flow{0, 0}; Vector2 jitter{0, 0};
                      Vector3 f{-1, 0, 0}, u{0, 1, 0}, pdir{0, 0, -1}, sun{0, 1, 0};
                      // Liftoff (LevelDef::liftoff): launch pad under the ship, engine spool, ground drop
                      bool liftoff = false, padLock = false; float liftPower = 1, padDepth = 0, padPx = 0, liftScroll = 0, drop = 0;
                      Vector2 padPos{0, 0};
                      // Liftoff (t214): altitude above the ground (planet radii), scroll slow-down in the near-vertical
                      // climb, the planet frame turned with pdir, and the volumetric cloud / atmosphere pass
                      float alt = 0, scrollK = 1, kside = 0, climbPitch = 0, flameBoost = 1, smokeDebt = 0;
                      Matrix prot{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                      Render3D::VolFog fog;
                      // Liftoff punch-out exhaust smoke: soft white billows (screen space, premultiplied)
                      struct Puff { Vector2 p{0, 0}, v{0, 0}; float sz = 0, grow = 0, life = 0, maxLife = 1, a = 0; unsigned char g = 220; };
                      std::vector<Puff> puffs; };
    Approach appr;
    void updateApproach(float dt);
    void updateFlightAudio(float dt);    // t235: Liftoff / Planetfall sound (flight_audio.cpp)
    float apTime(float levelT) const;    // level time -> approach timeline (L6 stretch / L4 continuation)
    float apLevelTime(float apT) const;  // inverse of apTime
    void setupLiftoffPad();              // Liftoff: pick a flat pad, put the ship on it, scroll the ground to it
    void updateLiftoffPad(float dt);     // Liftoff: ship rests on the pad, spools up, climbs to the flight plane
    bool planetCarry = false;            // Planetfall just ended: the next approach_start level keeps its ground scroll
    Render3D::ApproachScene approachScene() const;      // the scenery camera / planet / sun for this frame
    void drawApproachBack(const Camera2D& cam) const;   // planet + limb glow (behind ships)
    void drawApproachFront() const;                     // entry heat, rushing clouds (over ships)
    void drawApproachPanels() const;                    // turned playfield frame  // faint world-anchored bar under an enemy while it is being damaged
    void drawPauseMenu();
    void drawEndOverlay() const;                        // LEVEL CLEAR / GAME OVER (menus.cpp)
    void drawVictory();
    void exitToMenu();              // ESC / gamepad START / the HUD pause button while playing
    bool button(Rectangle r, const char* label, bool enabled = true, const Font* font = nullptr);  // immediate-mode tap button
    void updateBossDeath(float dt);
    // ---- t251: The Warden's death (hit-stop, internal chain, inside-out breakup, fireball + shockwave rings, bonus)
    struct BossDeathFx { bool on = false, broke = false, bonus = false; float t = 0, real = 0, breakReal = -1, popT = 0, flash = 0;
                         Vector2 pos{}, vel{}; float depth = 0, pitch = 0, roll = 0, rollV = 0; float dent[8][4]{}; int nDent = 0, dentNext = 0, pops = 0;
                         int model = 0; Color tint{255, 255, 255, 255}, engine{0, 0, 0, 0}; const EnemyDef* def = nullptr; };
    BossDeathFx bdx;
    float bossTimeScale(float rawDt);
    void startBossDeath(Enemy& e);
    void drawBossDeath3D() const;   // inside the ships pass
    void drawBossOverlay() const;   // screen space: flash, rest-zone marks, the WARDEN DESTROYED title
    int zoneHits = 0, allHits = 0; float zoneTime = 0;   // rest-zone verification (enemy shots reaching the player)
    // graphics quality (Options > Graphics: AUTO / HIGH / LOW)
    void applyQuality();
    void updateQuality(float dt);
    const char* gfxName() const;
    int gfxMode = 0, autoStep = 0, qFrames = 0;
    float rs = 1, qT = 0;
    RenderTexture2D rt{};
    mutable Render3D r3d;  // drawn from const drawWorld
    float pPitch = 0, pRoll = 0;  // player attitude (rad), used only in 3D
    float cameraTurn = 0, corridorScrollTime = 0;

    struct CombatInput {
        bool primaryHeld = false, secondaryPressed = false, mouseMovement = true;
        int movementTouch = -1, movementId = -1;
        bool movementPositionValid = false;
        Vector2 movementPosition{};
    } combatInput;

    float W = 0, H = 0, s = 1;
    View view;
    State state = State::Title;
    float engineMute = 0;  // seconds the engine loop stays faded after the player explodes
    Assets assets;
    Database db;
    Audio audio;
    Particles fx;
    Vfx vfx;               // Ryan's Three.js explosion + impact effects (assets/fx/*.json)
    float shipPx(float scale) const;  // on-screen enemy ship size for effect scaling
    Background bg;
    Enemies enemies;
    Weapons weapons;
    Player player;
    std::unique_ptr<Controller> controller;
    Pool<Pickup, 32> pickups;

    Cutscene cutscene;
    After afterCutscene = After::None;
    Background titleBg;
    Font titleFont{};
    int levelIdx = 0, waveIdx = 0, step = 0, savedStep = 0, unlocked = 1, lives = 3, score = 0, highScore = 0;
    float waveBase = 0, lastSpawn = 0;  // timed levels: wave-script cycle start, last spawn time
    float levelTime = 0, stateTime = 0, shake = 0, time = 0, titleRevealTime = 0, banner = 0, bossDeath = 0, volume = 0.8f;
    float damageSmokeDebt = 0;
    float priorWeaponHeat = 0;
    bool priorWeaponOverheat = false;
    Vector2 bossDeathPos{};
    bool bossSpawned = false, showFps = true, autoFire = true, secondaryWasHeld = false, freePlay = false, quit = false, tapConsumed = false;
    bool pickupTestSpawned = false;
    // ---- t219: selectable primaries. ownedWeapons bit i = PrimaryWeapon i (laser always). Saved; never lost.
    unsigned ownedWeapons = 1u;
    float weaponGiftT = 0.f;          // level time of the next guaranteed weapon-unlock pickup (L4 / Liftoff)
    float weaponNoteT = 0.f; std::string weaponNote;   // short HUD caption ("FLAMER ONLINE", "PLASMA" ...)
    bool switchWasHeld = false;
    float beamSoundT = 0.f;
    float flameSmokeT = 0.f;
    float hudMessageT = 0.f;
#if defined(_WIN32)
    bool showKeyboardHints = true;
#else
    bool showKeyboardHints = false;
#endif
    Color hudMessageColor{255, 220, 120, 255};
    std::string hudMessage;
    int ownedCount() const { int n = 0; for (int i = 0; i < kPrimaryCount; i++) n += (ownedWeapons >> i) & 1u; return n; }
    void selectWeapon(int w, bool announce);
    void cycleWeapon(int dir);
    void grantWeapon(int w);
    void updateWeaponGifts(float dt);
    Vector2 switchButtonPos() const;
    void drawWeaponIcon(Vector2 c, float r, int w, float alpha) const;
    // ---- t219: Liftoff escape burn: the shield is offline (and hidden) from the pad to orbit
    int shieldStash = 0; bool shieldOffline = false; float shieldNoteT = 0.f;
    void updateEscapeShield();
    // ---- t219: The Warden: the Liftoff sun (3D shader, flares, nebula, point stars) approached in orbit
    struct WardenSky { bool on = false; float k = 0.f, bossAt = -1.f; Vector3 f{0, 0, -1}, u{0, 1, 0}, sun{0, 0, -1}, sunCam{0, 0, -1}; float sunR = 0.1f, fovy = 40.f, expo = 0.f; };
    WardenSky wsky;
    void updateWardenSky(float dt);
    void drawWardenSky(const Camera2D& cam) const;
    bool paused = false;  // in-level pause menu (Playing is frozen while set)
    int developmentPage = 0;
    // Level 3 vertical-slice hazards. Visuals, collision, and firing are driven by these same
    // values so a player never encounters an invisible door or a detached turret muzzle.
    Turret level3Turret;
    Turret level3TurretBottom;
    struct Level3Bulkhead {
        bool active = false, spawned = false; float phase = 0, opening = 1, x = 0, age = 0;
        float mountStartX = 0, corridorScrollAtMount = 0;
        float mountedX(float corridorScrollTime, float pixelsPerWorldUnit) const {
            return mountStartX - (corridorScrollTime - corridorScrollAtMount) *
                Corridor::kWorldUnitsPerScrollSecond * pixelsPerWorldUnit;
        }
    } level3Door;
    int level3DoorCount = 0;
    float turretContactCooldown = 0;  // player-vs-turret impact damage interval
    // Level 6 ground guns: Level 3 turrets standing on flattened pads of the chunked terrain, fixed in
    // world space (they scroll with the ground). Only during the overhead combat (118 s on).
    struct GroundGun {
        Turret t; Render3D::GroundGunDraw d; long cell = 0;
        Vector2 muzzle{};
        float barSince = -1.f, barAlpha = 0.f, flash = 0.f; int burstLeft = 3;
    };
    std::vector<GroundGun> groundGuns;
    long groundGunNext = 0; bool groundGunInit = false; float groundScroll = 0.f;
    std::string groundModel;
    void updateGroundGuns(float dt);
    int movementTouchId = -1;  // stable raylib ID: list indexes can reorder as fingers land or lift
public:
    bool wantsQuit() const { return quit; }
private:
    std::string savePath;
};
