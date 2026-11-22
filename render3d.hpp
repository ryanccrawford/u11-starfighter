#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include "raylib.h"
#include "view.hpp"

enum class BulkheadPillarPhase { Approaching, CrossedCenter, Passed };

// Shared visual state for both identical bulkhead pillars. Distance transitions
// are explicit so gameplay or audio can subscribe without duplicating thresholds.
class BulkheadPillarVisual {
public:
    using Event = std::function<void(BulkheadPillarPhase)>;
    void setOpacity(float value) { opacity_ = std::clamp(value, 0.f, 1.f); }
    float opacity() const { return opacity_; }
    void updateDistance(float signedDistance, float fadeDistance, const Event& event = {});
private:
    float opacity_ = 1.f;
    int phase_ = 0;
};
#include "background.hpp"
#include "planet_render.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

// Per-instance enemy animation state passed to drawShip (all times in seconds).
struct EnemyAnim {
    int model = 0;          // Render3D::enemyModel() index
    float idleT = 0;        // Idle clip time (loops)
    float fireT = -1;       // seconds since the weapon started firing; < 0 = not firing
    bool held = false;      // weapon still active (beam/flame): Fire clip loops its sustain window
    float glow = 0;         // 0..1 extra glow on the emissive (alpha-masked) texels, e.g. a charge telegraph
    const std::map<std::string, std::string>* clips = nullptr; // optional per-entity clip names
};

// Real-time GLB ship renderer (USE_3D=1). Canonical pipeline matches shooter/3d-test.
class Render3D {
public:
    void init(const char* root);
    void shutdown();
    bool ok() const { return ok_; }

    // beginFrame opens the scene RT (when heat on). Draw 2D bg inside it, then ships,
    // then endFrame blits + heat; drawFlamesOnly adds nozzle billboards.
    void beginFrame(const View& view, Color clear, float cameraTurn = 0.f);
    void beginShips();
    void drawShip(Vector2 local, float sizePx, bool facingBack, float pitch, float roll,
                  Color tint, bool player, float flamePower, const float (*dent)[4] = nullptr, int nDent = 0,
                  const struct EnemyAnim* anim = nullptr, Color flameTint = {0, 0, 0, 0},  // flameTint alpha 0 = default flame
                  float depth = 0.f, float weaponHeat = 0.f, const float (*surfaceFx)[4] = nullptr);  // hidden-axis depth and per-hull damage look
    // Per-enemy-type GLB models (assets/models/<file>). 0 = the default/global enemy model
    // (ENEMY_MODEL or blue_racer_lod4k.glb). Loads + caches; returns 0 if the file fails.
    int enemyModel(const std::string& file);
    // Where the model's weapon muzzle (sidecar JSON "muzzle") sits in playfield-local coords.
    Vector2 muzzleLocal(int model, Vector2 shipLocal, float depth = 0.f) const;
    // Project the four player cannon barrel tips through the current gameplay camera.
    std::array<Vector2, 4> playerCannonMuzzlesLocal(const View& view, float cameraTurn,
                                                    Vector2 shipLocal, float pitch, float roll,
                                                    float depth = 0.f) const;
    // A real model-space shield shell.  It is rendered in the same 3D pass as the player,
    // rather than as a camera-facing sprite, so the ship remains inside the sphere in both views.
    void drawPlayerShield(Vector2 local, float pitch, float roll, float charge, float depth = 0.f);
    // Registers a gameplay-local shield contact. The shader turns it into a travelling arc over
    // the actual 3D sphere rather than a screen-space flash.
    void shieldImpact(Vector2 playerLocal, Vector2 contactLocal, float depth = 0.f);
    bool pickupReady() const { return pickupOk_; }
    void beginPickupBatch();
    void drawPickup(Vector2 local, int type, float sizePx, Color accent,
                    float opacity, float flash, float dissolve, float expand);
    void endPickupBatch();
    void endShips();
    void endFrame();
    void drawFlamesOnly();
    // Ryan's 3D planets + scrolling terrain (background.json planet3d / terrain3d layers). Call
    // right after the 2D background (outside Mode2D): its own Mode3D pass, behind everything else.
    void drawBackground3D(const std::vector<Background::Bg3D>& items);
    void drawShipCorridor(float elapsedSeconds, float scrollTime);
    void drawBulkhead(float worldX, float opening, float elapsedSeconds, float playerWorldX);
    // One door transform for drawing and collision (camera-relative, like the art).
    struct BulkheadPose { Vector3 center{}, up{0, 1, 0}, right{1, 0, 0}, forward{0, 0, -1}; float scale = 1.f; };
    BulkheadPose bulkheadPose(float worldX) const;
    // Gameplay position (local + hidden-axis depth) -> world, without the on-screen clamp.
    Vector3 worldAt(Vector2 local, float depth) const;
    // Gameplay position -> BulkheadModel coordinates (u across, v flight, w depth).
    Vector3 bulkheadLocal(const BulkheadPose& pose, Vector2 local, float depth) const;
    void drawTurret(Vector2 mount, Vector2 aim, float barrelAngle, float recoil, int firingBarrel, float muzzleFlash, bool destroyed);
    Vector2 turretMuzzle(Vector2 mount, Vector2 aim, float barrelAngle, float recoil, int barrelIndex) const;
    int bgTris() const { return bgTris_; }

