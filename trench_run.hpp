#pragma once
// t252: Moon Base trench run (level_08). The moon's surface is a station: after the opening turn the
// ship dives onto it, skims the greebled plating, drops into a twisting trench (turrets everywhere,
// TIE-style chasers behind / past / alongside), drops the SUPER DROP BOMB into the exhaust port at
// the end and climbs out with the fire chasing it. MoonLevel then shows the moon exploding.
//
// Space: the trench centreline path runs over the flat station surface (world y = 0). A point in
// the trench is (s along, u lateral, y height above the trench floor); floor at world y = -kDepth.
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <array>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <string>

struct Player;
class Weapons;
class Audio;
struct View;
class Render3D;

class TrenchRun {
public:
    static constexpr float kHalfW = 13.f, kDepth = 30.f, kSeg = 16.f;
    // trench time (s since the trench scene took over the frame)
    static constexpr float kDiveEnd = 7.f, kSkimEnd = 12.f, kEntryEnd = 15.5f;   // t267: 7 s Planetfall-style descent
    static constexpr float kBlastReach = 3.8f;   // t267: s after the detonation the fire reaches a ship still in the trench
    static constexpr float kPortS = 11200.f;   // exhaust port distance along the path (first pass)

    TrenchRun();
    ~TrenchRun();
    bool init();
    void update(float dt, float t, Player& player, Weapons& weapons, Audio& audio, const View& view, float s,
                const std::function<void(int, Vector2)>& damage, int& score, float shake);
    void draw(Render3D& r3d, const View& view, float t, float renderScale);
    // the moon-explosion shot (MoonLevel's camera): the ship racing away, with its fire trail
    void drawEscapeShip(const Camera3D& c, Vector3 moonCentre, float moonR, float since);
    bool finaleStarted() const { return finaleT_ >= 0.f; }
    float finaleTime() const { return finaleT_; }      // trench time the moon shot starts
    float detonateTime() const { return detT_; }
    // t267: controlled exit. A ship still in the trench when the fire arrives is destroyed; Game takes the life.
    bool blastDeathReady() const { return deadT_ >= 0.f && lastT_ - deadT_ > 2.4f && !deathTaken_; }
    void takeBlastDeath() { deathTaken_ = true; }
    bool escaped() const { return escapedT_ >= 0.f; }
    Vector3 sunDir() const { return sun_; }

private:
    // ---- path
    struct Path {
        float ds = 2.f; std::vector<float> h, x, z;
        void build(float len);
        float heading(float s) const;
        Vector3 centre(float s) const;      // surface level (y = 0)
    } path_;
    Vector3 fwd(float s) const;
    Vector3 right(float s) const;
    Vector3 world(float s, float u, float y) const;   // y = height above the floor
    Matrix frame(float s) const;                         // trench-local -> world (origin centre at surface)
    float project(Vector3 p, float sGuess, float& u, float& y) const;   // world -> (s, u, y)

public:
    struct Lamp { Vector3 p; float phase; float size; Color c; int kind; };   // kind 0 red blinker, 2 blue, 3 coil, 4 port
private:
    // ---- geometry kit
    struct Variant { Mesh full{}, lod{}; bool ok = false; std::vector<Lamp> lamps; int gantry = 0; };
    std::vector<Variant> seg_;          // trench segment variants (0..7 normal, 8 low gantry, 9 high gantry, 10 port)
    std::vector<Variant> tile_;         // surface tiles
    static constexpr float kTile = 64.f;
    Mesh turretBase_{}, turretHead_{}, turretGun_{}, cube_{}, bombMesh_{};
    Shader sh_{}; bool shOk_ = false;
    int locDmgSpot_ = -1, locDmgHeat_ = -1, locDmgN_ = -1, locHole_ = -1, locHoleN_ = -1;   // t267 visible hull damage
    int locTrenchInv_ = -1, locSun_ = -1, locCam_ = -1, locTime_ = -1, locUseAtlas_ = -1, locFog_ = -1, locFogK_ = -1,
        locAmb_ = -1, locSpec_ = -1, locHalfW_ = -1, locDepth_ = -1, locTint_ = -1, locGlow_ = -1, locFlash_ = -1, locFlashPos_ = -1;
    Material matAtlas_{}, matPlain_{};
    Texture2D albedo_{}, normal_{}, soft_{}, fire_{}, streak_{};
    bool texOk_ = false;
    mutable std::unordered_map<long, int> tileCache_;
    // t275: surface tiles beside the trench are classified by their true nearest path point and drawn clipped to the trench
    struct TileClip { float cx, cz, h, k, sMin, sMax; };
    mutable std::unordered_map<long, TileClip> tileClip_;
    static constexpr float kGridCell = 160.f;
    static long long gridKey(int cx, int cz) { return ((long long)cx << 32) ^ (long long)(unsigned)cz; }
    std::unordered_map<long long, std::vector<int>> pathGrid_;
    void buildPathGrid();
    float nearestS(Vector3 p, float& u) const;
    int locClipA_ = -1, locClipW_ = -1;
    Model player_{}, chaser_{}; bool playerOk_ = false, chaserOk_ = false;
    float playerScale_ = 1.f, chaserScale_ = 1.f; Vector3 playerC_{}, chaserC_{};
    Vector3 pWingL_{}, pWingR_{}, pNoz_{}, pNozL_{}, pNozR_{}, cWingL_{}, cWingR_{}, cNoz_{};
    Vector3 sun_{0.5f, 0.78f, 0.36f};
    Sound sRoar_{}, sWhoosh_{}, sTurret_{}, sChaser_{}, sDrop_{}, sBlast_{}, sCrash_{}, sScrape_{}, sBeep_{};
    bool sndOk_ = false;
    void buildKit();
    void loadShips();
    void loadSounds();
    int segVariant(long i) const;
    void drawEnv(const Camera3D& c, float t);
    void drawShipModel(Model& m, Matrix M, float sAt, Color tint, float glow);
    void setTrenchUniform(float sAt);
    void drawGlows(const Camera3D& c, float t);
    void beam(const Camera3D& c, Vector3 a, Vector3 b, float w, Color col) const;
    void hud(const View& view, float t, float W, float H);

