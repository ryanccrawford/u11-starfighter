#pragma once
#include "raylib.h"
#include "view.hpp"
#include "render3d.hpp"
#include "player.hpp"
#include "weapons.hpp"
#include "particles.hpp"
#include "vfx.hpp"
#include "audio.hpp"
#include <vector>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include "trench_run.hpp"   // t252

// Moon Breakup (level_08). Timeline (level seconds):
//   0-24   approach: whole cratered moon grows to ~45% of the screen height, right of centre
//   24-28  a dark asteroid (1/8 of the moon's diameter) falls onto the sun-facing side
//   28     impact: flash, molten crater, ejecta cone, surface ripple; the asteroid becomes debris
//   28-40  lightning-like cracks spread from the crater, charred plates push out, dust plume vents
//   40     breakup: the moon splits into sun-lit plates drifting apart; the debris storm begins
//   40-170 dodge large / small sun-lit rocks (gameplay chunks), no enemies
//   170+   calm, the field drifts and fades
enum class ChunkSize { Large, Medium, Small };

// Gameplay debris chunk
struct RockChunk {
    bool alive = false;
    ChunkSize size = ChunkSize::Medium;
    Vector2 pos{};          // Gameplay local space (x forward, y lateral)
    Vector2 vel{};          // Velocity (px/s)
    float hiddenAxis = 0.f; // Depth axis (-5..+5 world depth)
    float velZ = 0.f;       // Depth velocity
    Vector3 rot{};          // 3D Euler angles
    Vector3 spin{};         // 3D angular velocities
    float radius = 24.f;    // Gameplay collision radius
    int hp = 100;
    int maxHp = 100;
    int meshIndex = 0;
    float mass = 1.f;
    float hot = 0.f;        // Faint glow from fire/explosion
    float age = 99.f;       // t247: < kApproachTime = still flying out of the debris cloud (far behind, no contact)
};

// Moon-space particle (position / velocity in moon radii about the moon centre)
struct MoonParticle {
    bool alive = false;
    Vector3 pos{};
    Vector3 vel{};
    float size = 0.05f;     // moon radii
    float grow = 0.f;       // size growth (radii / s)
    float drag = 0.f;       // 1/s
    float life = 0.f;
    float maxLife = 1.f;
    float alpha = 0.6f;
    Color tint{255, 255, 255, 255};
};

// Moon-space 3D rock (asteroid fragments, ejecta boulders)
struct MoonRock {
    bool alive = false;
    Vector3 pos{};
    Vector3 vel{};
    Vector3 rot{};
    Vector3 spin{};
    float r = 0.03f;        // moon radii
    float albedo = 0.45f;
    float hot = 0.f;
    int mesh = 0;
};

// Pre-fractured moon plate
struct MoonShard {
    Model model{};
    bool loaded = false;
    Vector3 restCentroid{};
    Vector3 offset{};       // moon radii
    Vector3 vel{};          // moon radii / s
    Vector3 rot{};
    Vector3 spin{};
    float radius = 20.f;
    int hp = 400;
    bool active = true;
    bool isRemnantRing = false;
};

class MoonLevel {
public:
    MoonLevel();
    ~MoonLevel();

    void init(const View& view);
    void update(float dt, float levelTime, Player& player, Weapons& weapons,
                Particles& fx, Vfx& vfx, Audio& audio, Render3D& r3d, const View& view, float s,
                std::function<void(int dmg, Vector2 at)> damagePlayerCb,
                int& score, float& shake);
    void drawSky(Render3D& r3d, const View& view, float levelTime, const Texture2D* glow = nullptr, float renderScale = 1.f) const;
    // t248: the level opens where The Warden ended (huge sun in view), then the ship turns away
    // until the sun is behind / beside the camera and the moon is ahead, lit from the front.
    struct EntrySky {
        bool fromWarden = false;               // came straight from the Warden's clear screen
        Vector3 f{0, 0, -1}, u{0, 1, 0}, sun{0, 0, -1};   // Warden sky frame (nebula / sun) at its end
        Vector3 sunCam{0, 0, -1};              // toward the sun in the side camera's world frame
        float sunR = 0.39f, expo = 0.32f;      // angular radius (rad), Warden exposure-down
        float time = 0.f;                      // the Warden sky's clock at its end (sun surface animation)
        Color nebulaTint{160, 30, 50, 255};
    };
    void setEntrySky(const EntrySky& e, const View& view);
    float skyDim(float levelTime) const;       // full-screen darkening of the sky layer (0..1)
    void draw3D(Render3D& r3d, const View& view, float levelTime);
    void draw2D(const View& view, float s) const;
    void shutdown();
    // t252: the trench run owns the frame from kTrenchStart until the moon-explosion shot
    bool ownsFrame(float levelTime) const { return trench_ && levelTime >= kTrenchStart; }
    void drawOwnedFrame(Render3D& r3d, const View& view, float levelTime, const Texture2D* glow, float renderScale);
    bool levelDone() const { return finaleLT_ >= 0.f && realLT_ - finaleLT_ > kFinaleEnd; }
    // t267: the ship stayed in the trench and the blast took it: Game takes the life (once)
    bool takeTrenchBlastDeath();
    void endOfFrame();
    void toast(const char* m) { if (trench_) trench_->toast(m); }   // t260
    static constexpr float kTrenchStart = 8.0f;