    int shipTris() const { return shipTris_; }

    // --- damage: hull dents (vertex-shader displacement, no mesh copies) + death breakup ---
    struct BreakCfg { int dentMax = 8; float dentRadius = 0.22f, dentDepthPerDamage = 0.012f, dentMaxDepth = 0.11f, dentMerge = 0.12f, dentDarken = 0.6f;
                      int maxBreakups = 4, chunks = 6; float chunkSpeedMin = 1.6f, chunkSpeedMax = 3.6f, chunkDrag = 1.1f, chunkSpinMin = 2, chunkSpinMax = 7,
                      chunkLife = 1.3f, chunkShrinkStart = 0.55f, chunkExplosionScale = 0.55f, chunkExplosionGain = 0.4f, chunkExplosionDelayMax = 0.25f;
                      int shardsMin = 10, shardsMax = 20; float shardSizeMin = 3, shardSizeMax = 8, shardSpeedMin = 90, shardSpeedMax = 280, shardDrag = 1.6f, shardLife = 0.9f;
                      int maxShards = 500; };
    const BreakCfg& breakCfg() const { return bc_; }
    // add / deepen a dent where a shot (local coords) hit the enemy at shipLocal
    // hull-surface contact (playfield local) for a hit on a ship at shipLocal: projects the enemy
    // model's bbox through the 3D camera, so the 2D effect lands exactly on the drawn hull
    Vector2 hullContact(Vector2 shipLocal, Vector2 hitLocal, Vector2 dirLocal, int model = 0, float depth = 0.f) const;
    // Screen-space rectangle of an enemy model's projected bbox (anchors the faint health bar).
    Rectangle shipScreenRect(Vector2 shipLocal, int model = 0, float depth = 0.f) const;
    // Level 6 planet approach (see drawApproachScenery in render3d.cpp). The scenery camera shares
    // the render camera's orientation (f, u); the planet sits along pdir at dist radii; ONE sun.
    struct ApproachScene { Vector3 f{0, 0, -1}, u{0, 1, 0}, pdir{0, 0, -1}, sun{0, 1, 0}; Vector3 starF{0, 0, -1}, starU{0, 1, 0}; bool separateStarFrame = false; float fovy = 40, dist = 30, spin = 0, cloudSpin = 0;
                           float cityK = 0, stars = 1; Vector2 jitter{0, 0}; Color tint{250, 180, 255, 255};
                           Color nebulaTint{255, 255, 255, 255};
                           float sunR = 0, time = 0, starSpeed = 0, titleRevealTime = 0;
                           Matrix prot{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // Liftoff: the planet frame turned with pdir (sub-camera point fixed)
                           float cloudR = 1.012f, cloudA = 1.f, atmoK = 1.f;     // cloud sphere radius / opacity, atmosphere shell gain
                           float nebula = 0.f;             // > 0: the baked nebula / gas sky at infinity (additive, under the stars)
                           bool planet = true, titleScene = false; // titleScene adds 3D logo and full-detail player flyby
                           // title only (t228): fighter pose in camera space (x, y = screen fraction, z = depth in planet radii),
                           // heading (camera right / up / forward), roll, width (fraction of screen height), sun-only + ambient fill
                           Vector3 titleShip{0.6f, 0.64f, 0.3f}, titleShipHead{0.55f, 0.16f, 0.82f}; float titleShipRoll = 0, titleShipW = 0.4f, titleShipAmb = 0.22f;
                           bool titleAtmo = false, titleShell = true, titleGlyphs = true; };
    // flarePx / flareTanPx: where the hidden sun meets the limb (and a point a little along the limb),
    // projected with the scenery camera itself, so 2D flares sit exactly on the 3D limb.
    struct ApproachOut { Vector2 centre{-1e4f, -1e4f}; float radiusPx = 0; Vector2 sunPx{0, 0};
                         Vector2 flarePx{0, 0}, flareTanPx{0, 0}; bool flareOk = false;
                         Vector2 sunDiscPx{0, 0}; float sunRadPx = 0, sunVis = 0; bool sunOn = false;    // 3D sun on screen (sunVis: unoccluded share)
                         Vector2 titleNoz[2]{}, titleExhaust{0, 0}; float titleShipPx = 0; };             // title fighter: nozzles, exhaust dir, length (px)
    void drawApproachScenery(const ApproachScene& a, ApproachOut& out);
    void releaseTitleAssets();
    // Liftoff volumetric clouds + atmosphere: ray-marched in the scenery's planet frame (unit planet,
    // camera at -pdir*dist) at reduced resolution, then composited. Far = beyond the ship (drawn
    // before the ships), near = camera..ship (drawn over them). Render BEFORE beginFrame().
    struct VolFog { bool on = false; Vector2 deck{0.0012f, 0.0030f}; float cover = 0.62f, cloudK = 14000.f, atmoK = 240.f, Hs = 0.004f;
                    float sNear = 1.6e-4f, gain = 1.f, deckK = 1.f, nearK = 0.6f, time = 0.f; Vector3 wind{0, 0, 0};
                    bool trackPatch = false; };   // trackPatch: keep a cloud patch on the ground point under us (punch-through), frozen after
    void renderVolFog(const ApproachScene& a, const VolFog& v);
    void drawVolFog(bool nearPass, float destW, float destH) const;
    // Liftoff pull-up: the player's hull pitched nose-up (rad) in the overhead view.
    void setClimbPitch(float r) { climbPitch_ = r; }
    // Player engine plume size (Liftoff punch-out: 1.5 = 50% bigger flame, length and width).
    void setFlameBoost(float k) { flameBoost_ = k; }
    // Baked equirectangular nebula / gas / dust texture (space backdrop; 2D levels tile a band of it).
    const Texture2D& nebulaTexture() const { return nebTex_; }
    // Level 6: ships / enemies / terrain lit by the same sun (gameplay-world light travel direction).
    void setSceneLight(bool on, Vector3 lightDir = {0, 0, 0}, float metalAmb = 0.28f, float terrainAmb = 0.30f);
    // Level 6 chase / orbit camera: DRAWS ships, flames, shields and terrain from this camera while
    // every world position (toWorld, muzzles, hull contacts) still comes from the gameplay camera.
    void setRenderCamera(bool on, const Camera3D& c = Camera3D{}) { rcamOn_ = on; if (on) rcam_ = c; }
    // ---- Level 6 chunked ground (planet_terrain / planet_render) and its ground guns ----
    struct GroundGunDraw { float gx = 0, gz = 0, gy = 0, yaw = 0, pitch = 0.6f, barrelAngle = 0, recoil = 0, muzzleFlash = 0; int flashBarrel = 0; bool destroyed = false; };
    bool ensurePlanet(const std::string& model);                // loads a "chunk_terrain" descriptor; true if it is one
    const planet::Params* planetParams() const { return planet_ && planet_->ready() ? &planet_->params() : nullptr; }
    void prefetchPlanet(const std::string& model, float scrollUnits);   // build the chunks for a future scroll
    // Level start over existing terrain (Level 4 continuing Planetfall): build + upload the visible
    // ground rectangle now (bounded wait) so the first frame is complete. x/z in ground units.
    void warmPlanet(const std::string& model, float x0, float x1, float z0, float z1, float timeoutSec);
    void setGroundGuns(const std::vector<GroundGunDraw>& g) { groundGuns_ = g; }
    // Liftoff climb: push the drawn ground this many world units further from the camera (same
    // magnification), so it shrinks away under the ship. 0 = normal. Ground guns must be gone.
    void setGroundDrop(float d) { groundDrop_ = fmaxf(0.f, d); }
    static constexpr float kGroundBase = -18.f;                 // ground plane depth below the ships (as the old tiles)
    Vector2 groundLocal(float gx, float gy, float groundZ) const;   // ground point (Z = world z + scroll) -> gameplay local
    Vector2 groundGunMuzzle(const GroundGunDraw& g, float scroll, int barrel) const;
    const PlanetRenderer::Stats* planetStats() const { return planet_ ? &planet_->stats : nullptr; }
    const Camera3D& renderCamera() const { return rcamOn_ ? rcam_ : cam_; }
    Vector3 worldOf(Vector2 local, float depth = 0.f) const { return toWorld(local, depth); }
    void setProfileRoll(float r) { profile_ = r; }  // extra hull roll about the nose (approach side phase)
    int addDent(float (*dent)[4], int& n, int& next, Vector2 hitLocal, Vector2 shipLocal, float damage,
                int model = 0, float depth = 0.f, float (*surfaceFx)[4] = nullptr) const;
    // death: spawn chunks (false = pool full -> caller just explodes)
    bool spawnBreakup(Vector2 local, float pitch, float roll, const float (*dent)[4], int nDent, Color tint, int model = 0, float depth = 0.f);
    // t251: boss breakup: heavier parts thrown from the inside out (speed / spin / life multipliers), staggered
    // internal blasts up to delayMax, glowing-hot hull that cools (hot 0..1), chunk blasts scaled by blastScale.
    bool spawnBossBreakup(Vector2 local, float pitch, float roll, const float (*dent)[4], int nDent, Color tint, int model, float depth,
                          float speedMul, float spinMul, float life, float delayMax, float hot, float blastScale);
    struct ChunkBlast { Vector2 screen, dir; Color tint; float scale = 1.f; };  // a chunk's staggered explosion + shards
    // t251: per-model draw size multiplier (EnemyDef.modelScale): hull, muzzle, dents, hit box, lights, breakup.
    void setModelScale(int model, float mul) { if (model >= 0 && model < (int)em_.size()) em_[model].scaleMul = mul > 0.f ? mul : 1.f; }
    float modelScale(int model) const { return (model >= 0 && model < (int)em_.size()) ? em_[model].scaleMul : 1.f; }
    // t251: additive emissive FX inside the ships pass (batched with the ship lights, depth-tested, no depth write):
    // a soft round glow sprite, and a soft ring (shockwave / shield) in a plane tilted `tilt` rad from facing the camera.
    void addGlow(Vector2 local, float depth, float sizeWorld, Color c);
    void addRing(Vector2 local, float depth, float radiusWorld, float tilt, Color c, float spinRad = 0.f);
    void updateBreakups(float dt, std::vector<ChunkBlast>& out);
    void drawBreakups();  // inside beginShips/endShips
    void clearBreakups() { for (auto& b : breaks_) b.on = false; }
    int liveBreakups() const { int n = 0; for (auto& b : breaks_) n += b.on; return n; }

private:
    struct Plume { Vector2 nozzle; float angDeg; float unitPx; float scale; bool player; Color tint; float power = 1.f; };  // tint.a 0 = default
    Model player_{};
    ModelAnimation* playerAnims_ = nullptr;
    int playerAnimCount_ = 0;
    std::vector<int> playerGearAnims_, playerGunAnims_, playerExhaustAnims_;
    float playerAnimClock_ = 0.f;
    Model shield_{};
    static constexpr int kPickupTypes = 5;
    Model pickup_[kPickupTypes]{};
    ModelAnimation* pickupAnim_[kPickupTypes]{};
    int pickupAnimCount_[kPickupTypes]{};
    int pickupAnimFrame_[kPickupTypes] = {-1, -1, -1, -1, -1};
    BoundingBox pickupBounds_[kPickupTypes]{};
    Texture2D flameTex_{};
    Shader lit_{}, titleGlyphSh_{}, metal_{}, playerGlassSh_{}, flameSh_{}, heatSh_{}, shieldSh_{};
    Shader pickupSh_{};
    RenderTexture2D scene_{};
    int fTime_ = -1, fRecolor_ = -1, hTime_ = -1, hScreen_ = -1, shipTris_ = 0, rtW_ = 0, rtH_ = 0;
    int shieldTime_ = -1, shieldCharge_ = -1, shieldEye_ = -1, shieldHitsLoc_ = -1;
    int metalEye_ = -1, metalRoughness_ = -1, metalness_ = -1, metalEnvStrength_ = -1, metalOpacity_ = -1;
    int glassEye_ = -1, glassRoughness_ = -1, glassMetalness_ = -1, glassEnvStrength_ = -1, glassOpacity_ = -1;
    BulkheadPillarVisual bulkheadPillarVisual_{};
    bool direct_ = true, hidePlayer_ = false;
    float profile_ = 0;
    Camera3D rcam_{}; bool rcamOn_ = false;
    std::unique_ptr<PlanetRenderer> planet_;
    std::vector<GroundGunDraw> groundGuns_;
    float planetPropFar_ = 400.f;
    Vector3 sceneToSun_{-0.45f, 0.8f, 0.35f}; float sceneTerrainAmb_ = 0.30f;
    void drawPlanet(const Background::Bg3D& it, float halfH, float visHalfW, float pxPerUnit);
    void drawGroundGuns(const Matrix& layer, float scroll);
    bool sceneLight_ = false;
    bool ok_ = false, pickupOk_ = false, pickupBatch_ = false, heat_ = true, flames_ = true, inShips_ = false, inSceneRt_ = false;
    View view_{};
    Camera3D cam_{};
    float cameraTurn_ = 0.f;
    float shipScale_ = 1.8f;  // demo S (side + enemies)
    float shieldBaseRadius_ = 1.f;  // side view (accepted look): the fighter's largest half-axis + 3%
    float shieldTopRadius_ = 1.f;   // overhead view: same on-screen gap beside the hull as the side view shows above/below it
    static constexpr float kShieldTopGapMatch = 1.0f;  // 1 = top gap equals the side gap on screen; lower = tighter
    Vector4 shieldHits_[4]{{0, 0, 1, -100}, {0, 0, 1, -100}, {0, 0, 1, -100}, {0, 0, 1, -100}};
    int nextShieldHit_ = 0;
    float shieldShown_ = 0.f;  // displayed shield fraction: eases toward the real value so drops/refills animate
    // Overhead player only: half current on-screen size (shipScale_ * this). Side/enemies unchanged.
    static constexpr float kTopDownPlayerScaleMul = 0.5f;
    static constexpr float kSidePlayerScaleMul = 0.85f;  // side player ~15% smaller (Ryan)
    // The replacement starfighter is authored at a larger world-unit scale than
    // the legacy normalized player mesh.
    static constexpr float kPlayerModelScale = 0.15f;
    std::vector<Plume> plumes_;
    void animatePlayer(float weaponHeat);
    // t250: exterior lights mounted on the hulls (nav / strobe / engine / cockpit). Emissive
    // billboards only: they never light the hull (Ryan: sun-only ship lighting, no fill / rim).
    struct LightSprite { Vector3 p; float size; Color c; };
    std::vector<LightSprite> lights_;
    struct RingQ { Vector3 c; float r, tilt, spin; Color col; };   // t251: queued additive rings (drawn with the lights)
    std::vector<RingQ> rings_;
    Texture2D ringTex_{};
    Texture2D lightTex_{};
    int shipSeq_ = 0;
    bool shipLightsOn_ = true;
    struct ShipAnchors { Vector3 port{}, stbd{}, cockpit{}; bool ok = false; };
    ShipAnchors playerAnchors_{};
    static ShipAnchors findAnchors(const Model& m, Vector3 nose, Vector3 wing);
    void queueShipLights(const Matrix& M, const ShipAnchors& A, Vector3 noz0, Vector3 noz1, float S,
                         float strobeT, float thrust, Color engine, bool player);
    void drawShipLights();
    // damage
    static constexpr int kMaxChunks = 12;   // t251: the Warden breaks into 12 parts (other ships keep bc_.chunks)
    struct ChunkMesh { Mesh mesh{}; int mat = 0; int cluster = 0; };
    // One loaded enemy GLB + everything derived from it (chunks for the breakup, bbox for hits/dents,
    // nozzle + muzzle anchors and the optional Idle/Fire glTF animations from its sidecar JSON).
    struct EnemyModel {
        std::string file;
        Model m{};
        ModelAnimation* anims = nullptr; int animCount = 0, idle = -1, fire = -1;
        float sustain0 = 0, sustain1 = 0;   // Fire-anim seconds looped while a held weapon stays active
        bool skinned = false;
        std::vector<char> weaponBone;      // bone uses the Fire animation (name starts with "W_")
        std::vector<Matrix> scratch;
        std::vector<ChunkMesh> chunks;
        Vector3 pivots[kMaxChunks]{}; int nChunks = 0;
        float scaleMul = 1.f;              // t251: draw size multiplier (setModelScale)
        Vector3 bbMin{}, bbMax{};
        Vector3 noz[2] = {{-0.07f, -0.02f, -0.95f}, {0.07f, -0.02f, -0.95f}};  // blue_racer tail (-Z end)
        Vector3 muzzle{0, 0, 1.0f};
        ShipAnchors lights{};              // t250: wingtip nav lights + cockpit (from the mesh, model space)
    };
    std::vector<EnemyModel> em_;
    std::map<std::string, int> emIndex_;
    const EnemyModel& em(int i) const { return em_[(i >= 0 && i < (int)em_.size()) ? i : 0]; }
    bool loadEnemyModel(EnemyModel& E, const std::string& path);
    void animateEnemy(EnemyModel& E, const struct EnemyAnim& a);
    Shader dentSkinSh_{};
    int dsLocDents_ = -1, dsLocN_ = -1, dsLocSurfaceFx_ = -1;
    int dLocGlow_ = -1, dsLocGlow_ = -1, dLocTime_ = -1, dsLocTime_ = -1;
    struct Break { bool on = false; int model = 0; Matrix M0{}; float t = 0; Color tint{}; float dent[8][4]{}; int nDent = 0;
                   Vector3 off[kMaxChunks]{}, vel[kMaxChunks]{}, axis[kMaxChunks]{}; float ang[kMaxChunks]{}, spin[kMaxChunks]{}, delay[kMaxChunks]{};
                   bool fired[kMaxChunks]{};
                   float life = 0.f, hot = 0.f, blastScale = 1.f, shrinkStart = 0.f; };   // t251: 0 = BreakCfg defaults
    Shader dentSh_{};
    int dLocDents_ = -1, dLocN_ = -1, dLocSurfaceFx_ = -1;
    BreakCfg bc_{};
    Break breaks_[8];
    static constexpr int kMaxTerrainCraters = 32;
    struct BgModel {
        Model m{}, water{};
        Texture2D permutation{};
        Vector3 bmin{}, bmax{};
        Vector4 craters[kMaxTerrainCraters]{};
        Color low = WHITE, mid = WHITE, high = WHITE, waterColor = WHITE;
        float rMin = 1, rMax = 1;
        float gridSize = 0, heightScale = 0, frequency = 0, ridgeSharpness = 0;
        float minHeight = 0, maxHeight = 0, waterLevel = 0, waterAlpha = 0;
        int craterCount = 0, segments = 0;
        bool ok = false, proceduralTerrain = false, hasWater = false, chunked = false;
        // L6 full screen: the mirrored side copies only ever show a strip next to the centre tile, so
        // they draw edge strips of the SAME grid (same vertex positions -> identical heights / look).
        // Key = side (+1 / -1) * 1000 + cell columns from that edge.
        std::map<int, Model> strips;
    };
    Model& terrainStrip(BgModel& b, int side, int cells);
    std::map<std::string, BgModel> bgModels_;
    Shader planetSh_{}, cloudSh_{}, terrainSh_{}, terrainProcSh_{}, titlePlanetSh_{}, titleShellSh_{};
    int titlePlanetRoughnessLoc_ = -1, titlePlanetNormalScaleLoc_ = -1;
    int titlePlanetEyeLoc_ = -1, titlePlanetSunLoc_ = -1, titleShellSunLoc_ = -1;
    // Level 6 atmosphere: one sphere shell concentric with the planet, ray-marched scattering.
    Shader atmoSh_{}; Mesh atmoMesh_{}; Material atmoMat_{}; bool atmoOk_ = false;
    // Liftoff 3D sun: procedural plasma ball (ray-sphere impostor, smooth limb) with corona, and
    // animated prominence / eruption ribbons; all emissive. noiseTex_: 3D value-noise lattice.
    Shader fogUpSh_{}; int fogUpPx_ = -1;
    Shader sunSh_{}, promSh_{}, fogSh_{}; Texture2D noiseTex_{}; bool sunOk_ = false, fogOk_ = false;
    int sLocTime_ = -1, sLocAA_ = -1, sLocQ_ = -1, sLocRot_ = -1, prLocTime_ = -1;
    int fLoc_[24] = {};
    RenderTexture2D fogRt_[2] = {}; bool fogValid_ = false;
    float climbPitch_ = 0.f, flameBoost_ = 1.f;
    Vector3 patchL_{0, 1, 0}; bool patchSet_ = false;
    Texture2D nebTex_{}; Shader skySh_{}; int skyLoc_[6] = {-1, -1, -1, -1, -1, -1}; bool skyOk_ = false;
    void drawSunAndFlares(const Camera3D& c, Vector3 sunC, float sunRad, float time, Vector3 Xs, Vector3 Ys, Vector3 Zs, float rpx);
    float groundDrop_ = 0.f;
    int aLocCam_ = -1, aLocSun_ = -1, aLocRp_ = -1, aLocRa_ = -1, aLocGain_ = -1, aLocLobe_ = -1, aLocDK_ = -1;
    int pLocCol_ = -1, pLocR_ = -1, tLocTint_ = -1, tLocH_ = -1, bgTris_ = 0;
    int puLocOpacity_ = -1, puLocFlash_ = -1, puLocDissolve_ = -1, puLocAccent_ = -1;
    int tpLocPerm_ = -1, tpLocGrid_ = -1, tpLocHeight_ = -1, tpLocFreq_ = -1, tpLocRidge_ = -1;
    int tpLocCraters_ = -1, tpLocCraterCount_ = -1, tpLocRange_ = -1, tpLocTint_ = -1;
    int tpLocLow_ = -1, tpLocMid_ = -1, tpLocHigh_ = -1, tpLocWater_ = -1, tpLocWaterLevel_ = -1, tpLocIsWater_ = -1;
    std::string root_;
    BgModel& bgModel(const std::string& name);
    void loadBreakCfg(const std::string& path);
    void buildChunks(EnemyModel& E);
    void setDentUniforms(const float (*dent)[4], int n, bool skinned = false, float glow = 0,
                         const float (*surfaceFx)[4] = nullptr);
    float enemyScale() const;

    void ensureSceneRt();
    void setupCamera();
    Vector3 toWorld(Vector2 local, float depth = 0.f) const;
    Matrix makeShipMatrix(Vector3 pos, bool facingBack, float pitch, float roll, float scale) const;
    Matrix makeShipMatrix(Vector3 pos, bool facingBack, float pitch, float roll, float scale,
                          const View& poseView) const;
    Vector3 nozzleModel(int i) const;
    Vector3 enemyNozzle(int model, int i) const { return em(model).noz[i & 1]; }
};