    // ---- gameplay state
    struct Ship { float s = 0, u = 0, y = 0, vu = 0, vy = 0, v = 0, bank = 0, pitch = 0; };
    Ship pl_{};
    float hp_ = 1.f;
    Vector3 plPos_{}, plPrevPos_{}; Matrix plM_{};
    struct Turret { bool alive = false; long seg = 0; int slot = 0; float s = 0, u = 0, y = 0; bool rim = false;
                    float yaw = 0, pitch = 0, fireT = 2, tele = 0, recoil = 0, hp = 3, deadT = -1; int barrel = 0; };
    std::vector<Turret> turrets_;
    std::unordered_set<long> killedTurret_, seenTurretSeg_;
    struct Chaser { bool alive = false; int mode = 0; float s = 0, u = 0, y = 0, vs = 0, vu = 0, vy = 0, hp = 4, fireT = 1, t = 0,
                    bank = 0, wu = 0, wy = 0, side = 1; bool passed = false, whooshed = false; float crashT = -1; Vector3 pos{}; };
    std::array<Chaser, 8> ch_{};
    int wave_ = 0;
    struct Bolt { bool alive = false; Vector3 p{}, v{}; float life = 0, sGuess = 0; int dmg = 1; bool enemy = false; Color c{}; float w = 0.35f, len = 6.f; bool turret = false; };
    std::array<Bolt, 256> bolts_{};
    struct Part { bool alive = false; Vector3 p{}, v{}; float size = 1, grow = 0, life = 0, max = 1, drag = 0, rise = 0; int kind = 0; Color c{}; };
    std::array<Part, 1600> parts_{};
    int partNext_ = 0;
    struct Debris { bool alive = false; Vector3 p{}, v{}, rot{}, spin{}; float size = 1, life = 0, hot = 1; };
    std::array<Debris, 120> debris_{};
    struct Streak { Vector3 p{}; };
    std::array<Streak, 90> streaks_{};
    bool streakInit_ = false;