    static Vector3 sunDirection();   // world direction TOWARD the sun (the one light)

    static constexpr float kImpactTime = 28.f;
    static constexpr float kAsteroidLead = 4.0f;    // asteroid visible this long before impact
    static constexpr float kBreakupTime = 40.f;
    static constexpr float kApproachTime = 1.6f;    // chunk flies out of the cloud this long

private:
    void buildMoonShards();
    void buildRockVariants();
    void bakeSurfaceTextures();
    void triggerImpact(Audio& audio, float& shake);
    void triggerBreakup(Audio& audio, float& shake);
    void updateDebrisStorm(float dt, float levelTime, const View& view, float s,
                           const Player& player, float& shake);
    void spawnDebrisFromCloud(int waveType, const View& view, float s, float intensity);
    void spawnChunk(Vector2 pos, Vector2 vel, float depth, float velZ,
                    ChunkSize size, int meshIndex, float radius, int hp);
    void destroyChunk(int index, Particles& fx, Audio& audio, int& score, const View& view, float s);
    void emitDust(Vector3 pos, Vector3 vel, float size, float grow, float life, float alpha, Color tint, float drag);
    void emitSpray(Vector3 pos, Vector3 vel, float size, float life, Color tint);
    void emitEmber(Vector3 pos, Vector3 vel, float size, float life);
    void emitRock(Vector3 pos, Vector3 vel, float r, float albedo, float hot);
    void checkScreenshots(float levelTime);
    Camera3D sceneCamera(const View& view, float levelTime) const;
    // t248 opening turn
    float turnE(float levelTime) const;                        // 0 = Warden pose, 1 = moon pose
    void turnK(float a, bool inverse, Vector3& v) const;       // local (right, up, forward) rotation
    Vector3 camToWorld(float levelTime, Vector3 local) const;  // current view basis -> world
    Vector3 worldToCam(float levelTime, Vector3 w) const;
    Camera3D turnCamera(const Camera3D& base, float levelTime) const;
    Vector3 skyMap(Vector3 w) const;                           // world -> Warden sky frame
    Vector3 starMap(Vector3 w) const;
    EntrySky entry_{};
    bool hasEntry_ = false;
    float turnYaw_ = 0.f, turnPitch_ = 0.f;
    Vector3 c1r_{1, 0, 0}, c1u_{0, 1, 0}, c1f_{0, 0, -1};     // final (moon) camera basis, world
    Vector3 c0r_{1, 0, 0}, c0u_{0, 1, 0}, c0f_{0, 0, -1};     // opening camera basis, world
    Vector3 bwr_{1, 0, 0}, bwu_{0, 1, 0}, bwf_{0, 0, -1};     // Warden sky basis
    void moonFrame(const View& view, const Camera3D& c, float levelTime, Vector3& centre, float& radius) const;
    Matrix moonRotation(float levelTime) const;
    Vector3 deepWorld(const View& view, const Camera3D& c, Vector2 local, float depthWorld) const;