    float sPort_ = kPortS;
    bool bombReady_ = false, bombDropped_ = false, autoDropDone_ = false;
    float dropT_ = -1, detT_ = -1, finaleT_ = -1, missT_ = -1, readyT_ = -1;
    Vector3 bombPos_{}; float bombS0_ = 0, bombU0_ = 0, bombY0_ = 0;
    float flash_ = 0, veil_ = 1.f, shake_ = 0, scrapeT_ = 0, hitFlash_ = 0;
    float camMode_ = 0;   // 0 chase, 1 fpv, 2 rear (cinematic)
    int forcedCam_ = -1;  // V key / TRENCH_CAM override
    float lastT_ = 0;
    Camera3D cam_{};
    float fov_ = 70.f;
    bool prevDropKey_ = false, prevMouse_ = false;
    std::string msg_; float msgT_ = 0; Color msgCol_{255, 255, 255, 255};
    float beepT_ = 0;
    int kills_ = 0, crashes_ = 0, turretKills_ = 0;
    // t260 flight model: height is flown directly; left / right is a bank along a U across the trench
    float bankB_ = 0.f, bankBV_ = 0.f;          // -1 full left wall ride .. +1 full right (eased, critically damped)
    float hBase_ = 15.f, climbD_ = 0.f;          // player-chosen height; extra climb up the wall at full bank
    bool climbArmed_ = true;
    float halfSpan_ = 4.1f, halfThick_ = 1.5f;   // ship half extents (player: hull sockets, not bind-pose bbox)
    Vector3 plR_{1, 0, 0}, plU_{0, 1, 0};        // rolled ship right / up (guns, camera)
    // ---- t267: weapons in 3D (plasma lance beam, flamethrower cone, homing missiles)
    struct Missile3 { bool alive = false; Vector3 p{}, v{}; float life = 0, trailT = 0; int tc = -1, tt = -1; };
    std::array<Missile3, 16> mis_{};
    bool beamOn_ = false, beamHit_ = false; Vector3 beamA_{}, beamB_{}; float beamAcc_ = 0.f, flameTick_ = 0.f, flameDebt_ = 0.f, misTestT_ = 0.f;
    int hitsBy_[4]{}, killsBy_[4]{}, shotsBy_[4]{};   // laser, plasma, flame, missile (chaser + turret hits)
    int* scoreP_ = nullptr;
    Vector3 aimAssist(Vector3 from, Vector3 dir, float cone, float range) const;
    void hitChaser(Chaser& c, float dmg, Vector3 at, Vector3 dir, int by);
    void hitTurret(Turret& tr, float dmg, Vector3 at, int by);
    void updateWeapons3(float dt, float t, Player& player, Weapons& weapons, Audio& audio, bool& dropReq);
    // ---- t267: visible hull damage (trench only; self-contained, reusable for any 3D ship model)
    struct HullSpot { Vector3 local{}; float r = 0.6f, heat = 1.f, str = 0.3f, smokeAcc = 0.f, sparkT = 0.f; };
    std::array<HullSpot, 8> spots_{}; int nSpots_ = 0;
    Vector3 holeLocal_[2]{}; float holeR_[2]{}, holeAcc_[2]{}, holeSpark_[2]{}; int nHoles_ = 0;
    struct Plate { bool alive = false; Vector3 p{}, v{}, rot{}, spin{}, size{}; float life = 0; Color c{}; float hot = 0; };
    std::array<Plate, 64> plates_{};
    std::vector<Vector3> hullVerts_;
    float hullAcc_ = 0.f, hullDmg_ = 0.f; int lastHp_ = -1; int hullHits_ = 0, platesSpawned_ = 0;
    void hullHit(Vector3 at, float sev);
    float descEnd() const { return descDone_ ? std::min(kDiveEnd, descDoneT_ + 1.2f) : kDiveEnd; }
    void updateHull(float dt, const Player& player);
    // ---- t267: descent + exit
    float diveOff_ = 0.f, descShake_ = 0.f, enteredT_ = -1.f, deadT_ = -1.f, escapedT_ = -1.f, pullBeepT_ = 0.f;
    bool deathTaken_ = false, descDone_ = false; float descDoneT_ = -1.f, weapLogT_ = 0.f; int turretHitsBeforeEntry_ = 0;
    int turretShots_ = 0, turretHitsAbove_ = 0, turretHitsInside_ = 0, turretBoltsCut_ = 0, turretHolds_ = 0, clipFix_ = 0;
    float testLogT_ = 0.f;
    bool loggedEvents_[16]{};
    void message(const char* m, float secs, Color c);
    void spawnFire(Vector3 p, Vector3 v, float size, float grow, float life, int kind, Color c, float drag = 1.f, float rise = 0.f);
    void explode(Vector3 p, float k, Vector3 vel);
    void sparks(Vector3 p, Vector3 n, int n2, float speed);
    void updateTurrets(float dt, float t, Audio& audio);
    void updateChasers(float dt, float t, Audio& audio, int& score);
    void updateBolts(float dt, const std::function<void(int, Vector2)>& damage, Vector2 at, int& score, Audio& audio);
    void updateParts(float dt);
    void spawnWave(int kind, float t);
    Camera3D makeCamera(float t);
    bool shot(const char* name);
    Vector3 shipWorld(const Ship& sh) const { return world(sh.s, sh.u, sh.y); }
    Rectangle dropButton(float W, float H, float sc) const;
    float playH_ = 0, playW_ = 0, uiS_ = 1;
    float fpsAcc_ = 0; int fpsN_ = 0; float fpsMin_ = 1e9f, fpsSum_ = 0; int fpsSecs_ = 0;
public:
    bool wantsDropTap(Vector2 p) const;
    void toast(const char* m);   // t260: short HUD message (UP/DOWN: NORMAL / INVERTED)
    void captures(float t);
};

// t267: the moon level's flight sound (descent engine hum, finale music dip), written by TrenchRun / MoonLevel
// every frame and read by Game::updateFlightAudio (flight_audio.cpp). stamp = GetTime() of the last write.
struct TrenchAudioCue { double stamp = 0.0; float drone = 0.f, roar = 0.f, roarRate = 1.f, musicDuck = 0.f, engineDuck = 0.f; };
extern TrenchAudioCue g_trenchAudio;