    // t252 trench run
    std::unique_ptr<TrenchRun> trench_;
    float finaleLT_ = -1.f, realLT_ = 0.f;
    // t267: the finale is its own destruction sequence on the intact moon (no asteroid / old breakup): story time holds
    float storyTime(float lt) const { return finaleLT_ >= 0.f ? kFinaleStory : std::min(lt, kTrenchStart + 1.f); }
    static constexpr float kFinaleStory = 20.f;   // < kImpactTime - kAsteroidLead: whole moon, no asteroid
    static constexpr float kFinPause = 3.55f, kFinTear = 4.0f, kFinaleEnd = 15.5f;   // build .. pause .. tear (s since the finale)
    void updateFinale(float dt, float since, Audio& audio, const View& view);
    void drawFinale3D(const Camera3D& c, Vector3 centre, float R, float since);
    void drawRing(Vector3 centre, Vector3 e1, Vector3 e2, float r0, float w, float a, Color mid) const;
    struct FinRock { bool alive = false; Vector3 pos{}, vel{}, rot{}, spin{}; float r = 0.03f, hot = 1.f, albedo = 0.4f; int mesh = 0; };
    std::array<FinRock, 140> finRocks_{};
    struct FinFire { bool alive = false; Vector3 pos{}, vel{}; float size = 0.1f, grow = 0.f, life = 0.f, maxLife = 1.f; bool core = false; };
    std::array<FinFire, 320> finFire_{};
    std::array<float, 40> finShardK_{};
    bool finInit_ = false, finTorn_ = false, finRumbling_ = false;
    float finCrack_ = 0.f, finWobble_ = 0.f, finHeat_ = 1.f, finGlow_ = 0.f, finShake_ = 0.f, finVentAcc_ = 0.f, finLogT_ = 0.f;
    Vector2 finJit_{};   // angular camera shake (rad: right, up)
    Sound finRumble_{}, finBoom_{}; bool finSnd_ = false;
    void emitFinFire(Vector3 pos, Vector3 vel, float size, float grow, float life, bool core = false);
    void emitFinRock(Vector3 pos, Vector3 vel, float r, float hot);
    bool initialized_ = false;
    bool impacted_ = false;
    bool detonated_ = false;   // breakup happened
    float flashAlpha_ = 0.f;
    float curLevelTime_ = 0.f;
    bool sunInFrame_ = false;  // MOON_SUN_INFRAME test variant: sun disc in view, moon back-lit (crescent)

    // Moon placement
    Vector2 moonPosLocal_{};
    float moonBaseRadius_ = 160.f;   // local units (screen-height based)
    float fwd_ = 1000.f;

    // Impact geometry (object space of the moon, unit sphere) and camera-frame directions
    Vector3 impactObj_{1, 0, 0};
    Vector3 impactCam_{0, 0, 1};     // impact normal at impact time, world
    Vector3 viewBack_{0, 0, 1};      // unit vector toward the camera (side view)
    Vector3 astInDir_{1, 0, 0};      // world direction the asteroid comes FROM (unit)
    Vector3 astPos_{};               // moon radii about the centre
    Vector3 astRot_{}, astSpin_{};
    bool astAlive_ = false;
    float coreGlow_ = 0.f;           // crater bloom alpha
    float interiorGlow_ = 0.f;       // breakup interior glow alpha
    float ventAcc_ = 0.f;

    // Models & Shaders
    Model wholeMoonModel_{};
    bool wholeMoonLoaded_ = false;

    Shader moonShader_{};
    bool moonShaderLoaded_ = false;
    Shader fireballShader_{};
    bool fireballShaderLoaded_ = false;
    Shader rockShader_{};
    bool rockShaderLoaded_ = false;
    Shader dustShader_{};
    bool dustShaderLoaded_ = false;

    Material moonMat_{};
    Material rockMat_{};
    bool matsReady_ = false;

    Texture2D moonTexture_{};        // Ryan's regolith (owned by wholeMoonModel_)
    bool texturesLoaded_ = false;
    Texture2D craterTex_{};          // baked: rgb object-space normal (craters), a albedo
    Texture2D crackTex_{};           // baked: r crack distance, g arrival, b char, a plate noise
    bool bakedLoaded_ = false;

    Texture2D softDotTexture_{};
    bool softDotLoaded_ = false;

    static constexpr int kNumShards = 40;
    std::array<MoonShard, kNumShards> shards_{};

    static constexpr int kNumRockVariants = 6;
    Model rockModels_[kNumRockVariants]{};
    Model coreModel_{};
    bool coreLoaded_ = false;
    bool rocksLoaded_ = false;

    static constexpr int kMaxChunks = 120;
    std::array<RockChunk, kMaxChunks> chunks_{};

    static constexpr int kMaxDust = 360;
    std::array<MoonParticle, kMaxDust> dust_{};
    static constexpr int kMaxSpray = 220;
    std::array<MoonParticle, kMaxSpray> spray_{};
    static constexpr int kMaxEmbers = 260;
    std::array<MoonParticle, kMaxEmbers> embers_{};
    static constexpr int kMaxRocks = 24;
    std::array<MoonRock, kMaxRocks> rocks_{};

    float nextWaveTime_ = 42.0f;
    int waveCount_ = 0;
    bool cloudSpawn_ = false;
};
