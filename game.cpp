#include "game.hpp"
#include "ui_theme.hpp"
#include "hit_feedback.hpp"
#include "camera_system.hpp"
#include "world_physics.hpp"
#include "bulkhead.hpp"
#include "profile.hpp"
#include "rlgl.h"
#include <algorithm>
#include "config.hpp"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cerrno>
#include <fstream>
#include <vector>
#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include "moon_level.hpp"
static std::unique_ptr<MoonLevel> g_moonLevel;

// ---- Level 6 approach helpers (timeline constants used by update + draw) ----
namespace {
Color lerpCol(Color a, Color b, float k) { k = Clamp(k, 0.f, 1.f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), (unsigned char)(a.a + (b.a - a.a) * k)}; }
float ss(float a, float b, float x) { x = Clamp((x - a) / (b - a), 0.f, 1.f); return x * x * (3.f - 2.f * x); }
float h01(int i, int salt) { uint32_t h = (uint32_t)i * 2654435761u ^ (uint32_t)salt * 40503u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return (h & 0xFFFFFF) / 16777216.f; }
// 0-20 side view + combat | 20-24 enemies disengage, hand-off to the real camera | 24-42 orbit to the
// low chase view | 42-80 chase approach | 80-100 entry burn | 100-118 clouds (camera rises from 103)
// | 118 overhead, below the clouds. These are AUTHORED timeline seconds (Game::apTime maps level time):
// Planetfall (approach_end 122, 180 s) stretches 0-100 over 0-158 s, then clouds / rise / overhead hold
// 1:1 and ends at 122 (= 180 s); Level 4 (approach_start 122) continues from 122 with terrain combat.
constexpr float kApT1 = 20.f, kApHand = 24.f, kApOrbit = 42.f, kApDesc = 80.f, kApCloud = 100.f, kApRise = 103.f, kApOver = 118.f;
constexpr float kApTerrain = 112.f;
// Liftoff (level_07): Planetfall's approach timeline run backwards. Level time -> timeline knots:
//   0..86   overhead over the ground (pad takeoff 0..5, then combat + ground guns; timeline 140 -> 135)
//   86..94  climb: the ground falls away and the sky hazes over (135 -> 118)
//   94..110 up through the cloud deck, the camera tipping from overhead to the chase view (118 -> 100)
//   110..131 out of the atmosphere, the planet below and receding, then orbit round to the side (100 -> 24)
//   131..180 side view in space beside the planet, the 3D sun ahead (24 -> 0); combat again from ~139
// Liftoff (t214): overhead to 92 s (t 118), the 3D camera from 92 to 128 s (pull-up in the deck, then the
// front camera over the receding planet, then the orbit to the side), side view (t < 24) from 128 s.
constexpr float kLiftKnots[][2] = {{0.f, 140.f}, {86.f, 135.f}, {92.f, 118.f}, {128.f, 24.f}, {180.f, 0.f}};
constexpr float kLiftPadEnd = 5.f, kLiftCombatFrom = 6.f, kLiftGunsTo = 70.f, kLiftClimb = 84.f, kLiftSide = 128.f;
constexpr float kLiftPullUp0 = 92.f, kLiftPullUp1 = 100.f, kLiftOrbit0 = 114.f;   // overhead -> front camera; front -> side
constexpr float kLiftSunR = 6.f * DEG2RAD;   // angular radius of the sun disc
// Altitude above the ground in planet radii (log-linear between knots): a near-vertical climb, the cloud
// deck (kLiftDeck) crossed at ~89-95 s, the thinning atmosphere to ~110 s, then space.
constexpr float kLiftAltKnots[][2] = {{0.f, 3e-4f}, {84.f, 3e-4f}, {88.f, 6e-4f}, {92.f, 2.1e-3f}, {95.f, 4.0e-3f}, {100.f, 0.012f},
                                       {106.f, 0.035f}, {114.f, 0.12f}, {122.f, 0.45f}, {128.f, 0.9f}, {150.f, 3.0f}, {180.f, 6.5f}};
constexpr float kLiftDeck0 = 0.0007f, kLiftDeck1 = 0.0030f;
float liftAlt(float lt) {
    constexpr int n = sizeof(kLiftAltKnots) / sizeof(kLiftAltKnots[0]);
    if (lt <= kLiftAltKnots[0][0]) return kLiftAltKnots[0][1];
    for (int i = 1; i < n; i++) if (lt <= kLiftAltKnots[i][0]) {
        const float k = (lt - kLiftAltKnots[i - 1][0]) / (kLiftAltKnots[i][0] - kLiftAltKnots[i - 1][0]);
        const float s = k * k * (3.f - 2.f * k) * 0.35f + k * 0.65f;   // C1-ish easing between knots
        return expf(logf(kLiftAltKnots[i - 1][1]) + (logf(kLiftAltKnots[i][1]) - logf(kLiftAltKnots[i - 1][1])) * s);
    }
    return kLiftAltKnots[n - 1][1];
}
float liftT(float lt) {
    constexpr int n = sizeof(kLiftKnots) / sizeof(kLiftKnots[0]);
    if (lt <= kLiftKnots[0][0]) return kLiftKnots[0][1];
    for (int i = 1; i < n; i++) if (lt <= kLiftKnots[i][0]) {
        const float k = (lt - kLiftKnots[i - 1][0]) / (kLiftKnots[i][0] - kLiftKnots[i - 1][0]);
        return kLiftKnots[i - 1][1] + (kLiftKnots[i][1] - kLiftKnots[i - 1][1]) * k;
    }
    return fmaxf(0.f, kLiftKnots[n - 1][1] - (lt - kLiftKnots[n - 1][0]) * 0.5f);
}
float liftLevelTime(float at) {
    constexpr int n = sizeof(kLiftKnots) / sizeof(kLiftKnots[0]);
    if (at >= kLiftKnots[0][1]) return kLiftKnots[0][0];
    for (int i = 1; i < n; i++) if (at >= kLiftKnots[i][1]) {
        const float k = (kLiftKnots[i - 1][1] - at) / (kLiftKnots[i - 1][1] - kLiftKnots[i][1]);
        return kLiftKnots[i - 1][0] + (kLiftKnots[i][0] - kLiftKnots[i - 1][0]) * k;
    }
    return kLiftKnots[n - 1][0];
}
// No combat from the start of the turn until we are overhead below the clouds.
constexpr float kApLullFrom = kApT1, kApLullTo = kApOver + 0.5f;
}

using namespace cfg;

namespace {
const char* pickupLetter(PickupType t) {
    switch (t) { case PickupType::Weapon: return "W"; case PickupType::Missiles: return "M"; case PickupType::Shield: return "S";
                 case PickupType::Repair: return "+"; case PickupType::Rapid: return "R"; case PickupType::Flamer: return "F"; case PickupType::Plasma: return "P"; }
    return "?";
}
Color pickupColor(PickupType t) {
    switch (t) { case PickupType::Weapon: return {120, 230, 255, 255}; case PickupType::Missiles: return ORANGE;
                 case PickupType::Shield: return {120, 160, 255, 255}; case PickupType::Repair: return GREEN; case PickupType::Rapid: return {255, 120, 220, 255};
                 case PickupType::Flamer: return {255, 120, 30, 255}; case PickupType::Plasma: return {200, 90, 255, 255}; }
    return WHITE;
}
constexpr float kPickupVisualRadius = 21.f;  // matches the 42 px rendered 3D pickup diameter
constexpr float kPickupDriftSpeed = 60.f;
constexpr float kPickupMissDuration = 0.24f;
constexpr float kPickupMissFade = 0.08f;
constexpr float kPickupCollectDuration = 0.45f;

float pickupOpacity(const Pickup& p) {
    if (p.state == PickupState::Active) return 1.f;
    if (p.state == PickupState::MissedFlash)
        return p.stateTime < kPickupMissDuration - kPickupMissFade ? 1.f :
               Clamp((kPickupMissDuration - p.stateTime) / kPickupMissFade, 0.f, 1.f);
    return Clamp(1.f - p.stateTime / kPickupCollectDuration, 0.f, 1.f);
}
float pickupFlash(const Pickup& p) {
    if (p.state != PickupState::MissedFlash) return 0.f;
    return 0.5f + 0.5f * std::cos(p.stateTime / kPickupMissDuration * 6.f * PI);
}
float pickupDissolve(const Pickup& p) {
    return p.state == PickupState::CollectedDisperse ? Clamp(p.stateTime / kPickupCollectDuration, 0.f, 1.f) : 0.f;
}
float pickupExpand(const Pickup& p) {
    return p.state == PickupState::CollectedDisperse ? 1.f + 0.20f * pickupDissolve(p) : 1.f;
}
Color pickupVisualColor(const Pickup& p) {
    Color c = pickupColor(p.type); float f = pickupFlash(p);
    return {(unsigned char)Lerp(c.r, 255, f), (unsigned char)Lerp(c.g, 255, f),
            (unsigned char)Lerp(c.b, 255, f), 255};
}

bool tapped() { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

#if defined(__linux__)
struct RawTouchPoint { int id = -1; Vector2 position{}; };

class EvdevTouchReader {
public:
    ~EvdevTouchReader() { closeDevice(); }

    bool poll(float dt) {
        retryT_ -= dt;
        if (fd_ < 0 && retryT_ <= 0) {
            retryT_ = 1.f;
            openDevice();
        }
        if (fd_ < 0) return false;

        input_event events[64];
        for (;;) {
            const ssize_t bytes = read(fd_, events, sizeof(events));
            if (bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                if (errno != ENODEV) fprintf(stderr, "TOUCH evdev read failed: %d\n", errno);
                closeDevice();
                retryT_ = 1.f;
                return false;
            }
            if (bytes == 0) {
                closeDevice();
                retryT_ = 1.f;
                return false;
            }
            const size_t count = static_cast<size_t>(bytes) / sizeof(input_event);
            for (size_t i = 0; i < count; ++i) process(events[i]);
        }
        if (resync_) resyncSlots();
        appendObservedPoints();  // Keep held action touches visible on frames with no new evdev report.
        return true;
    }

    std::vector<RawTouchPoint> takeObservedPoints() {
        std::vector<RawTouchPoint> result;
        result.swap(observedPoints_);
        return result;
    }

    std::vector<RawTouchPoint> points() const {
        std::vector<RawTouchPoint> result;
        if (fd_ < 0) return result;
        const float screenW = static_cast<float>(GetScreenWidth());
        const float screenH = static_cast<float>(GetScreenHeight());
        const bool rotateClockwise = screenW > screenH && yAxis_.maximum > xAxis_.maximum;
        for (int i = 0; i < slotCount_; ++i) {
            const Slot& slot = slots_[i];
            if (slot.id < 0 || !slot.hasX || !slot.hasY) continue;
            float x, y;
            if (rotateClockwise) {
                x = (slot.y - yAxis_.minimum) * screenW / static_cast<float>(yAxis_.maximum - yAxis_.minimum);
                y = (xAxis_.maximum - slot.x) * screenH / static_cast<float>(xAxis_.maximum - xAxis_.minimum);
            } else {
                x = (slot.x - xAxis_.minimum) * screenW / static_cast<float>(xAxis_.maximum - xAxis_.minimum);
                y = (slot.y - yAxis_.minimum) * screenH / static_cast<float>(yAxis_.maximum - yAxis_.minimum);
            }
            x = std::clamp(x, 0.f, std::max(0.f, screenW - 1.f));
            y = std::clamp(y, 0.f, std::max(0.f, screenH - 1.f));
            result.push_back({slot.id, {x, y}});
        }
        return result;
    }

private:
    struct Slot { int id = -1, x = 0, y = 0; bool hasX = false, hasY = false; };
    static constexpr int kMaxSlots = 32;
    int fd_ = -1, slotMin_ = 0, slotCount_ = 0, currentSlot_ = 0;
    float retryT_ = 0;
    input_absinfo xAxis_{}, yAxis_{};
    std::array<Slot, kMaxSlots> slots_{};
    bool resync_ = false;
    std::vector<RawTouchPoint> observedPoints_;

    static bool bitSet(const unsigned long* bits, int bit) {
        return (bits[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1UL;
    }

    bool openDevice() {
        for (int n = 0; n < 64; ++n) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/input/event%d", n);
            const int candidate = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (candidate < 0) continue;
            unsigned long evBits[(EV_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))]{};
            unsigned long absBits[(ABS_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))]{};
            unsigned long propBits[(INPUT_PROP_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))]{};
            char name[128]{};
            input_absinfo slotAxis{}, xAxis{}, yAxis{};
            const bool valid = ioctl(candidate, EVIOCGNAME(sizeof(name)), name) >= 0 &&
                ioctl(candidate, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0 &&
                ioctl(candidate, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) >= 0 &&
                ioctl(candidate, EVIOCGPROP(sizeof(propBits)), propBits) >= 0 &&
                bitSet(evBits, EV_ABS) && bitSet(absBits, ABS_MT_SLOT) &&
                bitSet(absBits, ABS_MT_TRACKING_ID) && bitSet(absBits, ABS_MT_POSITION_X) &&
                bitSet(absBits, ABS_MT_POSITION_Y) && bitSet(propBits, INPUT_PROP_DIRECT) &&
                ioctl(candidate, EVIOCGABS(ABS_MT_SLOT), &slotAxis) >= 0 &&
                ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_X), &xAxis) >= 0 &&
                ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_Y), &yAxis) >= 0;
            const int count = valid ? slotAxis.maximum - slotAxis.minimum + 1 : 0;
            if (!valid || count < 1 || count > kMaxSlots || xAxis.maximum <= xAxis.minimum || yAxis.maximum <= yAxis.minimum) {
                close(candidate);
                continue;
            }
            fd_ = candidate;
            slotMin_ = slotAxis.minimum;
            slotCount_ = count;
            currentSlot_ = 0;
            xAxis_ = xAxis;
            yAxis_ = yAxis;
            slots_.fill({});
            fprintf(stderr, "TOUCH evdev=%s slots=%d x=%d..%d y=%d..%d\n", name, slotCount_,
                    xAxis_.minimum, xAxis_.maximum, yAxis_.minimum, yAxis_.maximum);
            resyncSlots();
            return true;
        }
        return false;
    }

    void closeDevice() {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
        slotCount_ = 0;
        slots_.fill({});
        resync_ = false;
        observedPoints_.clear();
    }

    void process(const input_event& event) {
        if (event.type == EV_SYN && event.code == SYN_DROPPED) {
            slots_.fill({});
            resync_ = true;
            return;
        }
        if (resync_) {
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                resyncSlots();
                appendObservedPoints();
            }
            return;
        }
        if (event.type == EV_ABS) {
            if (event.code == ABS_MT_SLOT) {
                currentSlot_ = event.value - slotMin_;
                if (currentSlot_ < 0 || currentSlot_ >= slotCount_) currentSlot_ = -1;
            } else if (currentSlot_ >= 0 && currentSlot_ < slotCount_) {
                Slot& slot = slots_[currentSlot_];
                if (event.code == ABS_MT_TRACKING_ID) {
                    if (event.value < 0) slot = {};
                    else slot.id = event.value;  // This driver reports X/Y before the new tracking ID.
                } else if (event.code == ABS_MT_POSITION_X) {
                    slot.x = event.value; slot.hasX = true;
                } else if (event.code == ABS_MT_POSITION_Y) {
                    slot.y = event.value; slot.hasY = true;
                }
            }
        } else if (event.type == EV_SYN && event.code == SYN_REPORT) appendObservedPoints();
    }

    void appendObservedPoints() {
        const std::vector<RawTouchPoint> current = points();
        observedPoints_.insert(observedPoints_.end(), current.begin(), current.end());
    }

    void resyncSlots() {
        if (fd_ < 0 || slotCount_ <= 0) { resync_ = false; return; }
        auto readSlots = [this](unsigned code, auto member) {
            std::array<int, kMaxSlots + 1> values{};
            values[0] = static_cast<int>(code);
            if (ioctl(fd_, EVIOCGMTSLOTS(sizeof(int) * (slotCount_ + 1)), values.data()) < 0) return false;
            for (int i = 0; i < slotCount_; ++i) slots_[i].*member = values[i + 1];
            return true;
        };
        std::array<int, kMaxSlots + 1> ids{};
        ids[0] = ABS_MT_TRACKING_ID;
        const bool haveIds = ioctl(fd_, EVIOCGMTSLOTS(sizeof(int) * (slotCount_ + 1)), ids.data()) >= 0;
        if (!haveIds || !readSlots(ABS_MT_POSITION_X, &Slot::x) || !readSlots(ABS_MT_POSITION_Y, &Slot::y)) {
            slots_.fill({});
            resync_ = false;
            return;
        }
        for (int i = 0; i < slotCount_; ++i) {
            slots_[i].id = ids[i + 1];
            slots_[i].hasX = slots_[i].hasY = slots_[i].id >= 0;
        }
        resync_ = false;
    }
};
#endif
}  // namespace

// ============================================================ setup / save
Game::Game() {
    W = (float)GetScreenWidth(); H = (float)GetScreenHeight();
    s = std::min(W, H) / 600.f;
    const char* home = getenv("HOME");
    savePath = std::string(home ? home : ".") + "/.u11shooter3d_save";
    r3d.init("assets");
    if (r3d.nebulaTexture().id > 0) bg.nebulaTex = &r3d.nebulaTexture();   // baked gas / dust sky for the space levels
    assets.init("assets");
    titleFont = LoadFontEx("assets/fonts/Oxanium-variable.ttf", 96, nullptr, 0);
    if (titleFont.texture.id > 0) SetTextureFilter(titleFont.texture, TEXTURE_FILTER_BILINEAR);
    fx.init();
    vfx.init("assets");
    audio.init();
    fprintf(stderr, "AUDIO %s\n", audio.ok() ? "ready" : "unavailable");
    loadSave();
    SetMasterVolume(volume);
    reloadData();
}

Game::~Game() {
    g_moonLevel.reset();
    r3d.shutdown();
    if (rt.id) UnloadRenderTexture(rt);
    if (titleFont.texture.id > 0) UnloadFont(titleFont);
    audio.shutdown(); vfx.shutdown(); fx.shutdown(); assets.shutdown();
}

void Game::reloadData() {
    assets.reload();
    db.load("assets");
    // Per-type enemy GLBs ("model" in assets/enemies/<id>.json): load + fracture them now, not mid-level.
    for (const auto& [id, d] : db.enemies()) if (!d.presentation.model.empty()) r3d.enemyModel(d.presentation.model);
    for (const auto& [id, d] : db.enemies())   // t251: per-type 3D size (the Warden is drawn 1.5x; its collision scale matches)
        if (!d.presentation.model.empty() && d.modelScale != 1.f) r3d.setModelScale(r3d.enemyModel(d.presentation.model), d.modelScale);
    enemies.onBossEvent = [this](int ev) {   // t251: Warden phase captions
        if (ev == 1) { hudMessage = "REINFORCEMENTS INBOUND - THE WARDEN SHIELDS UP"; hudMessageColor = {120, 200, 255, 255}; hudMessageT = 3.0f; audio.play(Sfx::ShieldDeflect, 0.7f, 0.f); }
        if (ev == 2) { hudMessage = TextFormat("THE WARDEN RETURNS - PHASE %d", enemies.warden.phase + 1); hudMessageColor = {255, 120, 100, 255}; hudMessageT = 3.0f; audio.play(Sfx::PlayerHit, 0.8f, 0.f); }
    };
    enemies.muzzleOf = [this](const Enemy& e) {
        return (r3d.ok() && !e.def->presentation.model.empty()) ? r3d.muzzleLocal(r3d.enemyModel(e.def->presentation.model), e.pos, e.hiddenAxis) : e.pos; };
    // New ships never start inside the L3 bulkhead (anywhere in its thickness, whatever the leaves
    // are doing) or the turret/wreck; spawnWave moves the wave further out instead.
    enemies.spawnBlocked = [this](Vector2 pos, float hidden, float r) {
        if (doorSolid()) {
            const Vector3 m = doorModel(pos, hidden);
            const float rm = doorModelRadius(r + 12.f * s);
            if (m.y > BulkheadModel::kFaceV0 - rm && m.y < BulkheadModel::kFaceV1 + rm) return true;
        }
        return turretContact(pos, hidden, r + 20.f * s, nullptr, nullptr);
    };
    LevelDef title; title.layers = {{LayerType::Stars, "", 60, WHITE, 120, 0.5f, 1, 0.7f, 0.05f, 0, "", ""},
                                   {LayerType::Stars, "", 240, WHITE, 40, 0.5f, 1, 0.7f, 0.05f, 0, "", ""}};
    View tv; tv.pf = {0, 0, W, H}; tv.s = s;
    titleBg.build(title, assets, tv, fx.glowTexture());
}

static float g_wardenSkyTime = 0.f;   // t248: the Warden sky clock (set in updateWardenSky), handed to Moon Breakup's opening

void Game::loadSave() {
    std::ifstream f(savePath);
    int fps = 1, autoValue = 1;
    if (f) f >> unlocked >> savedStep >> highScore >> volume >> fps;
    if (f) f >> gfxMode;  // optional (older saves lack it)
    if (f) f >> autoValue;  // optional (older saves keep Auto Fire enabled)
    unsigned owned = 1u; int prim = 0;
    if (f) f >> owned;      // optional (t219): owned primary weapons bitmask (older saves: lasers only)
    if (f) f >> prim;       // optional (t219): selected primary
    int inv = 0;
    if (f) f >> inv;        // optional (t260): Options > UP/DOWN inverted
    g_invertPitch = inv != 0;
    ownedWeapons = (owned & 7u) | 1u;
    if (const char* e = getenv("OWN_WEAPONS")) ownedWeapons |= (unsigned)atoi(e) & 7u;   // test hook (not saved unless something saves)
    weapons.primary = (prim >= 0 && prim < kPrimaryCount && ((ownedWeapons >> prim) & 1u)) ? prim : kPrimaryLaser;
    if (gfxMode < 0 || gfxMode > 2) gfxMode = 0;
    applyQuality();
    showFps = fps != 0;
    autoFire = autoValue != 0;
    unlocked = std::max(1, unlocked);
}

void Game::writeSave() const {
    std::ofstream f(savePath);
    f << unlocked << ' ' << savedStep << ' ' << highScore << ' ' << volume << ' ' << (showFps ? 1 : 0) << ' ' << gfxMode << ' ' << (autoFire ? 1 : 0)
      << ' ' << ownedWeapons << ' ' << weapons.primary << ' ' << (g_invertPitch ? 1 : 0) << '\n';   // t260: + invert up/down   // t219: owned primaries + selection (appended: older builds ignore them)
}

// ============================================================ flow
void Game::newCampaign(int st) {
    freePlay = false; step = st; lives = cfg::dev.startingLives; score = 0;
    weapons.reset();
    priorWeaponHeat = 0; priorWeaponOverheat = false;
    runStep();
}

void Game::runStep() {
    const auto& camp = db.campaign();
    if (step >= (int)camp.size()) { state = State::Victory; stateTime = 0; savedStep = 0; writeSave(); return; }
    savedStep = step; writeSave();
    const CampaignStep& cs = camp[step];
    if (!cs.cutscene.empty()) { playCutscene(cs.cutscene, After::NextStep); return; }
    int li = db.levelIndex(cs.level);
    if (li < 0) { fprintf(stderr, "CAMPAIGN unknown level %s\n", cs.level.c_str()); step++; runStep(); return; }
    startLevel(li, false);
}

void Game::playCutscene(const std::string& id, After then) {
    afterCutscene = then;
    if (cutscene.load(id, assets)) { state = State::Cutscene; stateTime = 0; return; }
    // missing cutscene: continue immediately
    if (then == After::StartLevel) beginPlaying();
    else if (then == After::NextStep) { step++; runStep(); }
}

void Game::startLevel(int li, bool fp) {
    freePlay = fp;
    levelIdx = li;
    const LevelDef& L = db.levels()[li];
    if (fp) { lives = cfg::dev.startingLives; score = 0; weapons.reset(); priorWeaponHeat = 0; priorWeaponOverheat = false; }
    if (!L.intro.empty() && !getenv("NOINTRO") && !getenv("AUTOPLAY")) playCutscene(L.intro, After::StartLevel);
    else beginPlaying();
}

void Game::beginPlaying() {
    const LevelDef& L = db.levels()[levelIdx];
    view.mode = L.mode; view.s = s;
    if (L.mode == Mode::SideScroll) view.pf = {0, 0, W, H};
    else {  // the approach level turns its playfield through 90 degrees: square, so it fits both ways
        float w = std::min(W, H * (L.approach ? 1.f : kTopDownAspect)); view.pf = {(W - w) / 2, 0, w, H};
    }
    view.rot = 0; view.shake = {0, 0}; appr = {}; r3d.setProfileRoll(0);
    view.vis = (L.approach && L.mode == Mode::TopDown) ? std::max(0.f, std::ceil((W - view.pf.width) * 0.5f)) + 4.f : 0.f;
    bg.margin = view.vis;
    const float prevGroundPx = bg.terrainOffset();
    const Vector2 prevShipPos = player.pos;
    const bool carry = planetCarry && !freePlay;
    planetCarry = false;
    bg.build(L, assets, view, fx.glowTexture());
    if (L.approach && L.approachStart > 0.f) {
        // Continuation of Planetfall: the same ground, from where its scroll stopped (a direct
        // start uses the nominal end of a 180 s Planetfall at the base scroll speed).
        float speed = 0.f; std::string model;
        for (const auto& b : L.layers) if (b.type == LayerType::Terrain3D) { speed = b.speed; model = b.model; }
        const float px = carry ? prevGroundPx : speed * view.s * 180.f;
        bg.setTerrainOffset(px);
        if (!model.empty()) {
            const float pxPerUnit = fmaxf(1.f, view.pf.height) / CameraSystem::kViewHeight;
            const float halfH = CameraSystem::kViewHeight * 0.5f;
            const float visHalfW = halfH * (view.pf.width + 2.f * view.vis) / fmaxf(1.f, view.pf.height);
            const float S = px / pxPerUnit;
            r3d.warmPlanet(model, -visHalfW - 1.2f, visHalfW + 1.2f, -halfH - 1.5f - S, halfH + 1.5f - S, 4.f);
        }
        fprintf(stderr, "PLANET continue carry=%d offset=%.0fpx\n", (int)carry, px);
    }
    enemies.reset(); pickups.clear(); fx.clear(); vfx.clear(); r3d.clearBreakups();
    weapons.lasers.clear(); weapons.missiles.clear();
    player = Player{};
    player.def = &db.player(L.player);
    player.hp = player.maxHp = player.def->hp;
    // The opening level teaches the protection mechanic immediately instead of withholding it
    // until a chance drop happens to appear.
    if (L.id == "level_01") player.shield = kShieldMax;
    if (const char* e = getenv("HUD_HP")) player.hp = std::max(1, player.maxHp * atoi(e) / 100);  // HUD test only
    if (const char* e = getenv("HUD_SHIELD")) player.shield = std::clamp(atoi(e), 0, kShieldMax);
    // 3D top-down: sit a bit higher so the half-scale ship stays clear of the soft-key bar.
    const float spawnF = (L.mode == Mode::TopDown) ? 0.22f : 0.12f;
    player.pos = {view.forward() * spawnF, view.lateral() / 2};
    for (const LevelSpawnPoint& spawn : L.spawnPoints) if (spawn.id == "player") {
        const WorldProjection projection = view.mode == Mode::SideScroll ? WorldProjection::Side : WorldProjection::Top;
        const WorldPosition position{spawn.position.x * s, spawn.position.y * s, spawn.position.z * s};
        player.pos = projectWorld(position, projection);
        player.hiddenAxis = projection == WorldProjection::Side ? position.y : position.z;
        break;
    }
    if (carry && L.approach && L.approachStart > 0.f)   // same flight plane: the ship flies on from where it was
        player.pos = {Clamp(prevShipPos.x, 0.f, view.forward()), Clamp(prevShipPos.y, 0.f, view.lateral())};
    player.invuln = 2;
    controller = makeController(player.def->controller);
    combatInput = {}; secondaryWasHeld = false; movementTouchId = -1;
    levelTime = 0; waveIdx = 0; waveBase = 0; lastSpawn = 0; bossSpawned = false; banner = 0; bossDeath = 0; pickupTestSpawned = false; damageSmokeDebt = 0;
    bdx = BossDeathFx{}; zoneHits = allHits = 0; zoneTime = 0; g_autopilotHold = {-1.f, -1.f};   // t251
    if (L.id == "level_03" && getenv("BULKHEAD_REVIEW")) {
        levelTime = getenv("BULKHEAD_REVIEW_OPEN") ? 108.f : 102.f;
        waveIdx = (int)L.waves.size(); lastSpawn = levelTime;
        player.pos = {getenv("BULKHEAD_REVIEW_NEAR") ? view.forward() * .55f : view.forward() * .12f,
                      view.lateral() * (getenv("BULKHEAD_REVIEW_GAP") ? .5f : .12f)};
    }
    if (L.id == "level_03" && getenv("TURRET_REVIEW")) {
        levelTime = 62.f; waveIdx = (int)L.waves.size(); lastSpawn = levelTime;
        if (getenv("TURRET_REVIEW_SHOOT")) player.pos.y = 68.f * s;
    }
    // t219: owned primaries are a save-game property (kept for the rest of the game, every level)
    weaponGiftT = (L.id == "level_04" && !((ownedWeapons >> kPrimaryFlame) & 1u)) ? 16.f
                : (L.id == "level_07" && !((ownedWeapons >> kPrimaryPlasma) & 1u)) ? 22.f : 0.f;
        weaponNoteT = 0.f; beamSoundT = 0.f; hudMessageT = 0.f; switchWasHeld = false;
    weapons.flames.clear(); weapons.beamOn = false; weapons.flameOn = false; weapons.plasmaHeat = 0.f; weapons.plasmaLock = 0.f;
    if (L.id == "level_05" && (ownedWeapons & 7u) != 7u) {   // The Warden: every primary online
        ownedWeapons |= 7u; writeSave(); weaponNote = "ALL WEAPONS ONLINE"; weaponNoteT = 3.5f;
        fprintf(stderr, "WEAPON all online (Warden)\n");
    }
    if (const char* e = getenv("WEAPON")) selectWeapon(atoi(e), false);
    shieldStash = 0; shieldOffline = false; shieldNoteT = 0.f;
    const WardenSky prevSky = wsky;   // t248: Moon Breakup opens where The Warden ended
    wsky = {}; wsky.on = L.id == "level_05";
    if (L.id == "level_08") {
        g_moonLevel = std::make_unique<MoonLevel>();
        g_moonLevel->init(view);
        WardenSky w = prevSky;
        const bool fromWarden = w.on && r3d.ok();
        if (!fromWarden && r3d.ok()) {   // direct start (level select / START_STEP): the Warden's end pose
            wsky = {}; wsky.on = true; wsky.k = 1.f; wsky.bossAt = 0.f;
            const float lt = levelTime; levelTime = 150.f;
            updateWardenSky(0.f);
            levelTime = lt; w = wsky; wsky = {};
        }
        MoonLevel::EntrySky es;
        es.fromWarden = fromWarden && !freePlay;
        es.f = w.f; es.u = w.u; es.sun = w.sun; es.sunCam = w.sunCam; es.sunR = w.sunR; es.expo = w.expo; es.time = g_wardenSkyTime;
        if (const int wi = db.levelIndex("level_05"); wi >= 0)
            for (const BgLayer& layer : db.levels()[wi].layers) if (layer.type == LayerType::Nebula) { es.nebulaTint = layer.color; break; }
        if (r3d.ok()) g_moonLevel->setEntrySky(es, view);
    } else {
        g_moonLevel.reset();
    }
    if (L.liftoff) setupLiftoffPad();
    if (L.approach && L.approachStart <= 0.f && getenv("L6_TIME")) {  // capture hook: start the approach timeline at t seconds
        levelTime = (float)atof(getenv("L6_TIME"));
        while (waveIdx < (int)L.waves.size() && L.waves[waveIdx].time < levelTime) ++waveIdx;
        lastSpawn = levelTime;
    }
    if (L.approach) updateApproach(0);
    level3Turret = {}; level3Door = {};
    groundGuns.clear(); groundGunInit = false; groundModel.clear(); r3d.setGroundGuns({});
    cameraTurn = 0; corridorScrollTime = 0;
    state = State::Playing; stateTime = 0;
    fprintf(stderr, "LEVEL %s '%s' mode=%s controller=%s\n", L.id.c_str(), L.name.c_str(),
            L.mode == Mode::TopDown ? "top_down" : "side_scroll", controller->name());
}

void Game::finishLevel() {
    {   // t219: the level's weapon unlock is guaranteed (given at the clear if its pickup was missed)
        const std::string& id = db.levels()[levelIdx].id;
        if (id == "level_04" && !((ownedWeapons >> kPrimaryFlame) & 1u)) { ownedWeapons |= 1u << kPrimaryFlame; weaponNote = "FLAMETHROWER UNLOCKED"; weaponNoteT = 3.f; }
        if (id == "level_07" && !((ownedWeapons >> kPrimaryPlasma) & 1u)) { ownedWeapons |= 1u << kPrimaryPlasma; weaponNote = "PLASMA LANCE UNLOCKED"; weaponNoteT = 3.f; }
    }
    fprintf(stderr, "WEAPON at clear owned=%u primary=%d\n", ownedWeapons, weapons.primary);
    if (db.levels()[levelIdx].id == "level_05")   // t251: rest-zone verification
        fprintf(stderr, "SAFEZONE summary time_in_zone=%.1fs hits_in_zone=%d hits_total=%d dropped_by_filter=%d removed_at_edge=%d\n",
                zoneTime, zoneHits, allHits, enemies.warden.dropped, enemies.warden.blocked);
    planetCarry = db.levels()[levelIdx].approachEnd > 0.f;   // next: a continuation keeps the ground scroll
    unlocked = std::max(unlocked, campaignPos(levelIdx) + 2);   // opens the next level in campaign order
    highScore = std::max(highScore, score);
    writeSave();
    state = State::LevelClear; stateTime = 0;
}

void Game::updateGroundGuns(float dt) {
    const LevelDef& L = db.levels()[levelIdx];
    if (!appr.on) { if (!groundGuns.empty()) { groundGuns.clear(); r3d.setGroundGuns({}); } return; }
    float speed = 0.f;
    if (groundModel.empty()) for (const auto& b : L.layers) if (b.type == LayerType::Terrain3D) groundModel = b.model;
    for (const auto& b : L.layers) if (b.type == LayerType::Terrain3D) speed = b.speed;
    const float pxPerUnit = fmaxf(1.f, view.pf.height) / CameraSystem::kViewHeight;
    const float S = bg.terrainOffset() / pxPerUnit, t = appr.t;
    groundScroll = S;
    // Build the chunks for the 112 s terrain reveal ahead of time (worker threads), so it never pops in.
    if (!L.liftoff && t >= 86.f && t < kApTerrain) r3d.prefetchPlanet(groundModel, S + (apLevelTime(kApTerrain) - levelTime) * speed * view.s / pxPerUnit);
    if (L.liftoff && levelTime >= kLiftClimb) {   // climbing away: the ground is no longer the gameplay plane
        if (!groundGuns.empty()) { groundGuns.clear(); r3d.setGroundGuns({}); }
        return;
    }
    if (t < kApTerrain - 0.5f || !r3d.ensurePlanet(groundModel)) return;
    const planet::Params* P = r3d.planetParams();
    if (!P) return;
    const float halfH = CameraSystem::kViewHeight * 0.5f, halfW = halfH * view.pf.width / fmaxf(1.f, view.pf.height);
    // Spawn: sites just above the top edge, only in the overhead combat phase (no guns in the burn-in).
    if (L.groundGuns && t >= kApOver && (L.liftoff ? levelTime >= kLiftCombatFrom + 1.f && levelTime < kLiftGunsTo : levelTime < L.duration - 4.f)) {
        const float zTop = -halfH - S;
        if (!groundGunInit) { groundGunInit = true; groundGunNext = planet::gunCellOf(*P, zTop); }
        for (int guard = 0; guard < 8; guard++) {
            const planet::GunSite site = planet::gunSite(*P, groundGunNext);
            if (site.z < zTop - 3.f) break;                                     // not near the top yet
            groundGunNext--;
            if (site.z >= zTop - 0.6f) continue;                                // already on screen: never pop in
            if (fabsf(site.x) > halfW * 0.8f) continue;                         // outside the playfield lane
            GroundGun g; g.cell = site.cell;
            g.d.gx = site.x; g.d.gz = site.z; g.d.gy = site.y; g.d.yaw = PI * 0.5f;
            g.t.alive = g.t.spawned = true; g.t.hp = 24;
            g.t.fireCooldown = 1.0f + 1.2f * h01((int)site.cell, 7);
            groundGuns.push_back(g);
        }
    }
    const Vector3 pw = r3d.worldAt(player.pos, 0.f);
    std::vector<Render3D::GroundGunDraw> draws;
    for (GroundGun& g : groundGuns) {
        const float Z = g.d.gz + S;
        g.t.pos = r3d.groundLocal(g.d.gx, g.d.gy, Z);
        g.t.age += dt;
        stepHitBar(g.barSince, g.barAlpha, dt);
        g.t.recoil = std::max(0.f, g.t.recoil - dt * 2.4f);
        g.t.muzzleFlash = std::max(0.f, g.t.muzzleFlash - dt);
        if (g.t.alive) {
            // slew-limited traverse toward the ship's ground point; barrels elevate as it nears
            const float dx = pw.x - g.d.gx, dz = pw.z - Z, dist = sqrtf(dx * dx + dz * dz);
            const float want = atan2f(-dz, dx);
            float err = fmodf(want - g.d.yaw + 3.f * PI, 2.f * PI) - PI;
            const float step = 2.6f * dt;
            g.d.yaw += Clamp(err, -step, step);
            g.d.pitch = Clamp(atan2f(5.f, dist + 1e-3f), 0.3f, 0.95f);
            g.t.barrelAngle = fmodf(g.t.barrelAngle + dt * kTurretBarrelAngularSpeed * (g.burstLeft < 3 ? 1.f : 0.3f), 2.f * PI);
            g.d.barrelAngle = g.t.barrelAngle; g.d.recoil = g.t.recoil;
            g.muzzle = r3d.groundGunMuzzle(g.d, S, g.t.firingBarrel);
            const Vector2 toP = Vector2Subtract(player.pos, g.muzzle);
            if (Vector2LengthSqr(toP) > .001f) g.t.aim = Vector2Normalize(toP);
            const bool inside = g.t.pos.x > 30.f * s && g.t.pos.x < view.forward() - 30.f * s && g.t.pos.y > 20.f * s && g.t.pos.y < view.lateral() - 20.f * s;
            if (inside && fabsf(err) < 0.35f && t >= kApOver + 0.5f) {
                g.t.fireCooldown -= dt;
                if (g.t.fireCooldown <= 0.f) {
                    if (EnemyShot* shot = enemies.shots.spawn()) {
                        shot->pos = g.muzzle; shot->previousPos = shot->pos; shot->faction = Faction::Enemy;
                        shot->vel = Vector2Scale(g.t.aim, 300.f * s); shot->r = 6.f * s;
                        shot->dmg = 8; shot->col = {255, 90, 70, 255};
                        g.t.recoil = .20f; g.t.muzzleFlash = .075f; g.t.flashBarrel = g.t.firingBarrel;
                        audio.play(Sfx::Hit, .22f, .2f);
                    }
                    g.t.firingBarrel = (g.t.firingBarrel + kTurretBarrelCount - 1) % kTurretBarrelCount;
                    if (--g.burstLeft > 0) g.t.fireCooldown = .16f;
                    else { g.burstLeft = 3; g.t.fireCooldown = 2.2f + 1.0f * h01((int)g.cell, (int)(t * 10.f)); }
                }
            }
        } else {
            g.t.wreckTime += dt; g.t.sparkCooldown -= dt;
            if (g.t.sparkCooldown <= 0.f && Z > -halfH && Z < halfH) {
                g.t.sparkCooldown = .18f;
                fx.turretSmoke(view.toScreen(g.t.pos), 9.f * s);
                if (fmodf(g.t.wreckTime, 2.4f) < .5f) fx.sparks(view.toScreen(g.t.pos), view.dirToScreen({1, 0}), 3, {255, 176, 60, 255}, 100.f * s);
            }
        }
        g.d.muzzleFlash = g.t.muzzleFlash; g.d.flashBarrel = g.t.flashBarrel; g.d.destroyed = !g.t.alive;
        draws.push_back(g.d);
    }
    groundGuns.erase(std::remove_if(groundGuns.begin(), groundGuns.end(), [&](const GroundGun& g) { return g.d.gz + S > halfH + 4.f; }), groundGuns.end());
    r3d.setGroundGuns(draws);
    static const bool glog = getenv("GUN_LOG") != nullptr;
    if (glog) { static double lt = 0; if (GetTime() - lt > 1.0) { lt = GetTime(); for (const GroundGun& g : groundGuns) { const Vector2 sp = view.toScreen(g.t.pos); fprintf(stderr, "GUN t=%.1f cell=%ld alive=%d hp=%d local=(%.0f,%.0f) screen=(%.0f,%.0f) ground=(%.2f,%.2f,%.2f)\n", t, g.cell, g.t.alive, g.t.hp, g.t.pos.x, g.t.pos.y, sp.x, sp.y, g.d.gx, g.d.gy, g.d.gz + S); } } }
}

void Game::updateLevel3Hazards(float dt) {
    const LevelDef& L = db.levels()[levelIdx];
    if (L.id != "level_03" || view.mode != Mode::SideScroll) return;
    // The mount travels with the corridor. Its visual pose and muzzle share one aim vector.
    if (levelTime >= 62.f && !level3Turret.spawned) {
        level3Turret.spawned = true; level3Turret.alive = true; level3Turret.hp = 24;
        level3Turret.pos = {view.forward() + 80.f * s, 68.f * s};
        level3Turret.mountStartX = level3Turret.pos.x;
        level3Turret.corridorScrollAtMount = corridorScrollTime;
        level3Turret.fireCooldown = kTurretBarrelFireInterval;
    }
    if (level3Turret.spawned && level3Turret.pos.x > -180.f * s) {
        level3Turret.pos.x = level3Turret.mountedX(corridorScrollTime, view.pf.height / CameraSystem::kViewHeight);
        level3Turret.recoil = std::max(0.f, level3Turret.recoil - dt * 2.4f);
        if (level3Turret.alive) {
        level3Turret.barrelAngle = fmodf(level3Turret.barrelAngle + dt * kTurretBarrelAngularSpeed, 2.f * PI);
        level3Turret.muzzleFlash = std::max(0.f, level3Turret.muzzleFlash - dt);
        const Vector2 towardPlayer = Vector2Subtract(player.pos, level3Turret.pos);
        if (Vector2LengthSqr(towardPlayer) > .001f) level3Turret.aim = Vector2Normalize(towardPlayer);
        // Resolve aim from the actual animated muzzle, not from the mounting pedestal.
        if (r3d.ok()) for (int i = 0; i < 3; ++i) {
            const Vector2 from = r3d.turretMuzzle(level3Turret.pos, level3Turret.aim, level3Turret.barrelAngle, 0.f, level3Turret.firingBarrel);
            const Vector2 delta = Vector2Subtract(player.pos, from);
            if (Vector2LengthSqr(delta) > .001f) level3Turret.aim = Vector2Normalize(delta);
        }
        level3Turret.fireCooldown -= dt;
        if (level3Turret.fireCooldown <= 0 && level3Turret.pos.x < view.forward() - 40.f * s) {
            level3Turret.fireCooldown += kTurretBarrelFireInterval;
            if (EnemyShot* shot = enemies.shots.spawn()) {
                shot->pos = r3d.ok() ? r3d.turretMuzzle(level3Turret.pos, level3Turret.aim, level3Turret.barrelAngle,
                                                        level3Turret.recoil, level3Turret.firingBarrel) : level3Turret.pos;
                shot->previousPos = shot->pos; shot->faction = Faction::Enemy;
                shot->vel = Vector2Scale(level3Turret.aim, 330.f * s); shot->r = 6.f * s;
                shot->dmg = 10; shot->col = {255, 72, 96, 255};
                level3Turret.recoil = .20f;
                level3Turret.muzzleFlash = .075f;
                level3Turret.flashBarrel = level3Turret.firingBarrel;
                audio.play(Sfx::Hit, .25f, .2f);
                if (getenv("TURRET_REVIEW")) fprintf(stderr, "TURRET fired mount=(%.1f,%.1f) muzzle=(%.1f,%.1f) aim=(%.3f,%.3f)\n", level3Turret.pos.x, level3Turret.pos.y, shot->pos.x, shot->pos.y, level3Turret.aim.x, level3Turret.aim.y);
            }
            // The three barrels rotate toward the firing point, so the next barrel index
            // moves opposite the rotor's increasing local angle.
            level3Turret.firingBarrel = (level3Turret.firingBarrel + kTurretBarrelCount - 1) % kTurretBarrelCount;
        }
        } else {
            level3Turret.wreckTime += dt; level3Turret.sparkCooldown -= dt;
            if (level3Turret.sparkCooldown <= 0.f) {
                level3Turret.sparkCooldown = .18f;
                fx.turretSmoke(view.toScreen(level3Turret.pos), 9.f * s);
                if (fmodf(level3Turret.wreckTime, 2.4f) < .5f)
                    fx.sparks(view.toScreen(level3Turret.pos), view.dirToScreen({-1, 1}), 3, {255, 176, 60, 255}, 100.f * s);
            }
        }
    }
    // Security bulkhead cycle: warning/open/closing/hold.  The same opening is consumed by
    // rendering and collision, leaving a reachable central passage during every cycle.
    if (levelTime >= 102.f && !level3Door.spawned) {
        level3Door.spawned = true; level3Door.active = true;
        level3Door.x = getenv("BULKHEAD_REVIEW") ? view.forward() * .55f : view.forward() + 40.f * s;
        level3Door.mountStartX = level3Door.x;
        level3Door.corridorScrollAtMount = corridorScrollTime;
    }
    if (level3Door.active) {
        // Door and turret mounts use the same corridor attachment transform. Moving the
        // corridor section moves every attached hazard together.
        level3Door.x = level3Door.mountedX(corridorScrollTime, view.pf.height / CameraSystem::kViewHeight);
        const float cycle = fmodf(levelTime - 102.f, 6.f);
        level3Door.phase = cycle;
        level3Door.opening = cycle < 1.4f ? 1.f : cycle < 3.0f ? 1.f - (cycle - 1.4f) / 1.6f : cycle < 4.4f ? 0.f : (cycle - 4.4f) / 1.6f;
        if (level3Door.x < -80.f * s) level3Door.active = false;
    }
}

void Game::updateLevel(float dt) {
    const LevelDef& L = db.levels()[levelIdx];
    levelTime += dt;
    // Level 3 hazards shape the traffic around them (their own timing is unchanged): from just
    // before the turret mounts until it has scrolled away, new lanes stay below its body; the
    // corridor goes quiet for the bulkhead's warning, and while the door crosses only single-file
    // traffic is sent, down the centre of its gap (paths and divers wait until it has passed).
    const bool l3 = L.id == "level_03" && view.mode == Mode::SideScroll;
    enemies.laneMin = l3 && ((!level3Turret.spawned && levelTime >= 56.f) || turretSolid()) ? (68.f + 70.f) * s : 0.f;
    const bool doorWarning = l3 && !level3Door.spawned && levelTime >= 98.f;   // door mounts at 102 s
    const bool doorCrossing = l3 && level3Door.active;
    while (waveIdx < (int)L.waves.size() && L.waves[waveIdx].time + waveBase <= levelTime) {
        if (L.approach && appr.lull) { ++waveIdx; lastSpawn = levelTime; continue; }  // L6: no combat during the turn / burn
        // ENEMY_TEST=<id>: every wave spawns that enemy type instead (model/weapon review on any level)
        static const char* enemyTest = getenv("ENEMY_TEST");
        const EnemyDef& d = db.enemy(enemyTest && *enemyTest ? enemyTest : L.waves[waveIdx].enemy);
        const SpawnWave& authored = L.waves[waveIdx];   // stays in the LevelDef (a wave path is referenced, not copied)
        SpawnWave gapLane;
        const SpawnWave* w = &authored;
        if (doorWarning) break;
        if (doorCrossing) {
            if (d.pattern == Pattern::Path || d.pattern == Pattern::Dive || d.pattern == Pattern::Invaders) break;
            gapLane = authored; gapLane.path.clear();
            gapLane.formation = "column"; gapLane.y = .5f; gapLane.amp = 0.f;
            w = &gapLane;
        }
        ++waveIdx;
        enemies.spawnWave(*w, d, L, view);
        lastSpawn = levelTime;
    }
    if (L.duration > 0) {
        // Timed (non-boss) level: the wave script repeats until the clock runs out, then the field
        // is cleared (or given up to 8 s for stragglers) and the level completes.
        const bool spent = waveIdx >= (int)L.waves.size();
        if (spent && levelTime < L.duration - 6 && (enemies.alive() == 0 || levelTime - lastSpawn > 10)) {
            waveBase = levelTime + 2 - (L.waves.empty() ? 0 : L.waves.front().time);  // next cycle starts in 2 s
            waveIdx = 0;
        }
        if (levelTime >= L.duration && (enemies.alive() == 0 || levelTime >= L.duration + 8)) {
            fprintf(stderr, "LEVEL done t=%.1f (duration %.0f)\n", levelTime, L.duration);
            finishLevel();
        }
        return;
    }
    if (waveIdx >= (int)L.waves.size() && enemies.alive() == 0 && bossDeath <= 0) {
        if (!L.boss.empty() && !bossSpawned) {
            enemies.spawnBoss(db.enemy(L.boss), L, view);
            bossSpawned = true; banner = 2.5f;
            audio.play(Sfx::PlayerHit, 0.8f, 0);
        } else {
            finishLevel();
        }
    }
}

// ============================================================ gameplay
void Game::updateCombatInput() {
    combatInput = {};
    const float r = kActionButtonRadius * s;
    const Vector2 fire{W - kFireRight * s, H - kFireBottom * s};
    const Vector2 secondary{W - kSecondaryRight * s, H - kSecondaryBottom * s};
    bool secondaryHeld = false, switchHeld = false;
    const Vector2 swc = switchButtonPos(); const float swr = r * 0.72f;
    int touches = 0;
    int freeTouch = -1, stableMovement = -1;
#if defined(__linux__)
    static EvdevTouchReader evdevTouches;
    std::vector<RawTouchPoint> rawPoints, actionPoints;
    if (evdevTouches.poll(GetFrameTime())) {
        combatInput.mouseMovement = false;  // Ignore Termux:X11's duplicate single-pointer mouse emulation.
        rawPoints = evdevTouches.points();
        actionPoints = evdevTouches.takeObservedPoints();
        touches = static_cast<int>(rawPoints.size());
        freeTouch = -1;
        for (const RawTouchPoint& point : rawPoints) {
            if (point.id == movementTouchId) {
                combatInput.movementPositionValid = true;
                combatInput.movementPosition = point.position;
                combatInput.movementId = point.id;
                stableMovement = point.id;
            }
            else if (!CheckCollisionPointCircle(point.position, fire, r) &&
                     !CheckCollisionPointCircle(point.position, secondary, r) &&
                     !CheckCollisionPointCircle(point.position, swc, swr) && freeTouch < 0) freeTouch = point.id;
        }
        for (const RawTouchPoint& point : actionPoints) {
            if (point.id == movementTouchId) continue;
            if (CheckCollisionPointCircle(point.position, fire, r)) combatInput.primaryHeld = true;
            else if (CheckCollisionPointCircle(point.position, secondary, r)) secondaryHeld = true;
            else if (CheckCollisionPointCircle(point.position, swc, swr)) switchHeld = true;
        }
        if (!combatInput.movementPositionValid && freeTouch >= 0) {
            for (const RawTouchPoint& point : rawPoints) if (point.id == freeTouch) {
                combatInput.movementPositionValid = true;
                combatInput.movementPosition = point.position;
                combatInput.movementId = point.id;
                stableMovement = point.id;
                break;
            }
        }
        movementTouchId = stableMovement;
    } else movementTouchId = -1;
#else
    touches = GetTouchPointCount();
        for (int i = 0; i < touches; i++) {
            const Vector2 p = GetTouchPosition(i);
            const int id = GetTouchPointId(i);
            // Keep movement ownership stable when raylib reorders its touch list.
            if (id == movementTouchId) { stableMovement = i; continue; }
            if (CheckCollisionPointCircle(p, fire, r)) combatInput.primaryHeld = true;
            else if (CheckCollisionPointCircle(p, secondary, r)) secondaryHeld = true;
            else if (CheckCollisionPointCircle(p, swc, swr)) switchHeld = true;
            else if (freeTouch < 0) freeTouch = i;
        }
        if (stableMovement >= 0) {
            combatInput.movementTouch = stableMovement;
            combatInput.movementId = GetTouchPointId(stableMovement);
        }
        else {
            movementTouchId = -1;
            if (freeTouch >= 0) {
                combatInput.movementTouch = freeTouch;
                movementTouchId = GetTouchPointId(freeTouch);
                combatInput.movementId = movementTouchId;
            }
        }
#endif
#if !defined(__linux__)
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)
    ) {
        const Vector2 p = GetMousePosition();
        if (CheckCollisionPointCircle(p, fire, r)) { combatInput.primaryHeld = true; combatInput.mouseMovement = false; }
        else if (CheckCollisionPointCircle(p, secondary, r)) { secondaryHeld = true; combatInput.mouseMovement = false; }
        else if (CheckCollisionPointCircle(p, swc, swr)) { switchHeld = true; combatInput.mouseMovement = false; }
        else combatInput.movementId = -2;
    }
#endif
    combatInput.primaryHeld = combatInput.primaryHeld || IsKeyDown(KEY_SPACE);
    secondaryHeld = secondaryHeld || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    combatInput.secondaryPressed = (secondaryHeld && !secondaryWasHeld) || IsKeyPressed(KEY_LEFT_CONTROL) || IsKeyPressed(KEY_RIGHT_CONTROL);
    secondaryWasHeld = secondaryHeld;
    // t219: primary weapon switch (every level; inert until two or more primaries are owned)
    if (state == State::Playing && !paused) {
        if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_E) || IsKeyPressed(KEY_TAB)) showKeyboardHints = true;
        if (touches > 0) showKeyboardHints = false;
        if ((switchHeld && !switchWasHeld) || IsKeyPressed(KEY_E) || IsKeyPressed(KEY_TAB)) cycleWeapon(1);
        if (IsKeyPressed(KEY_Q)) cycleWeapon(-1);
        if (IsKeyPressed(KEY_ONE)) selectWeapon(kPrimaryLaser, true);
        if (IsKeyPressed(KEY_TWO)) selectWeapon(kPrimaryFlame, true);
        if (IsKeyPressed(KEY_THREE)) selectWeapon(kPrimaryPlasma, true);
        // t260: X flips UP/DOWN (only where up/down is pitch: the trench run); saved like the Options toggle
        bool flipX = IsKeyPressed(KEY_X);
        if (const char* e = getenv("INVERT_TEST_AT")) {   // hidden test runs: virtual X presses at these level times
            static int next = 0; float at[8]; int n = 0; const char* q = e;
            while (*q && n < 8) { at[n++] = (float)atof(q); while (*q && *q != ',') q++; if (*q == ',') q++; }
            if (next < n && levelTime >= at[next]) { next++; flipX = true; }
        }
        if (flipX && g_moonLevel) {
            g_invertPitch = !g_invertPitch; writeSave();
            const char* m = g_invertPitch ? "UP/DOWN: INVERTED" : "UP/DOWN: NORMAL";
            g_moonLevel->toast(m);
            fprintf(stderr, "INVERT toggled by X -> %d (lt=%.1f, saved)\n", g_invertPitch ? 1 : 0, levelTime);
        }
    }
    switchWasHeld = switchHeld;
    if (getenv("TOUCHDBG")) {
        static float logT = 0; logT += GetFrameTime();
        if (logT >= 0.5f) {
            logT = 0;
            fprintf(stderr, "TOUCH n=%d moveId=%d moveIndex=%d fire=%d secondary=%d", touches,
                    movementTouchId, combatInput.movementTouch, combatInput.primaryHeld, secondaryHeld);
#if defined(__linux__)
            for (const RawTouchPoint& point : rawPoints)
                fprintf(stderr, " [id=%d x=%.0f y=%.0f]", point.id, point.position.x, point.position.y);
#endif
            fputc('\n', stderr);
        }
    }
}

void Game::updatePlayer(float dt) {
    controller->update(player, dt, view, {combatInput.movementTouch, combatInput.movementId, combatInput.mouseMovement,
                                          combatInput.movementPositionValid, combatInput.movementPosition});
    cameraTurn = Lerp(cameraTurn, (float)player.edgeHold, fminf(1.f, 2.5f * dt));
        {   // 3D attitude
            float vy = Clamp(player.vel.y / (650 * s), -1.f, 1.f);
            float tgtPitch = (view.mode == Mode::SideScroll ? -vy : 0) * 10 * DEG2RAD;  // -vy: climb (y decreases) → nose up
            float tgtRoll = -vy * 25 * DEG2RAD;
            float rollRate = 6;
            if (view.mode == Mode::TopDown) {  // bank into the turn: right = right wing down (negative roll)
                // Driven by the actual frame-to-frame lateral position delta (works for drag, keys, autoplay).
                static Vector2 lastPos = player.pos; static float latV = 0, fwdV = 0;
                float lv = (player.pos.y - lastPos.y) / fmaxf(dt, 1e-4f);
                float fv = (player.pos.x - lastPos.x) / fmaxf(dt, 1e-4f);  // local +x = forward (screen up)
                lastPos = player.pos;
                latV = Lerp(latV, lv, fminf(1.f, 12 * dt));
                fwdV = Lerp(fwdV, fv, fminf(1.f, 12 * dt));
                const float ref = player.def->speed * s * 0.6f;
                float vl = Clamp(latV / ref, -1.f, 1.f), vf = Clamp(fwdV / ref, 0.f, 1.f);  // forward only
                tgtRoll = -vl * 32 * DEG2RAD; rollRate = 9;
                // Top-down "pitch" slot carries a small nose YAW, only for diagonal forward motion (NE/NW):
                // + = nose toward screen right. Pure E/W and backward motion: nose stays straight up.
                tgtPitch = vl * vf * 9 * DEG2RAD;
                static const char* bt = getenv("BANK_TEST");  // test: L / R pure sideways, NE / NW diagonal
                if (bt) {
                    bool right = bt[0] == 'R' || (bt[0] == 'N' && bt[1] == 'E');
                    tgtRoll = (right ? -28.f : 28.f) * DEG2RAD;
                    tgtPitch = bt[0] == 'N' ? (right ? 9.f : -9.f) * DEG2RAD : 0;
                }
                if (getenv("BANK_LOG")) { static float lt = 0; lt += dt; if (lt > 0.25f) { lt = 0; fprintf(stderr, "BANK roll=%.1f yaw=%.1f latV=%.0f fwdV=%.0f\n", pRoll * RAD2DEG, pPitch * RAD2DEG, latV, fwdV); } }
            }
            pPitch = Lerp(pPitch, tgtPitch, fminf(1.f, 6 * dt));
            pRoll = Lerp(pRoll, tgtRoll, fminf(1.f, rollRate * dt));
        }
    player.invuln = std::max(0.f, player.invuln - dt);
    player.flash = std::max(0.f, player.flash - dt);
    {   // aim follows the live 3D nose pitch in side view; pitch is nose-up in y-up 3D while
        // local gameplay +y is screen-down, so invert for weapons
        const float aim = view.mode == Mode::SideScroll ? pPitch : 0.f;
        const bool primary = autoFire || combatInput.primaryHeld || getenv("AUTOPLAY") != nullptr ||
                             (db.levels()[levelIdx].id == "level_03" && (getenv("BULKHEAD_REVIEW") != nullptr || getenv("TURRET_REVIEW_SHOOT") != nullptr));
        const bool moonIntro = db.levels()[levelIdx].id == "level_08" && levelTime < MoonLevel::kTrenchStart;
        static const bool holdFire = getenv("HOLD_FIRE") != nullptr;  // capture hook: pilot never shoots
        bool secondary = combatInput.secondaryPressed && !holdFire && !appr.cam3d && !appr.padLock && !moonIntro;
        if (getenv("TURRET_REVIEW_MISSILE")) {
            static bool launched = false;
            if (!launched && level3Turret.alive && level3Turret.pos.x < view.forward() * .65f) {
                launched = true; secondary = true;
                fprintf(stderr, "TURRET missile review launch\n");
            }
        }
        ProjectileTarget turretTarget{level3Turret.pos, Faction::Enemy, level3Turret.alive};
        if (!level3Turret.alive) {   // Level 6: missiles may home on the nearest on-screen ground gun
            float bestD = 1e18f;
            for (const GroundGun& g : groundGuns) {
                if (!g.t.alive || g.t.pos.x < 0.f || g.t.pos.x > view.forward() || g.t.pos.y < 0.f || g.t.pos.y > view.lateral()) continue;
                const float d = Vector2DistanceSqr(g.t.pos, player.pos);
                if (d < bestD) { bestD = d; turretTarget = {g.t.pos, Faction::Enemy, true}; }
            }
        }
        static const float autoWeapon = getenv("AUTOWEAPON") ? (float)atof(getenv("AUTOWEAPON")) : 0.f;   // capture hook: cycle owned primaries
        if (autoWeapon > 0.f) { static float acc = 0.f; acc += dt; if (acc >= autoWeapon) { acc = 0.f; cycleWeapon(1); } }
        weapons.update(dt, player.pos, enemies.list, view, fx, audio, primary && !holdFire && !appr.cam3d && !appr.padLock && !moonIntro, secondary, -aim,
                       turretTarget.active ? &turretTarget : nullptr);
        {   // plasma lance overheat / recovered cues (same designer sounds as the laser heat)
            static bool lockWas = false;
            // (t235: the lockout alarm / vent / cooling hiss are the lance's own now: Audio::plasmaLance)
            if (lockWas && weapons.plasmaLock <= 0) audio.play(Sfx::WeaponCooldown, 0.65f, 0.02f);
            lockWas = weapons.plasmaLock > 0;
        }
        if (!priorWeaponOverheat && weapons.overheat > 0) audio.play(Sfx::WeaponOverheat, 0.8f, 0.02f);
        if (priorWeaponOverheat && weapons.overheat <= 0 && priorWeaponHeat > 0.2f) audio.play(Sfx::WeaponCooldown, 0.65f, 0.02f);
        priorWeaponHeat = weapons.heat;
        priorWeaponOverheat = weapons.overheat > 0;
    }
    if (player.hp < player.maxHp * 0.3f) {  // damaged: fixed particles/second, independent of PC frame rate
        damageSmokeDebt += 22.5f * dt;  // preserves the previous 1-in-4 cadence at the tablet's 90 FPS cap
        const int count = std::min(2, (int)damageSmokeDebt);
        damageSmokeDebt -= count;
        for (int i = 0; i < count; ++i) fx.smoke(view.toScreen(player.pos), 14 * s);
    } else damageSmokeDebt = 0;
}

void Game::damagePlayer(int dmg, Vector2 at) {
    if (player.invuln > 0 || getenv("GODMODE") || getenv("AUTOPLAY")) return;
    int absorbed = std::min(player.shield, dmg);
    player.shield -= absorbed; dmg -= absorbed;
    player.hp -= dmg;
    player.flash = 0.12f; player.invuln = kInvulnTime;
    shake = std::min(kShakeMax, shake + 0.12f + (absorbed + dmg) / 60.f);
    fx.sparks(view.toScreen(at), view.dirToScreen({-1, 0}), 14, absorbed ? SKYBLUE : ORANGE, 300 * s);
    audio.play(Sfx::PlayerHit, 0.7f);
    if (player.hp <= 0) {
        fx.explosion(view.toScreen(player.pos), 1.8f * s, SKYBLUE);
        audio.play(Sfx::BigExplosion);
        engineMute = 1.2f;  // engine fades out with the ship, back after the respawn
        shake = kShakeMax;
        if (--lives <= 0) {
            highScore = std::max(highScore, score); writeSave();
            state = State::GameOver; stateTime = 0;
        } else {  // respawn
            // Return to the level's safe entry lane instead of leaving the ship inside the
            // hazard that killed it.  The invulnerability window then gives the player a
            // deterministic, visible respawn rather than an immediate second collision.
            player.pos = {view.forward() * ((view.mode == Mode::TopDown) ? 0.22f : 0.12f), view.lateral() * 0.5f};
            for (const LevelSpawnPoint& spawn : db.levels()[levelIdx].spawnPoints) {
                if (spawn.id != "player") continue;
                const WorldPosition position{spawn.position.x * s, spawn.position.y * s, spawn.position.z * s};
                const WorldProjection projection = view.mode == Mode::SideScroll ? WorldProjection::Side : WorldProjection::Top;
                player.pos = projectWorld(position, projection);
                player.hiddenAxis = projection == WorldProjection::Side ? position.y : position.z;
                break;
            }
            player.vel = {};
            player.hiddenAxis = Clamp(player.hiddenAxis, -CameraSystem::kMaxDepthWorld, CameraSystem::kMaxDepthWorld);
            player.hp = player.maxHp; player.shield = 0; player.invuln = 2.5f;
            player.flash = 0;
            weapons.level = std::max(1, weapons.level - 1);
        }
    }
}

static void enemyAttitude(const Enemy& e, Mode m, float s, float& ep, float& er) {
    const float vy = Clamp(e.vel.y / (200 * s), -1.f, 1.f);
    if (m == Mode::SideScroll) { ep = -vy * 8 * DEG2RAD; er = -vy * 15 * DEG2RAD; }
    else { ep = 0; er = -vy * 20 * DEG2RAD; }
}

float Game::shipPx(float scale) const {
    const float enemyMul = view.mode == Mode::SideScroll ? kSideEnemyScaleMul : kTopDownEnemyScaleMul;
    return kEnemySize * scale * s * enemyMul * 1.4f;  // x1.4: visible hull length
}

void Game::killEnemy(Enemy& e, bool award) {
    vfx.detach(&e);  // the explosion replaces any impacts riding this ship
    const EnemyDef& d = *e.def;
    e.alive = false;
    if (award) score += d.score;
    if (d.boss) {  // t251: the full death sequence (updateBossDeath); helpers and bullets go with it
        fx.text(view.toScreen(e.pos), TextFormat("+%d", d.score), GOLD, CameraSystem::depthScale(view, e.hiddenAxis));
        startBossDeath(e);
        enemies.list.each([&](Enemy& o) { if (o.alive && &o != &e && !o.def->boss) killEnemy(o, award); });
        enemies.shots.clear();
        return;
    }
    // Explosion sized to the ship as drawn at its depth.
    vfx.explode(view.toScreen(e.pos), shipPx(d.scale) * CameraSystem::depthScale(view, e.hiddenAxis));
    {   // break the hull into chunks (each explodes + throws shards); pool full -> explosion only
        float ep, er; enemyAttitude(e, view.mode, s, ep, er);
        r3d.spawnBreakup(e.pos, ep, er, e.dent, e.nDent, d.tint, r3d.enemyModel(d.presentation.model), e.hiddenAxis);
    }
    shake = std::min(kShakeMax, shake + 0.08f * d.scale);
    audio.play(Sfx::EnemyExplosion, 0.6f);  // designer JSON; 4-voice cap + 0.6 gain so pile-ups don't clip
    if (award) dropPickup(e);
}

// ============================================================ t251: The Warden's death
namespace { constexpr float kBossChain = 2.4f, kBossSequence = 7.0f; }

void Game::startBossDeath(Enemy& e) {
    bdx = BossDeathFx{};
    bdx.on = true; bdx.def = e.def; bdx.pos = e.pos; bdx.vel = Vector2Scale(e.vel, 0.4f); bdx.depth = e.hiddenAxis;
    enemyAttitude(e, view.mode, s, bdx.pitch, bdx.roll);
    bdx.nDent = std::min(e.nDent, 8); bdx.dentNext = e.dentNext;
    for (int i = 0; i < 8; i++) for (int k = 0; k < 4; k++) bdx.dent[i][k] = e.dent[i][k];
    bdx.model = r3d.enemyModel(e.def->presentation.model); bdx.tint = e.def->tint; bdx.engine = e.def->engineTint;
    bossDeath = kBossSequence; bossDeathPos = e.pos;
    audio.play(Sfx::BigExplosion, 0.9f); shake = std::max(shake, 0.6f);
    fprintf(stderr, "WARDEN destroyed t=%.2f pos=(%.0f,%.0f)\n", levelTime, e.pos.x, e.pos.y);
}

// Real-time hit-stop, then slow motion easing back to full speed; a second short freeze at the hull break.
float Game::bossTimeScale(float rawDt) {
    bdx.real += rawDt;
    if (bdx.real < 0.16f) return 0.03f;
    if (bdx.breakReal >= 0.f && bdx.real - bdx.breakReal < 0.09f) return 0.05f;
    const float k = Clamp((bdx.real - 0.16f) / 1.2f, 0.f, 1.f);
    return Lerp(0.25f, 1.f, k * k * (3.f - 2.f * k));
}

void Game::updateBossDeath(float dt) {
    if (!bdx.on) return;
    const float t0 = bdx.t; bdx.t += dt; const float t = bdx.t;
    if (bossDeath > 0.f) bossDeath = std::max(0.f, bossDeath - dt);
    const EnemyDef& d = *bdx.def;
    const float half = shipPx(d.scale) * 0.5f, ds = CameraSystem::depthScale(view, bdx.depth);
    if (!bdx.broke) {
        // the dying hull drifts, sags and rolls while explosions tear through it, faster and bigger
        bdx.pos = Vector2Add(bdx.pos, Vector2Scale(bdx.vel, dt));
        bdx.vel = Vector2Scale(bdx.vel, std::exp(-0.8f * dt)); bdx.vel.y += 14.f * s * dt;
        bdx.rollV += 0.25f * dt; bdx.roll += bdx.rollV * dt; bdx.pitch -= 0.03f * dt;
        while (bdx.popT <= t && bdx.popT < kBossChain) {
            const float kk = Clamp(bdx.popT / kBossChain, 0.f, 1.f);
            const Vector2 p = Vector2Add(bdx.pos, {frand(-0.8f, 0.8f) * half, frand(-0.16f, 0.16f) * half});
            const Vector2 sp = view.toScreen(p);
            fx.explosion(sp, Lerp(0.7f, 1.7f, kk) * s * ds, GetRandomValue(0, 2) ? Color{255, 170, 70, 255} : Color{255, 235, 190, 255});
            fx.sparks(sp, view.dirToScreen(Vector2Normalize({frand(-1, 1), frand(-1, 1)})), 10, {255, 220, 150, 255}, 420 * s, 1.2f);
            if (bdx.pops % 3 == 0) vfx.explode(sp, shipPx(1) * Lerp(0.6f, 1.3f, kk) * ds, 0.7f);
            r3d.addDent(bdx.dent, bdx.nDent, bdx.dentNext, p, bdx.pos, 14.f, bdx.model, bdx.depth);
            audio.play(Sfx::Explosion, 0.5f + 0.4f * kk);
            shake = std::min(1.0f, shake + 0.06f + 0.06f * kk);
            bdx.flash = std::max(bdx.flash, 0.10f + 0.15f * kk);
            bdx.pops++;
            bdx.popT += Lerp(0.20f, 0.05f, kk);
        }
        if (t >= kBossChain) {   // the hull gives way: parts burst outward from the inside, giant fireball, shockwave
            bdx.broke = true; bdx.breakReal = bdx.real;
            const Vector2 sp = view.toScreen(bdx.pos);
            auto burst = [&]() { return r3d.spawnBossBreakup(bdx.pos, bdx.pitch, bdx.roll, bdx.dent, bdx.nDent, bdx.tint, bdx.model, bdx.depth,
                                                             2.2f, 0.35f, 5.5f, 1.4f, 1.f, 2.2f); };
            if (!burst()) { r3d.clearBreakups(); burst(); }
            vfx.explode(sp, shipPx(d.scale) * 2.4f * ds, 1.f);
            vfx.explode(sp, shipPx(d.scale) * 1.3f * ds, 1.f);
            fx.explosion(sp, 7.f * s * ds, {255, 235, 190, 255});
            fx.explosion(sp, 5.f * s * ds, {255, 150, 60, 255});
            for (int i = 0; i < 4; i++)
                fx.explosion(view.toScreen(Vector2Add(bdx.pos, {frand(-0.7f, 0.7f) * half, frand(-0.25f, 0.25f) * half})), 3.f * s * ds, ORANGE);
            for (int i = 0; i < 16; i++) {
                const float a = i * 2.f * PI / 16.f + frand(-0.1f, 0.1f);
                fx.sparks(sp, {std::cos(a), std::sin(a)}, 12, {255, 205, 130, 255}, frand(700, 1100) * s, 1.6f);
            }
            audio.play(Sfx::BossBoom, 1.f, 0.02f);
            audio.play(Sfx::BigExplosion, 1.f);
            shake = 1.4f; bdx.flash = 1.f;
            fprintf(stderr, "WARDEN break t=%.2f chunks=%d\n", levelTime, r3d.liveBreakups());
        }
    } else {
        const float u = t - kBossChain, u0 = t0 - kBossChain;
        shake = std::max(shake, 1.3f * std::exp(-u / 1.0f));
        if (u < 1.6f && std::floor(u0 / 0.18f) != std::floor(u / 0.18f)) {   // secondary blasts racing outward with the debris
            const float a = frand(0, 2 * PI), r = (0.5f + u) * half;
            fx.explosion(view.toScreen(Vector2Add(bdx.pos, {std::cos(a) * r, std::sin(a) * r * 0.6f})), 1.6f * s * ds, ORANGE);
            audio.play(Sfx::Explosion, 0.45f);
        }
        if (!bdx.bonus && u >= 0.9f) {
            bdx.bonus = true; score += 25000;
            fx.text(view.toScreen(bdx.pos), "+25000", GOLD, 2.f);
            audio.play(Sfx::WeaponPickup, 0.9f, 0.f);
            fprintf(stderr, "WARDEN bonus +25000 score=%d\n", score);
        }
    }
    bdx.flash = std::max(0.f, bdx.flash - 2.6f * dt);
    if (t > 12.f) bdx.on = false;
}

void Game::drawBossDeath3D() const {
    if (!bdx.on) return;
    const float t = bdx.t;
    if (!bdx.broke) {
        const float k = Clamp(t / kBossChain, 0.f, 1.f), amp = (2.f + 9.f * k) * s;
        const Vector2 p{bdx.pos.x + std::sin(t * 53.f) * amp, bdx.pos.y + std::cos(t * 41.f) * amp * 0.6f};
        EnemyAnim a; a.model = bdx.model; a.idleT = t;
        a.glow = 0.5f + 0.5f * std::pow(std::fabs(std::sin(t * (8.f + 22.f * k))), 2.f);
        r3d.drawShip(p, 0.f, true, bdx.pitch, bdx.roll, bdx.flash > 0.3f ? WHITE : bdx.tint, false, 0.8f, bdx.dent, bdx.nDent, &a, bdx.engine, bdx.depth);
        r3d.addGlow(p, bdx.depth, 1.5f + 3.5f * k, Color{255, 170, 80, (unsigned char)(60 + 120 * k)});   // fire inside the hull
        return;
    }
    const float u = t - kBossChain;
    auto A = [](float x) { return (unsigned char)std::lround(Clamp(x, 0.f, 1.f) * 255.f); };
    // 3D shockwave: a tilted ring racing out past the screen edges, a second slower one, a soft face-on ring
    r3d.addRing(bdx.pos, bdx.depth, 1.5f + 25.f * (1.f - std::exp(-u * 1.5f)), 1.05f, {255, 205, 140, A(std::pow(Clamp(1.f - u / 2.4f, 0.f, 1.f), 1.4f))});
    if (u > 0.15f) r3d.addRing(bdx.pos, bdx.depth, 1.f + 18.f * (1.f - std::exp(-(u - 0.15f) * 1.3f)), 1.25f, {255, 140, 70, A(0.75f * (1.f - (u - 0.15f) / 2.6f))}, 0.3f);
    r3d.addRing(bdx.pos, bdx.depth, 2.f + 20.f * (1.f - std::exp(-u * 2.2f)), 0.f, {255, 235, 205, A(0.55f * (1.f - u / 1.8f))});
    r3d.addGlow(bdx.pos, bdx.depth, 5.f + 12.f * (1.f - std::exp(-u * 5.f)), {255, 215, 160, A(std::exp(-u / 0.9f))});   // fireball core
    r3d.addGlow(bdx.pos, bdx.depth, 10.f, {255, 160, 80, A(0.45f * std::exp(-u / 3.5f))});                               // lingering glow
}

void Game::drawBossOverlay() const {
    if (enemies.safeOn && enemies.boss() && !bdx.on) {   // the rest zone: four faint corner marks
        const Rectangle R = enemies.safeRect;
        const Vector2 a = view.toScreen({R.x, R.y}), b = view.toScreen({R.x + R.width, R.y + R.height});
        const float x0 = std::min(a.x, b.x) + 8 * s, x1 = std::max(a.x, b.x) - 8 * s, y0 = std::min(a.y, b.y) + 8 * s, y1 = std::max(a.y, b.y) - 8 * s;
        const float L = 34 * s, th = 2.f * s;
        const Color c = Fade({120, 220, 255, 255}, 0.24f + 0.08f * std::sin(time * 2.f));
        DrawRectangleV({x0, y0}, {L, th}, c); DrawRectangleV({x0, y0}, {th, L}, c);
        DrawRectangleV({x1 - L, y0}, {L, th}, c); DrawRectangleV({x1 - th, y0}, {th, L}, c);
        DrawRectangleV({x0, y1 - th}, {L, th}, c); DrawRectangleV({x0, y1 - L}, {th, L}, c);
        DrawRectangleV({x1 - L, y1 - th}, {L, th}, c); DrawRectangleV({x1 - th, y1 - L}, {th, L}, c);
        DrawText("REST ZONE", (int)(x0 + 10 * s), (int)(y0 + 8 * s), (int)(16 * s), Fade(c, 0.9f));
    }
    if (!bdx.on) return;
    if (bdx.flash > 0.01f) {   // additive white-hot flash (brightens, never darkens)
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangle(0, 0, (int)W, (int)H, Fade({255, 225, 180, 255}, 0.42f * bdx.flash * bdx.flash));
        EndBlendMode();
    }
    if (bdx.bonus) {
        const float u = bdx.t - kBossChain - 0.9f;
        const float a = Clamp(u / 0.35f, 0.f, 1.f) * Clamp((kBossSequence - 0.2f - bdx.t) / 0.8f, 0.f, 1.f);
        if (a > 0.01f) {
            const float pop = 1.f + 0.25f * std::exp(-u * 6.f);
            const int f1 = (int)(64 * s * pop), f2 = (int)(34 * s);
            const char* t1 = "THE WARDEN DESTROYED"; const char* t2 = "+25000 BOSS BONUS";
            const int w1 = MeasureText(t1, f1), w2 = MeasureText(t2, f2);
            DrawText(t1, (int)(W / 2 - w1 / 2 + 3 * s), (int)(H * 0.30f + 3 * s), f1, Fade(BLACK, 0.5f * a));
            DrawText(t1, (int)(W / 2 - w1 / 2), (int)(H * 0.30f), f1, Fade({255, 214, 90, 255}, a));
            DrawText(t2, (int)(W / 2 - w2 / 2), (int)(H * 0.30f + f1 + 14 * s), f2, Fade({255, 240, 200, 255}, a));
        }
    }
}

void Game::dropPickup(const Enemy& e) {
    const EnemyDef& d = *e.def;
    if (frand(0, 1) > d.dropChance) return;
    const LevelDef& level = db.levels()[levelIdx];
    bool heavyAhead = false;
    for (int i = waveIdx; i < (int)level.waves.size(); ++i) {
        const float until = level.waves[i].time + waveBase - levelTime;
        if (until < 0.f) continue;
        if (until > 22.f) break;
        const EnemyDef& next = db.enemy(level.waves[i].enemy);
        if (next.boss || next.hp >= 8 || next.shots >= 3) { heavyAhead = true; break; }
    }
    // Needs take priority over random drops: repair is rare, but never withheld
    // when the hull is genuinely low; shields are capped and only fill an empty bar.
    if (player.hp <= player.maxHp * .30f && frand(0, 1) < .78f) {
        if (Pickup* p = pickups.spawn()) { p->pos = e.pos; p->type = PickupType::Repair; }
        return;
    }
    if (player.shield == 0 && frand(0, 1) < .16f) {
        if (Pickup* p = pickups.spawn()) { p->pos = e.pos; p->type = PickupType::Shield; }
        return;
    }
    if (heavyAhead && weapons.level < kMaxWeaponLevel && frand(0, 1) < .70f) {
        if (Pickup* p = pickups.spawn()) { p->pos = e.pos; p->type = PickupType::Weapon; }
        return;
    }
    if (heavyAhead && weapons.level <= 1 && weapons.rapid <= 0 && frand(0, 1) < .28f) {
        if (Pickup* p = pickups.spawn()) { p->pos = e.pos; p->type = PickupType::Rapid; }
        return;
    }
    float total = 0;
    for (auto& dr : d.drops) total += dr.weight;
    PickupType t = (PickupType)GetRandomValue(0, 4);
    if (total > 0) {
        float r = frand(0, total);
        for (auto& dr : d.drops) {
            if ((r -= dr.weight) > 0) continue;
            t = dr.type == "weapon" ? PickupType::Weapon : dr.type == "missiles" ? PickupType::Missiles : dr.type == "shield" ? PickupType::Shield
              : dr.type == "repair" ? PickupType::Repair : PickupType::Rapid;
            break;
        }
    }
    if (Pickup* p = pickups.spawn()) { p->pos = e.pos; p->type = t; }
}

void Game::applyPickup(PickupType t) {
    const char* msg = "";
    switch (t) {
        case PickupType::Weapon:
            if (weapons.level < kMaxWeaponLevel) { weapons.level++; msg = weapons.level == 2 ? "DOUBLE LASER" : "SPREAD LASER"; }
            else { score += 500; msg = "+500"; }
            break;
        case PickupType::Missiles: weapons.ammo = std::min(kMissileMaxAmmo, weapons.ammo + 6); msg = "MISSILES +6"; break;
        case PickupType::Shield: player.shield = std::min(kShieldMax, player.shield + kShieldPickup); msg = "SHIELD"; break;
        case PickupType::Repair: player.hp = std::min(player.maxHp, player.hp + kRepairAmount); msg = "REPAIR"; break;
        case PickupType::Rapid: weapons.rapid = kRapidTime; msg = "RAPID FIRE"; break;
        case PickupType::Flamer: grantWeapon(kPrimaryFlame); msg = "FLAMETHROWER"; break;
        case PickupType::Plasma: grantWeapon(kPrimaryPlasma); msg = "PLASMA LANCE"; break;
    }
    hudMessage = msg; hudMessageT = 1.7f; hudMessageColor = pickupColor(t);
    switch (t) {
        case PickupType::Weapon: audio.play(Sfx::WeaponPickup, 0.85f, 0.015f); break;
        case PickupType::Shield: audio.play(Sfx::ShieldPickup, 0.8f, 0.015f); break;
        case PickupType::Repair: audio.play(Sfx::RepairPickup, 0.78f, 0.02f); break;
        case PickupType::Rapid: audio.play(Sfx::RapidPickup, 0.84f, 0.015f); break;
        case PickupType::Missiles: audio.play(Sfx::Pickup, 0.8f, 0); break;
        default: break;   // Flamer / Plasma: grantWeapon() plays its own cue
    }
}

// ============================================================ Level 3 hazard solids
// The bulkhead collides with the same leaves, sills and pillars that Render3D draws, in the
// door's model frame (BulkheadModel). The turret's body is a few capsules around its drawn
// hardpoint, head and barrels. Ships steer around both; a ship that still meets one crashes.
bool Game::doorSolid() const {
    return level3Door.active && r3d.ok() && view.mode == Mode::SideScroll;
}

Vector3 Game::doorModel(Vector2 pos, float hidden) const {
    return r3d.bulkheadLocal(r3d.bulkheadPose(level3Door.x), pos, hidden);
}

float Game::doorModelRadius(float radius) const {
    // Physical radii are in gameplay units at the focal plane (one world size at every depth).
    return radius * CameraSystem::worldPerLocal(view) / r3d.bulkheadPose(level3Door.x).scale;
}

float Game::doorFaceX(Vector2 pos, float hidden, float modelV) const {
    // Model v is affine in local x at a fixed lateral position and depth.
    const float v0 = doorModel(pos, hidden).y, v1 = doorModel({pos.x + 1.f, pos.y}, hidden).y;
    const float dv = v1 - v0;
    return std::fabs(dv) > 1e-6f ? pos.x + (modelV - v0) / dv : pos.x;
}

bool Game::turretSolid() const {
    // The hardpoint and (after destruction) the wreck stay mounted while they are in the corridor.
    return level3Turret.spawned && view.mode == Mode::SideScroll && level3Turret.pos.x > -180.f * s &&
           level3Turret.pos.x < view.forward() + 180.f * s;
}

int Game::turretSolids(HazardCapsule out[3]) const {
    const Vector2 m = level3Turret.pos;
    int n = 0;
    // Flat ceiling hardpoint (drawn ~97 px wide at s = 1).
    out[n++] = {{m.x - 48.f * s, m.y - 4.f * s, 0.f}, {m.x + 48.f * s, m.y - 4.f * s, 0.f}, 9.f * s};
    if (level3Turret.alive) {
        // Inverted base + head down to the barrel pivot, then the rotating barrel cluster.
        const Vector2 pivot{m.x, m.y + 29.f * s};
        const Vector2 muzzle = r3d.ok() ? r3d.turretMuzzle(m, level3Turret.aim, level3Turret.barrelAngle,
                                                           level3Turret.recoil, level3Turret.firingBarrel) : m;
        out[n++] = {{m.x, m.y, 0.f}, {pivot.x, pivot.y, 0.f}, 22.f * s};
        out[n++] = {{pivot.x, pivot.y, 0.f}, {muzzle.x, muzzle.y, 0.f}, 9.f * s};
    } else {
        out[n++] = {{m.x, m.y, 0.f}, {m.x, m.y + 17.f * s, 0.f}, 22.f * s};  // ragged wreck base
    }
    return n;
}

bool Game::turretContact(Vector2 pos, float hidden, float radius, Vector3* normal, float* penetration) const {
    if (!turretSolid()) return false;
    HazardCapsule caps[3];
    const int n = turretSolids(caps);
    const Vector3 p{pos.x, pos.y, hidden};
    float best = 0.f; Vector3 bestN{0, 1, 0}; bool hit = false;
    for (int i = 0; i < n; ++i) {
        const Vector3 ab = Vector3Subtract(caps[i].b, caps[i].a);
        const float len2 = Vector3DotProduct(ab, ab);
        const float t = len2 > 1e-6f ? Clamp(Vector3DotProduct(Vector3Subtract(p, caps[i].a), ab) / len2, 0.f, 1.f) : 0.f;
        const Vector3 d = Vector3Subtract(p, Vector3Add(caps[i].a, Vector3Scale(ab, t)));
        const float dist = Vector3Length(d), pen = caps[i].r + radius - dist;
        if (pen > best) {
            best = pen; hit = true;
            bestN = dist > 1e-4f ? Vector3Scale(d, 1.f / dist) : Vector3{0, 1, 0};
        }
    }
    if (hit) { if (normal) *normal = bestN; if (penetration) *penetration = best; }
    return hit;
}

void Game::updateHazardAvoidance(float dt) {
    turretContactCooldown = std::max(0.f, turretContactCooldown - dt);
    if (level3Door.active) level3Door.age += dt;
    if (level3Turret.spawned) level3Turret.age += dt;
    const bool door = doorSolid(), turret = turretSolid();
    if (!door && !turret) return;
    const float step = 220.f * s * dt;  // lateral steering authority (px/s at s = 1)
    const float dMax = CameraSystem::maxDepthLocal(view);
    enemies.list.each([&](Enemy& e) {
        if (!e.alive || e.def->boss) return;
        if (door) {
            const Vector3 m = doorModel(e.pos, e.hiddenAxis);
            const float rm = doorModelRadius(e.r);
            // Ahead of either face (the door also overtakes slower ships from behind).
            if (m.y > BulkheadModel::kFaceV0 - rm - 6.f && m.y < BulkheadModel::kFaceV1 + rm + 6.f) {
                const float allowed = std::min(BulkheadModel::leafInnerEdge(level3Door.opening), BulkheadModel::kSillU) - rm - .25f;
                const float target = allowed > 0.f ? Clamp(m.x, -allowed, allowed) : 0.f;
                if (std::fabs(target - m.x) > 1e-3f) {
                    const float du = doorModel({e.pos.x, e.pos.y + 1.f}, e.hiddenAxis).x - m.x;
                    if (std::fabs(du) > 1e-6f) {
                        const float dy = Clamp((target - m.x) / du, -step, step);
                        e.avoidance.y += dy; e.pos.y += dy;
                    }
                }
                const float wAllowed = BulkheadModel::kPillarW0 - rm - .2f;
                if (std::fabs(m.z) > wAllowed) {  // keep clear of the near/far pillars
                    const float dw = doorModel(e.pos, e.hiddenAxis + 1.f).z - m.z;
                    const float want = Clamp(m.z, -wAllowed, wAllowed);
                    if (std::fabs(dw) > 1e-6f)
                        e.hiddenAxis = Clamp(e.hiddenAxis + Clamp((want - m.z) / dw, -step, step), -dMax, dMax);
                }
            }
        }
        if (turret && std::fabs(e.pos.x - level3Turret.pos.x) < 260.f * s) {
            // Look ahead along its current heading and steer off the turret's body.
            const Vector2 ahead = Vector2Add(e.pos, Vector2Scale(e.vel, .45f));
            Vector3 n; float pen;
            if (turretContact(ahead, e.hiddenAxis, e.r + 14.f * s, &n, &pen) ||
                turretContact(e.pos, e.hiddenAxis, e.r + 14.f * s, &n, &pen)) {
                const float dy = Clamp((n.y >= 0.f ? 1.f : -1.f) * pen, -step, step);
                e.avoidance.y += dy; e.pos.y += dy;
                e.hiddenAxis = Clamp(e.hiddenAxis + Clamp(n.z * pen, -step, step), -dMax, dMax);
            }
        }
    });
}

void Game::resolveHazardSpawns() {
    // Nothing may be created inside a solid: a ship that is still being spawned, or a hazard
    // that has just been mounted on top of one, moves the ship clear instead of crashing it.
    const bool door = doorSolid(), turret = turretSolid();
    if (!door && !turret) return;
    enemies.list.each([&](Enemy& e) {
        if (!e.alive) return;
        auto shift = [&](Vector2 d) {
            e.pos = Vector2Add(e.pos, d);
            if (e.def->pattern == Pattern::Path || e.def->pattern == Pattern::Invaders) e.avoidance = Vector2Add(e.avoidance, d);
            else e.origin = Vector2Add(e.origin, d);
        };
        if (door && (e.spawnAge < .15f || level3Door.age < .15f)) {
            const float rm = doorModelRadius(e.r);
            if (BulkheadModel::contacts(doorModel(e.pos, e.hiddenAxis), rm, level3Door.opening)) {
                const float x = doorFaceX(e.pos, e.hiddenAxis, BulkheadModel::kFaceV1 + rm + .1f);
                shift({x - e.pos.x, 0.f});
                if (getenv("HAZARD_LOG")) fprintf(stderr, "HAZARD spawn-resolve door %s -> x=%.0f\n", e.def->id.c_str(), e.pos.x);
            }
        }
        if (turret && (e.spawnAge < .15f || level3Turret.age < .15f)) {
            Vector3 n; float pen;
            for (int i = 0; i < 4 && turretContact(e.pos, e.hiddenAxis, e.r, &n, &pen); ++i) {
                const Vector2 d2{n.x, n.y};
                const float l = Vector2Length(d2);
                shift(l > .2f ? Vector2Scale(d2, (pen + 1.f * s) / l) : Vector2{0.f, pen + 1.f * s});
                if (getenv("HAZARD_LOG")) fprintf(stderr, "HAZARD spawn-resolve turret %s -> (%.0f,%.0f)\n", e.def->id.c_str(), e.pos.x, e.pos.y);
            }
        }
    });
}

void Game::collide() {
    flameSmokeT = std::max(0.f, flameSmokeT - GetFrameTime());
    const float pr = player.def->size * 0.22f * s;
    const WorldProjection projection = view.mode == Mode::SideScroll ? WorldProjection::Side : WorldProjection::Top;
    auto projectedContact = [&](const Projectile& shot, WorldPosition target, float radius) {
        return ProjectileTargeting::firstProjectedContact(shot.previousWorldPosition(projection),
            shot.worldPosition(projection), target, radius, projection);
    };
    auto bulkheadContact = [&](const Projectile& shot, float radius) {
        if (!level3Door.active) return -1.f;
        if (!doorSolid()) return Bulkhead::firstProjectileContact(shot.previousPos, shot.pos, radius,
            level3Door.x, 18.f * s, view.lateral(), (70.f + 290.f * level3Door.opening) * s);
        // Swept against the drawn leaves/frame, in the shot's own (hidden-axis) plane.
        const Render3D::BulkheadPose pose = r3d.bulkheadPose(level3Door.x);
        return BulkheadModel::firstContact(r3d.bulkheadLocal(pose, shot.previousPos, shot.previousHiddenAxis),
            r3d.bulkheadLocal(pose, shot.pos, shot.hiddenAxis),
            radius * CameraSystem::worldPerLocal(view) / pose.scale, level3Door.opening);
    };
    const float shieldRadius = player.def->size *
        (view.mode == Mode::SideScroll ? kShieldRadiusSide : kShieldRadiusTop) * s;
    auto hitEnemy = [&](Enemy& e, int dmg, Vector2 at, Vector2 dir, bool laser = false) {
        if (e.def->boss && enemies.bossShielded()) {   // t251: the Warden is behind its shield while its helpers fight
            fx.sparks(view.toScreen(at), view.dirToScreen(Vector2Negate(dir)), 6, {120, 200, 255, 255}, 260 * s);
            if (!laser) audio.play(Sfx::ShieldDeflect, 0.25f, 0.1f);
            return;
        }
        bool dead = applyEnemyHit(e.hp, e.flash, e.barSince, dmg);  // every hit counts: no i-frames
        if (e.def->boss) { const int fl = enemies.bossHpFloor(e); if (e.hp < fl) { e.hp = fl; dead = false; } }   // t251: next helper wave first
        static const bool hitLog = getenv("HIT_LOG") != nullptr;
        if (hitLog) fprintf(stderr, "HIT %s#%d hp=%d/%d grp=%d x=%.0f y=%.0f\n", e.def->id.c_str(), (int)(&e - &enemies.list.items[0]), e.hp, e.maxHp, e.group, e.pos.x, e.pos.y);
        const float depthScale = CameraSystem::depthScale(view, e.hiddenAxis);
        fx.sparks(view.toScreen(at), view.dirToScreen(Vector2Negate(dir)), 5, {255, 230, 150, 255}, 220 * s * depthScale, depthScale);
        if (dead) killEnemy(e);
        else if (!laser) audio.play(Sfx::Hit, 0.3f, 0.2f);
    };
    // Level 6 ground guns: swept capsule from the pad to the live muzzle (as the Level 3 turret).
    auto groundGunContact = [&](const Projectile& shot, float radius, GroundGun** hit) {
        float best = -1.f; *hit = nullptr;
        for (GroundGun& g : groundGuns) {
            if (!g.t.alive) continue;
            const float tt = ProjectileTargeting::firstCapsuleContact(shot.previousPos, shot.pos, g.t.pos, g.muzzle, radius);
            if (tt >= 0.f && (best < 0.f || tt < best)) { best = tt; *hit = &g; }
        }
        return best;
    };
    auto hitGroundGun = [&](GroundGun& g, int dmg, Vector2 at, Vector2 dir, bool laser) {
        float flash = 0.f;
        const bool dead = applyEnemyHit(g.t.hp, flash, g.barSince, dmg);
        fx.sparks(view.toScreen(at), view.dirToScreen(dir), 12, {145, 225, 255, 255}, 260 * s);
        if (dead) {
            g.t.alive = false;
            fx.explosion(view.toScreen(g.t.pos), 0.7f * s, {120, 220, 255, 255});
            fx.explosion(view.toScreen(g.t.pos), 0.45f * s, ORANGE);
            audio.play(Sfx::EnemyExplosion, 0.55f);
            score += 300; fx.text(view.toScreen(g.t.pos), "+300", GOLD);
        } else if (laser) audio.hitLaser();
        else audio.play(Sfx::Hit, 0.3f, 0.2f);
    };
    weapons.lasers.each([&](Laser& l) {
        Enemy* firstEnemy = nullptr;
        float first = 2.f;
        enemies.list.each([&](Enemy& e) {
            const ProjectileTarget target{e.pos, Faction::Enemy, e.alive};
            if (!ProjectileTargeting::canTarget(l.faction, target)) return;
            const float projectedRadius = e.r * CameraSystem::depthScale(view, e.hiddenAxis);
            const float t = projectedContact(l, e.worldPosition(projection), projectedRadius + 6 * s);
            if (t >= 0.f && t < first) { first = t; firstEnemy = &e; }
        });
        float turretT = -1.f;
        if (level3Turret.alive) {
            const Vector2 muzzle = r3d.ok() ? r3d.turretMuzzle(level3Turret.pos, level3Turret.aim, level3Turret.barrelAngle,
                                                                level3Turret.recoil, level3Turret.firingBarrel) : level3Turret.pos;
            turretT = ProjectileTargeting::firstCapsuleContact(l.previousPos, l.pos, level3Turret.pos, muzzle, 21.f * s);
        }
        GroundGun* gun = nullptr;
        const float gunT = groundGuns.empty() ? -1.f : groundGunContact(l, 21.f * s, &gun);
        const float doorT = bulkheadContact(l, 6.f * s);
        if (gun && gunT < first && (turretT < 0.f || gunT < turretT) && (doorT < 0.f || gunT < doorT)) {
            l.pos = Vector2Lerp(l.previousPos, l.pos, gunT);
            l.alive = false; audio.stopLaser(l.flightSound); l.flightSound = 0;
            hitGroundGun(*gun, l.damage, l.pos, l.vel, true);
        } else if (doorT >= 0.f && doorT < first && (turretT < 0.f || doorT <= turretT)) {
            l.pos = Vector2Lerp(l.previousPos, l.pos, doorT); l.alive = false;
            if (getenv("BULKHEAD_REVIEW")) fprintf(stderr, "BULKHEAD laser hit x=%.1f y=%.1f opening=%.1f\n", l.pos.x, l.pos.y, level3Door.opening);
            audio.stopLaser(l.flightSound); l.flightSound = 0; audio.hitLaser();
            fx.sparks(view.toScreen(l.pos), view.dirToScreen(Vector2Negate(l.vel)), 8, {255, 210, 120, 255}, 220 * s);
        } else if (turretT >= 0.f && turretT < first) {
            l.pos = Vector2Lerp(l.previousPos, l.pos, turretT);
            l.alive = false; audio.stopLaser(l.flightSound); l.flightSound = 0;
            level3Turret.hp -= l.damage;
            if (getenv("TURRET_REVIEW")) fprintf(stderr, "TURRET laser hit hp=%d x=%.1f y=%.1f\n", level3Turret.hp, l.pos.x, l.pos.y);
            fx.sparks(view.toScreen(l.pos), view.dirToScreen(l.vel), 12, {145, 225, 255, 255}, 260 * s);
            if (level3Turret.hp <= 0) {
                level3Turret.alive = false;
                if (getenv("TURRET_REVIEW")) fprintf(stderr, "TURRET destroyed x=%.1f y=%.1f\n", level3Turret.pos.x, level3Turret.pos.y);
                fx.explosion(view.toScreen(level3Turret.pos), 0.7f * s, {120, 220, 255, 255});
                audio.play(Sfx::EnemyExplosion, 0.55f);
            } else audio.hitLaser();
        } else if (firstEnemy) {
            Enemy& e = *firstEnemy;
            l.pos = Vector2Lerp(l.previousPos, l.pos, first);
            l.alive = false;
            audio.stopLaser(l.flightSound);
            l.flightSound = 0;
            audio.hitLaser();
            const int em = r3d.enemyModel(e.def->presentation.model);
            const Vector2 hp = r3d.ok() ? r3d.hullContact(e.pos, l.pos, l.vel, em, e.hiddenAxis) : l.pos;  // bolt contact on the drawn hull
            vfx.impact(hp, view.dirToScreen(l.vel), shipPx(e.def->scale) * CameraSystem::depthScale(view, e.hiddenAxis), &e.pos, &e, false);
                    r3d.addDent(e.dent, e.nDent, e.dentNext, hp, e.pos, (float)l.damage, em, e.hiddenAxis, e.surfaceFx); hitEnemy(e, l.damage, l.pos, Vector2Normalize(l.vel), true);
        }
        if (l.alive && level3Door.active && getenv("BULKHEAD_REVIEW_GAP") &&
            l.previousPos.x < level3Door.x && l.pos.x >= level3Door.x)
            fprintf(stderr, "BULKHEAD laser passed gap x=%.1f y=%.1f opening=%.2f\n", l.pos.x, l.pos.y, level3Door.opening);
    });
    // ---- plasma lance (hull-accurate first hit) + flamethrower (six first-hit rays) ----
    {
        const float dt = GetFrameTime();
        beamSoundT = std::max(0.f, beamSoundT - dt);
        auto rayCircle = [](Vector2 o, Vector2 d, Vector2 c, float R) {   // distance along the ray to the circle, -1 = miss
            const Vector2 w = Vector2Subtract(c, o); const float t = Vector2DotProduct(w, d);
            const float p2 = Vector2LengthSqr(w) - t * t; if (p2 > R * R) return -1.f;
            const float th = t - sqrtf(std::max(0.f, R * R - p2)); return th >= -R ? std::max(0.f, th) : -1.f; };
        auto rayEllipse = [](Vector2 origin, Vector2 direction, Rectangle bounds) {
            const float rx = bounds.width * 0.5f, ry = bounds.height * 0.5f;
            if (rx <= 0.5f || ry <= 0.5f) return -1.f;
            const Vector2 center{bounds.x + rx, bounds.y + ry};
            const Vector2 p{(origin.x - center.x) / rx, (origin.y - center.y) / ry};
            const Vector2 d{direction.x / rx, direction.y / ry};
            const float a = Vector2DotProduct(d, d), c = Vector2DotProduct(p, p) - 1.f;
            if (c <= 0.f) return 0.f;
            if (a <= 1e-8f) return -1.f;
            const float b = Vector2DotProduct(p, d), disc = b * b - a * c;
            if (disc < 0.f) return -1.f;
            const float t = (-b - sqrtf(disc)) / a;
            return t >= 0.f ? t : -1.f;
        };
        auto enemyRayContact = [&](Enemy& e, Vector2 origin, Vector2 direction, Vector2& contact) {
            float distance = -1.f;
            int model = 0;
            if (r3d.ok()) {
                if (!e.def->presentation.model.empty()) model = r3d.enemyModel(e.def->presentation.model);
                const Rectangle hull = r3d.shipScreenRect(e.pos, model, e.hiddenAxis);
                if (hull.width > 1.f && hull.height > 1.f) {
                    const float projectedDistance = rayEllipse(view.toScreen(origin), view.dirToScreen(direction), hull);
                    if (projectedDistance >= 0.f) {
                        distance = projectedDistance / CameraSystem::depthScale(view, e.hiddenAxis);
                        const Vector2 candidate = Vector2Add(origin, Vector2Scale(direction, distance));
                        contact = r3d.hullContact(e.pos, candidate, direction, model, e.hiddenAxis);
                        return std::max(0.f, Vector2DotProduct(Vector2Subtract(contact, origin), direction));
                    }
                }
            }
            distance = rayCircle(origin, direction, e.pos, e.r * CameraSystem::depthScale(view, e.hiddenAxis));
            if (distance >= 0.f) contact = Vector2Add(origin, Vector2Scale(direction, distance));
            return distance;
        };
        bool plasmaHullHit = false, flameHullHit = false;
        if (weapons.beamOn) {
            const Vector2 o = weapons.beamFrom, d = weapons.beamDir;
            const Vector2 end = Vector2Add(o, Vector2Scale(d, weapons.beamMax));
            Projectile ray{}; ray.previousPos = o; ray.pos = end;
            ray.previousHiddenAxis = player.hiddenAxis; ray.hiddenAxis = player.hiddenAxis;
            float best = weapons.beamMax; Enemy* te = nullptr; GroundGun* tg = nullptr; bool tt = false, doorHit = false;
            const float doorT = bulkheadContact(ray, 13.f * s);
            if (doorT >= 0.f) { best = doorT * weapons.beamMax; doorHit = true; }
            GroundGun* rayGun = nullptr;
            const float gunT = groundGunContact(ray, 13.f * s, &rayGun);
            if (rayGun && gunT >= 0.f && gunT * weapons.beamMax < best) {
                best = gunT * weapons.beamMax; tg = rayGun; doorHit = false;
            }
            if (level3Turret.alive) {
                const Vector2 muzzle = r3d.ok() ? r3d.turretMuzzle(level3Turret.pos, level3Turret.aim,
                    level3Turret.barrelAngle, level3Turret.recoil, level3Turret.firingBarrel) : level3Turret.pos;
                const float turretT = ProjectileTargeting::firstCapsuleContact(o, end, level3Turret.pos, muzzle, 13.f * s);
                if (turretT >= 0.f && turretT * weapons.beamMax < best) {
                    best = turretT * weapons.beamMax; tt = true; tg = nullptr; doorHit = false;
                }
            }
            enemies.list.each([&](Enemy& e) {
                if (!e.alive || e.pos.x > view.forward() + 20.f * s) return;
                Vector2 contact{};
                const float th = enemyRayContact(e, o, d, contact);
                if (th >= 0.f && th < best) { best = th; weapons.beamHitPos = contact; te = &e; tg = nullptr; tt = false; doorHit = false; }
            });
            weapons.beamLen = best;
            weapons.beamDepthScale = te ? CameraSystem::depthScale(view, te->hiddenAxis) : 1.f;
            const bool targetHit = te || tg || tt;
            weapons.beamHit = targetHit || doorHit;
            if (!targetHit && doorHit) weapons.beamHitPos = Vector2Add(o, Vector2Scale(d, best));
            if (targetHit && !te) weapons.beamHitPos = Vector2Add(o, Vector2Scale(d, best));
            plasmaHullHit = targetHit;
            if (targetHit) weapons.beamAcc += Weapons::kPlasmaDps * dt;
            const int dmg = (int)weapons.beamAcc;
            if (targetHit && dmg > 0) {
                weapons.beamAcc -= dmg;
                const Vector2 at = weapons.beamHitPos;
                if (beamSoundT <= 0.f) { audio.hitLaser(); beamSoundT = 0.11f; }
                if (te) {
                    const int em = te->def->presentation.model.empty() ? 0 : r3d.enemyModel(te->def->presentation.model);
                    const int slot = r3d.addDent(te->dent, te->nDent, te->dentNext, at, te->pos, (float)dmg, em, te->hiddenAxis, te->surfaceFx);
                    if (slot >= 0) {
                        te->surfaceFx[slot][kSurfacePlasmaHeat] = std::min(1.f, te->surfaceFx[slot][kSurfacePlasmaHeat] + dmg * 0.035f);
                        te->surfaceFx[slot][kSurfaceChar] = std::min(1.f, te->surfaceFx[slot][kSurfaceChar] + dmg * 0.012f);
                        te->surfaceFx[slot][kSurfaceCharSpread] = std::min(1.f, te->surfaceFx[slot][kSurfaceCharSpread] + dmg * 0.008f);
                    }
                    if (beamSoundT <= 0.11f) fx.embers(view.toScreen(at), 2, 2.2f * s * CameraSystem::depthScale(view, te->hiddenAxis), 70.f * s);
                    hitEnemy(*te, dmg, at, d, true);
                } else if (tg) hitGroundGun(*tg, dmg, at, Vector2Negate(d), false);
                else {
                    level3Turret.hp -= dmg; fx.sparks(view.toScreen(at), view.dirToScreen(Vector2Negate(d)), 4, {230, 160, 255, 255}, 220 * s);
                    if (level3Turret.hp <= 0) { level3Turret.alive = false; fx.explosion(view.toScreen(level3Turret.pos), 0.7f * s, {120, 220, 255, 255}); audio.play(Sfx::EnemyExplosion, 0.55f); }
                }
            } else if (!targetHit) weapons.beamAcc = std::min(weapons.beamAcc, 1.f);
        }
        if (weapons.flameOn) {
            weapons.flameTick += dt * Weapons::kFlameTickRate * (weapons.rapid > 0 ? 1.5f : 1.f);
            const int ticks = std::min(3, (int)weapons.flameTick); weapons.flameTick -= ticks;
            const Vector2 o = weapons.flameFrom, baseDir = weapons.flameDir;
            const Vector2 end = Vector2Add(o, Vector2Scale(baseDir, weapons.flameRange));
            Projectile ray{}; ray.previousPos = o; ray.pos = end;
            ray.previousHiddenAxis = player.hiddenAxis; ray.hiddenAxis = player.hiddenAxis;
            struct FlameEnemyHit { Enemy* enemy = nullptr; Vector2 at{}, dir{}; };
            std::array<FlameEnemyHit, Weapons::kFlameRayCount> hitEnemies{};
            std::array<GroundGun*, Weapons::kFlameRayCount> hitGuns{};
            int nHitEnemies = 0, nHitGuns = 0;
            bool hitTurret = false;
            const float baseAngle = atan2f(baseDir.y, baseDir.x);
            for (int i = 0; i < Weapons::kFlameRayCount; i++) {
                const float offset = -Weapons::kFlameHalfAngle +
                    (2.f * Weapons::kFlameHalfAngle) * i / (Weapons::kFlameRayCount - 1.f);
                const Vector2 d{cosf(baseAngle + offset), sinf(baseAngle + offset)};
                const Vector2 rayEnd = Vector2Add(o, Vector2Scale(d, weapons.flameRange));
                ray.pos = rayEnd;
                float best = weapons.flameRange;
                Enemy* hitEnemyPtr = nullptr; GroundGun* hitGunPtr = nullptr;
                bool hitL3Turret = false;
                Vector2 hitAt{};
                const float doorT = bulkheadContact(ray, 6.f * s);
                if (doorT >= 0.f) best = doorT * weapons.flameRange;
                GroundGun* rayGun = nullptr;
                const float gunT = groundGunContact(ray, 10.f * s, &rayGun);
                if (rayGun && gunT >= 0.f && gunT * weapons.flameRange < best) {
                    best = gunT * weapons.flameRange; hitGunPtr = rayGun;
                }
                if (level3Turret.alive) {
                    const Vector2 muzzle = r3d.ok() ? r3d.turretMuzzle(level3Turret.pos, level3Turret.aim,
                        level3Turret.barrelAngle, level3Turret.recoil, level3Turret.firingBarrel) : level3Turret.pos;
                    const float turretT = ProjectileTargeting::firstCapsuleContact(o, rayEnd,
                        level3Turret.pos, muzzle, 10.f * s);
                    if (turretT >= 0.f && turretT * weapons.flameRange < best) {
                        best = turretT * weapons.flameRange; hitL3Turret = true; hitGunPtr = nullptr;
                    }
                }
                enemies.list.each([&](Enemy& e) {
                    if (!e.alive || e.pos.x > view.forward() + 20.f * s) return;
                    Vector2 contact{};
                    const float t = enemyRayContact(e, o, d, contact);
                    if (t >= 0.f && t < best && t <= weapons.flameRange) {
                        best = t; hitEnemyPtr = &e; hitGunPtr = nullptr; hitL3Turret = false; hitAt = contact;
                    }
                });
                weapons.flameStops[i] = best;
                if (hitEnemyPtr) {
                    flameHullHit = true;
                    if (ticks > 0 && nHitEnemies < Weapons::kFlameRayCount &&
                        std::find_if(hitEnemies.begin(), hitEnemies.begin() + nHitEnemies,
                            [&](const FlameEnemyHit& h) { return h.enemy == hitEnemyPtr; }) == hitEnemies.begin() + nHitEnemies)
                        hitEnemies[nHitEnemies++] = {hitEnemyPtr, hitAt, d};
                } else if (hitGunPtr) {
                    flameHullHit = true;
                    if (ticks > 0 && nHitGuns < Weapons::kFlameRayCount &&
                        std::find(hitGuns.begin(), hitGuns.begin() + nHitGuns, hitGunPtr) == hitGuns.begin() + nHitGuns)
                        hitGuns[nHitGuns++] = hitGunPtr;
                } else if (hitL3Turret) { flameHullHit = true; hitTurret = hitTurret || ticks > 0; }

            }
            const float baseAngleForParticles = atan2f(baseDir.y, baseDir.x);
            weapons.flames.each([&](FlameBit& f) {
                const int rayIndex = std::clamp(f.rayIndex, 0, Weapons::kFlameRayCount - 1);
                const float offset = -Weapons::kFlameHalfAngle +
                    (2.f * Weapons::kFlameHalfAngle) * rayIndex / (Weapons::kFlameRayCount - 1.f);
                const Vector2 d{cosf(baseAngleForParticles + offset), sinf(baseAngleForParticles + offset)};
                if (Vector2DotProduct(Vector2Subtract(f.pos, o), d) >= weapons.flameStops[rayIndex]) f.alive = false;
            });
            if (ticks > 0) {
                for (int i = 0; i < nHitEnemies; i++) {
                    FlameEnemyHit& hit = hitEnemies[i];
                    const int model = hit.enemy->def->presentation.model.empty() ? 0 : r3d.enemyModel(hit.enemy->def->presentation.model);
                    const int slot = r3d.addDent(hit.enemy->dent, hit.enemy->nDent, hit.enemy->dentNext, hit.at, hit.enemy->pos, 0.f, model, hit.enemy->hiddenAxis, hit.enemy->surfaceFx);
                    if (slot >= 0) {
                        hit.enemy->surfaceFx[slot][kSurfaceFlameHeat] = std::min(1.f, hit.enemy->surfaceFx[slot][kSurfaceFlameHeat] + ticks * 0.12f);
                        hit.enemy->surfaceFx[slot][kSurfaceChar] = std::min(1.f, hit.enemy->surfaceFx[slot][kSurfaceChar] + ticks * 0.018f);
                        hit.enemy->surfaceFx[slot][kSurfaceCharSpread] = std::min(1.f, hit.enemy->surfaceFx[slot][kSurfaceCharSpread] + ticks * 0.014f);
                    }
                    hitEnemy(*hit.enemy, ticks, hit.at, hit.dir, true);
                    if (flameSmokeT <= 0.f) {
                        fx.turretSmoke(view.toScreen(hit.at), 7.f * s * CameraSystem::depthScale(view, hit.enemy->hiddenAxis));
                        flameSmokeT = 0.14f;
                    }
                }
                for (int i = 0; i < nHitGuns; i++) hitGroundGun(*hitGuns[i], ticks, hitGuns[i]->t.pos, Vector2Negate(baseDir), true);
                if (hitTurret) {
                    level3Turret.hp -= ticks;
                    if (level3Turret.hp <= 0) { level3Turret.alive = false; fx.explosion(view.toScreen(level3Turret.pos), 0.7f * s, {120, 220, 255, 255}); audio.play(Sfx::EnemyExplosion, 0.55f); }
                }
            }
        }
        audio.flamethrower(flameHullHit);
        audio.sizzle(plasmaHullHit);
    }
    weapons.missiles.each([&](Missile& m) {
        Enemy* firstEnemy = nullptr; float first = 2.f;
        enemies.list.each([&](Enemy& e) {
            const ProjectileTarget target{e.pos, Faction::Enemy, e.alive};
            if (!ProjectileTargeting::canTarget(m.faction, target)) return;
            const float projectedRadius = e.r * CameraSystem::depthScale(view, e.hiddenAxis);
            const float t = projectedContact(m, e.worldPosition(projection), projectedRadius + 8 * s);
            if (t >= 0.f && t < first) { first = t; firstEnemy = &e; }
        });
        float turretT = -1.f;
        if (level3Turret.alive) {
            const Vector2 muzzle = r3d.ok() ? r3d.turretMuzzle(level3Turret.pos, level3Turret.aim, level3Turret.barrelAngle,
                                                                level3Turret.recoil, level3Turret.firingBarrel) : level3Turret.pos;
            turretT = ProjectileTargeting::firstCapsuleContact(m.previousPos, m.pos, level3Turret.pos, muzzle, 23.f * s);
        }
        GroundGun* gun = nullptr;
        const float gunT = groundGuns.empty() ? -1.f : groundGunContact(m, 23.f * s, &gun);
        const float doorT = bulkheadContact(m, 8.f * s);
        if (gun && gunT < first && (turretT < 0.f || gunT < turretT) && (doorT < 0.f || gunT < doorT)) {
            m.pos = Vector2Lerp(m.previousPos, m.pos, gunT); m.alive = false;
            fx.explosion(view.toScreen(m.pos), .45f * s, ORANGE);
            hitGroundGun(*gun, m.damage, m.pos, Vector2Negate(m.vel), false);
        } else if (doorT >= 0.f && doorT < first && (turretT < 0.f || doorT <= turretT)) {
            m.pos = Vector2Lerp(m.previousPos, m.pos, doorT); m.alive = false;
            fx.explosion(view.toScreen(m.pos), .45f * s, ORANGE);
        } else if (turretT >= 0.f && turretT < first) {
            m.pos = Vector2Lerp(m.previousPos, m.pos, turretT); m.alive = false;
            level3Turret.hp -= m.damage;
            if (getenv("TURRET_REVIEW")) fprintf(stderr, "TURRET missile hit hp=%d x=%.1f y=%.1f\n", level3Turret.hp, m.pos.x, m.pos.y);
            fx.explosion(view.toScreen(m.pos), .45f * s, ORANGE);
            if (level3Turret.hp <= 0) {
                level3Turret.alive = false;
                if (getenv("TURRET_REVIEW")) fprintf(stderr, "TURRET destroyed x=%.1f y=%.1f\n", level3Turret.pos.x, level3Turret.pos.y);
                fx.explosion(view.toScreen(level3Turret.pos), .7f * s, {120, 220, 255, 255});
                audio.play(Sfx::EnemyExplosion, .55f);
            } else audio.play(Sfx::Hit, .3f, .2f);
        } else if (firstEnemy) {
            Enemy& e = *firstEnemy;
            m.pos = Vector2Lerp(m.previousPos, m.pos, first);
            m.alive = false;
            r3d.addDent(e.dent, e.nDent, e.dentNext, m.pos, e.pos, (float)m.damage, r3d.enemyModel(e.def->presentation.model), e.hiddenAxis, e.surfaceFx);
            fx.explosion(view.toScreen(m.pos), 0.45f * s, ORANGE);
            fx.text(view.toScreen(e.pos), TextFormat("%d", m.damage), ORANGE, CameraSystem::depthScale(view, e.hiddenAxis));
            hitEnemy(e, m.damage, m.pos, Vector2Normalize(m.vel));
        }
    });
    enemies.shots.each([&](EnemyShot& b) {
        const float shieldT = player.shield > 0 ? projectedContact(b, player.worldPosition(projection), shieldRadius + b.r) : -1.f;
        const float hullT = projectedContact(b, player.worldPosition(projection), pr + b.r);
        const float doorT = bulkheadContact(b, b.r);
        if (doorT >= 0.f && (shieldT < 0.f || doorT <= shieldT) && (hullT < 0.f || doorT <= hullT)) {
            b.pos = Vector2Lerp(b.previousPos, b.pos, doorT); b.alive = false;
            fx.sparks(view.toScreen(b.pos), view.dirToScreen(Vector2Negate(b.vel)), 8, ORANGE, 220 * s);
        } else if (shieldT >= 0.f && (hullT < 0.f || shieldT <= hullT)) {
            b.pos = Vector2Lerp(b.previousPos, b.pos, shieldT);
            b.alive = false;
            if (enemies.safeOn) { allHits++; if (enemies.insideSafe(player.pos)) { zoneHits++; fprintf(stderr, "SAFEZONE HIT shield at (%.0f,%.0f)\n", player.pos.x, player.pos.y); } }
            Vector2 outward = Vector2Normalize(Vector2Subtract(b.pos, player.pos));
            if (Vector2LengthSqr(outward) < 0.001f) outward = Vector2Normalize(Vector2Negate(b.vel));
            const Vector2 contact = Vector2Add(player.pos, Vector2Scale(outward, shieldRadius));
            // The bolt dies at the sphere surface; blue radial sparks make that dissipation readable.
            fx.sparks(view.toScreen(contact), view.dirToScreen(outward), 14, {100, 225, 255, 255}, 310 * s);
            r3d.shieldImpact(player.pos, contact, player.hiddenAxis);
            audio.shieldDeflect();
            damagePlayer(b.dmg > 0 ? b.dmg : kBulletDamage, contact);
        } else if (hullT >= 0.f) {
            b.pos = Vector2Lerp(b.previousPos, b.pos, hullT);
            b.alive = false;
            if (enemies.safeOn) { allHits++; if (enemies.insideSafe(player.pos)) { zoneHits++; fprintf(stderr, "SAFEZONE HIT hull at (%.0f,%.0f)\n", player.pos.x, player.pos.y); } }
            vfx.impact(b.pos, view.dirToScreen(b.vel), player.def->size * s, &player.pos, &player, true);
            damagePlayer(b.dmg > 0 ? b.dmg : kBulletDamage, b.pos);
        }
    });
    if (level3Door.active) {
        const float halfGap = (70.f + 290.f * level3Door.opening) * s;
        bool panelContact, inFront;
        float frontX, beyondX;  // the door's approach face and the clear side beyond it
        if (doorSolid()) {
            const Vector3 m = doorModel(player.pos, player.hiddenAxis);
            const float rm = doorModelRadius(pr);
            panelContact = BulkheadModel::contacts(m, rm, level3Door.opening);
            inFront = m.y <= (BulkheadModel::kFaceV0 + BulkheadModel::kFaceV1) * .5f;
            frontX = doorFaceX(player.pos, player.hiddenAxis, BulkheadModel::kFaceV0 - rm - .02f);
            beyondX = doorFaceX(player.pos, player.hiddenAxis, BulkheadModel::kFaceV1 + rm) + 5.f * s;
        } else {
            panelContact = Bulkhead::contacts(player.worldPosition(WorldProjection::Side), pr,
                level3Door.x, 18.f * s, view.lateral(), halfGap);
            inFront = player.pos.x <= level3Door.x;
            frontX = level3Door.x - 18.f * s - pr;
            beyondX = level3Door.x + 18.f * s + pr + 5.f * s;
        }
        if (panelContact && !inFront) {
            // Caught inside the door's thickness from behind: pushed out the far side.
            damagePlayer(8, player.pos);
            player.pos.x = Clamp(beyondX, 45.f * s, view.forward() * kPlayerMaxX);
        }
        if (panelContact && inFront) {
            const int livesBefore = lives;
            damagePlayer(8, player.pos);
            player.pos.x = std::min(player.pos.x, frontX);
            if (lives < livesBefore && state == State::Playing) {
                player.pos.x = Clamp(beyondX, 45.f * s, view.forward() * kPlayerMaxX);
                if (getenv("BULKHEAD_REVIEW")) fprintf(stderr, "BULKHEAD respawn after damage x=%.1f door=%.1f lives=%d\n", player.pos.x, level3Door.x, lives);
            } else if (player.pos.x < pr && state == State::Playing && !getenv("GODMODE") && !getenv("AUTOPLAY")) {
                player.invuln = 0; player.shield = 0;
                damagePlayer(player.hp, player.pos);
                if (state == State::Playing) player.pos.x = Clamp(beyondX, 45.f * s, view.forward() * kPlayerMaxX);
                if (getenv("BULKHEAD_REVIEW")) fprintf(stderr, "BULKHEAD crushed x=%.1f door=%.1f lives=%d\n", player.pos.x, level3Door.x, lives);
            }
        }
    }
    enemies.list.each([&](Enemy& e) {
        if (e.leaveT >= 0.f) return;  // L6 lull: disengaging ships steer clear and never ram
        if (player.shield > 0 && WorldPhysics::overlaps(e.worldPosition(projection), e.r,
                                                        player.worldPosition(projection), shieldRadius)) {
            Vector2 outward = Vector2Normalize(Vector2Subtract(e.pos, player.pos));
            if (Vector2LengthSqr(outward) < 0.001f) outward = {view.forwardAngle() == 0.f ? 1.f : 0.f,
                                                               view.forwardAngle() == 0.f ? 0.f : -1.f};
            const float separation = shieldRadius + e.r + 1.f * s;
            const float depthDelta = e.hiddenAxis - player.hiddenAxis;
            const float projectedSeparation = sqrtf(std::max(0.f, separation * separation - depthDelta * depthDelta));
            const Vector2 corrected = Vector2Add(player.pos, Vector2Scale(outward, projectedSeparation));
            // This is a hard field, not a bumper: hold the intruder at the shell rather than
            // changing its path anchor and visibly throwing it back into the level.
            e.pos = corrected;
            if (e.shieldContact <= 0.f) {
                e.shieldContact = kShieldContactCooldown;
                const Vector2 contact = Vector2Add(player.pos, Vector2Scale(outward, shieldRadius));
                fx.sparks(view.toScreen(contact), view.dirToScreen(outward), 22, {100, 225, 255, 255}, 360 * s);
                r3d.shieldImpact(player.pos, contact, player.hiddenAxis);
                audio.shieldDeflect();
                hitEnemy(e, kShieldContactDamage, contact, outward);
                damagePlayer(kShieldContactDamage, contact);
            }
        } else if (!e.def->boss && WorldPhysics::overlaps(e.worldPosition(projection), e.r,
                                                           player.worldPosition(projection), pr)) {
            killEnemy(e); damagePlayer(kRamDamage, e.pos);
        } else if (e.def->boss && WorldPhysics::overlaps(e.worldPosition(projection), e.r,
                                                          player.worldPosition(projection), pr)) {
            // A boss occupies its space too: the hull is held outside it and a ram hurts both,
            // at the same contact cadence as a shield touch (a boss is not destroyed by one ram).
            Vector2 outward = Vector2Normalize(Vector2Subtract(player.pos, e.pos));
            if (Vector2LengthSqr(outward) < 0.001f) outward = view.mode == Mode::SideScroll ? Vector2{-1.f, 0.f} : Vector2{-1.f, 0.f};
            const float separation = e.r + pr + 1.f * s;
            const float depthDelta = e.hiddenAxis - player.hiddenAxis;
            const float projectedSeparation = sqrtf(std::max(0.f, separation * separation - depthDelta * depthDelta));
            player.pos = Vector2Add(e.pos, Vector2Scale(outward, projectedSeparation));
            player.pos.x = Clamp(player.pos.x, 45.f * s, view.forward() * kPlayerMaxX);
            player.pos.y = Clamp(player.pos.y, 35.f * s, view.lateral() - 35.f * s);
            if (e.shieldContact <= 0.f) {
                e.shieldContact = kShieldContactCooldown;
                const Vector2 contact = Vector2Add(e.pos, Vector2Scale(outward, e.r));
                if (getenv("HAZARD_LOG")) fprintf(stderr, "HAZARD boss-ram hp=%d player=%d\n", e.hp, player.hp);
                hitEnemy(e, kRamDamage, contact, Vector2Negate(outward));
                damagePlayer(kRamDamage, contact);
            }
        }
    });
    // Level 3 solids. Ships that reach a door leaf, sill, pillar or the turret body crash
    // (destroyed, no score); fresh spawns were already moved clear in resolveHazardSpawns().
    resolveHazardSpawns();
    if (doorSolid() || turretSolid()) {
        enemies.list.each([&](Enemy& e) {
            if (!e.alive) return;
            const char* what = nullptr;
            if (doorSolid() && BulkheadModel::contacts(doorModel(e.pos, e.hiddenAxis), doorModelRadius(e.r), level3Door.opening)) what = "door";
            else if (turretContact(e.pos, e.hiddenAxis, e.r, nullptr, nullptr)) what = "turret";
            if (!what) return;
            if (getenv("HAZARD_LOG")) fprintf(stderr, "HAZARD crash %s into %s at (%.0f,%.0f,h%.0f)\n", e.def->id.c_str(), what, e.pos.x, e.pos.y, e.hiddenAxis);
            fx.sparks(view.toScreen(e.pos), view.dirToScreen({-1.f, 0.f}), 16, {255, 176, 60, 255}, 260 * s);
            if (e.def->boss) { e.pos.x += 4.f * s; hitEnemy(e, kRamDamage, e.pos, {-1.f, 0.f}); }
            else killEnemy(e, false);
        });
    }
    if (turretSolid()) {   // the player cannot fly through the turret or its wreck
        const float rp = player.shield > 0 ? shieldRadius : pr;
        Vector3 n; float pen;
        if (turretContact(player.pos, player.hiddenAxis, rp, &n, &pen)) {
            Vector2 push{n.x, n.y};
            const float l = Vector2Length(push);
            push = l > .2f ? Vector2Scale(push, (pen + .5f * s) / l) : Vector2{0.f, pen + .5f * s};
            player.pos = Vector2Add(player.pos, push);
            player.pos.x = Clamp(player.pos.x, 45.f * s, view.forward() * kPlayerMaxX);
            player.pos.y = Clamp(player.pos.y, 35.f * s, view.lateral() - 35.f * s);
            // No room between the turret and the ceiling (or pinned at the screen edge): the
            // ship is forced out underneath it instead of being left inside the mount.
            for (int i = 0; i < 48 && turretContact(player.pos, player.hiddenAxis, rp, nullptr, nullptr); ++i)
                player.pos.y = std::min(player.pos.y + 4.f * s, view.lateral() - 35.f * s);
            // A live turret is rammed like a ship (both take damage). The cold wreck only blocks
            // a shield, but still scrapes an unshielded hull.
            if (turretContactCooldown <= 0.f && (level3Turret.alive || player.shield <= 0)) {
                turretContactCooldown = kShieldContactCooldown;
                const Vector2 dir = Vector2Normalize(push);
                const Vector2 contact = Vector2Subtract(player.pos, Vector2Scale(dir, rp));
                const int dmg = !level3Turret.alive ? kShieldContactDamage : player.shield > 0 ? kShieldContactDamage : kRamDamage;
                if (getenv("HAZARD_LOG")) fprintf(stderr, "HAZARD player-turret %s dmg=%d turretHp=%d alive=%d\n", player.shield > 0 ? "shield" : "hull", dmg, level3Turret.hp, (int)level3Turret.alive);
                fx.sparks(view.toScreen(contact), view.dirToScreen(dir), 18, {255, 176, 60, 255}, 300 * s);
                if (player.shield > 0) { r3d.shieldImpact(player.pos, contact, player.hiddenAxis); audio.shieldDeflect(); }
                damagePlayer(dmg, contact);
                if (level3Turret.alive) {
                    level3Turret.hp -= dmg;
                    if (level3Turret.hp <= 0) {
                        level3Turret.alive = false;
                        fx.explosion(view.toScreen(level3Turret.pos), .7f * s, {120, 220, 255, 255});
                        audio.play(Sfx::EnemyExplosion, .55f);
                    } else audio.play(Sfx::Hit, .3f, .2f);
                }
            }
        }
    }
    pickups.each([&](Pickup& p) {
        if (p.state != PickupState::Active) return;
        float d = Vector2Distance(p.pos, player.pos);
        if (d < 120 * s) p.pos = Vector2MoveTowards(p.pos, player.pos, 500 * s * GetFrameTime());  // magnet
        if (d < pr + kPickupVisualRadius * s) {
            p.state = PickupState::CollectedDisperse; p.stateTime = 0;
            fx.pickupVaporize(view.toScreen(p.pos), s, pickupColor(p.type));
            applyPickup(p.type);
        }
    });
}

// Exhaust direction (screen space) of a ship frame: side view follows the frame's visible nose pitch
// (mirrored for left-facing enemies); top-down frames are banking poses, so the exhaust stays straight back.
static Vector2 exhaustDir(const View& view, const SpriteSheet& sh, int row, int col, bool back) {
    if (view.mode != Mode::SideScroll) return Vector2Scale(view.dirToScreen({1, 0}), back ? 1.f : -1.f);
    const float p = sh.pitchDeg(row, col) * DEG2RAD;
    return {back ? std::cos(p) : -std::cos(p), std::sin(p)};  // nose up -> exhaust points down-back
}

void Game::emitGlitter(float dt) {
    if (appr.on && appr.cam3d) return;  // 2D exhaust glitter belongs to the overhead / turned image only
    const int frame = (int)(time * kAnimFps);
    auto emit = [&](const SpriteSheet& sh, Vector2 local, int row, float size, bool back) {
        auto noz = sh.nozzle(row, frame);
        Vector2 sp = view.toScreen(local);
        Vector2 exhaust = exhaustDir(view, sh, row, frame, back);
        for (int k = 0; k < 2; k++) {
            float nx = noz[k].x, ny = noz[k].y;
            if (back) { if (view.mode == Mode::SideScroll) nx = 1 - nx; else ny = 1 - ny; }
            Vector2 p{sp.x + (nx - 0.5f) * size, sp.y + (ny - 0.5f) * size};
            p = Vector2Add(p, Vector2Scale(exhaust, size * kFlameScale * kFlameLength * frand(0.5f, 0.95f)));  // trailing half/tip
            float expected = kGlitterRate * dt;
            int n = (int)expected + (frand(0, 1) < expected - (int)expected);
            for (int i = 0; i < n; i++) {
                Vector2 vel = Vector2Add(Vector2Scale(exhaust, frand(0.5f, 1.f) * kGlitterSpeed * s),
                                         Vector2Scale(view.dirToScreen({0, 1}), frand(-30, 30) * s));
                fx.flameSpark(p, vel, frand(0.6f, 1.3f) * kGlitterSize * s, frand(0.5f, 1.f) * kGlitterLife);
            }
        }
    };
    static const std::string kSecSide = "side_scroller", kSecTop = "top_down";
    const std::string& sec = view.mode == Mode::SideScroll ? kSecSide : kSecTop;
    if (state == State::Playing) emit(assets.sheet(player.def->presentation.sprite, sec), player.pos, player.row, player.def->size * s, false);
    enemies.list.each([&](Enemy& e) {
        if (e.pos.x < view.forward() + 40 * s + view.vis) {
            const float enemyMul = view.mode == Mode::SideScroll ? kSideEnemyScaleMul : kTopDownEnemyScaleMul;
            emit(assets.sheet(e.def->presentation.sprite, sec), e.pos, e.row, kEnemySize * e.def->scale * s * enemyMul, true);
        }
    });
}

// ============================================================ update
void Game::update(float dt) {
    {   // Level 6 full screen: no pointer (the small centre "x"/dot) over the play area
        static bool hidden = false;
        bool want = appr.on && state == State::Playing && !paused;
#if defined(_WIN32)
        want = state == State::Playing && !paused;
#endif
        if (want != hidden) { if (want) HideCursor(); else ShowCursor(); hidden = want; }
    }
    updateQuality(dt);
    static bool boot = true;  // START_LEVEL=n jumps straight into a level (testing)
    if (boot && getenv("START_STEP")) { boot = false; newCampaign(atoi(getenv("START_STEP"))); return; }   // capture: campaign from step n
    if (boot) { boot = false; if (const char* e = getenv("START_LEVEL")) { int n = atoi(e) - 1; if (n >= 0 && n < (int)db.levels().size()) { startLevel(n, true); return; } } }
    dt = std::min(dt, 1 / 30.f);
    if (state != State::Playing) paused = false;
    else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))
        paused = !paused;
    {   // PAUSE_TEST=t: pause at level time t (capture hook); PAUSE_TEST_EXIT=1 then takes "EXIT TO MAIN MENU"
        static const float pt = getenv("PAUSE_TEST") ? (float)atof(getenv("PAUSE_TEST")) : -1.f;
        static bool done = false; static double at = 0;
        if (pt >= 0 && !done && state == State::Playing && levelTime >= pt) { done = true; paused = true; at = GetTime(); fprintf(stderr, "PAUSE_TEST paused at %.1f\n", levelTime); }
        if (pt >= 0 && paused && getenv("PAUSE_TEST_EXIT") && at > 0 && GetTime() - at > 2.0) { at = 0; exitToMenu(); fprintf(stderr, "PAUSE_TEST exit -> title state=%d\n", (int)state); }
    }
    if (paused) {  // frozen: no sim, no timers; music keeps streaming, engine loop off
        audio.engine(false); audio.flamethrower(false); audio.plasma(false); audio.sizzle(false);
        audio.updateMusic(dt);
        return;
    }
    if (bdx.on) dt *= bossTimeScale(dt);   // t251: boss-kill hit-stop / slow motion
    {   // t251 BOSS_LOG: real frame times through the Warden fight and its death (FPS proof)
        static const bool blog = getenv("BOSS_LOG") != nullptr;
        if (blog && (enemies.wardenOn() || bdx.on) && state == State::Playing) fprintf(stderr, "FRAME raw=%.5f death=%d t=%.3f\n", GetFrameTime(), bdx.on ? 1 : 0, bdx.t);
    }
    time += dt; stateTime += dt;
    if (state == State::Title) titleRevealTime += dt;
    else if (state != State::LevelSelect && state != State::Options && state != State::Development) { titleRevealTime = 0.f; r3d.releaseTitleAssets(); }   // t231: menus keep the title scene
    if (IsKeyPressed(KEY_F1)) showFps = !showFps;
    if (IsKeyPressed(KEY_F5) || IsKeyPressed(KEY_R)) { reloadData(); fprintf(stderr, "RELOAD data\n"); }
    shake = std::max(0.f, shake - dt);
    view.shake = {0, 0};  // level 6 camera shake is render-only: never seen by input / gameplay
    fx.update(dt);
    vfx.toScreen = [this](Vector2 l) { return view.toScreen(l); };
    {   // EXPLO_TEST=1: detonate + bolt impacts at playfield centre (capture/bench hook);
        // EXPLO_FREEZE=t freezes the effects t seconds after the first detonation
        static const bool test = getenv("EXPLO_TEST") != nullptr;
        static const float freeze = getenv("EXPLO_FREEZE") ? (float)atof(getenv("EXPLO_FREEZE")) : -1;
        static const bool dentTest = getenv("DENT_TEST") != nullptr, breakTest = getenv("BREAK_TEST") != nullptr;
        static float tt = 6, age = 0, dt2 = 0; static int shots = 0; static bool frozen = false, fired = false;
        static Vector2 anchor{};
        static const Vector2* tgt = nullptr; static const void* own = nullptr;
        if (test && state == State::Playing && !frozen) {
            anchor = {view.forward() * 0.62f, view.lateral() * 0.5f};
            tt -= dt; age += dt;
            static float wait = 0;
            const void* pick = nullptr;  // aim at an on-screen enemy, picked at each detonation (not a stale one)
            if (tt <= 0) enemies.list.each([&](Enemy& e) { if (!pick && e.pos.x > view.forward() * 0.3f && e.pos.x < view.forward() * 0.92f) pick = &e; });
            if (tt <= 0 && !pick && (wait += dt) < 20) tt = 0.05f;  // none in view yet: wait for the wave
            if (tt <= 0) {
                tt = 1.4f; age = 0; shots = 0; fired = true;
                tgt = &anchor; own = nullptr;
                enemies.list.each([&](Enemy& e) { if (&e == pick) { tgt = &e.pos; own = &e; } });
                vfx.explode(view.toScreen({view.forward() * 0.38f, view.lateral() * 0.5f}), shipPx(1));
            }
            if (tgt && shots < 6 && age > shots * 0.12f) {  // bolts hitting a hull from the player's side
                const Vector2 hp = own && r3d.ok() ? r3d.hullContact(*tgt, {tgt->x, tgt->y + frand(-8, 8) * s}, {1, 0}) : Vector2{tgt->x - 22 * s, tgt->y};
                if (shots == 0) fprintf(stderr, "IMPACT_TEST ship=%.0f,%.0f hit=%.0f,%.0f own=%d\n", tgt->x, tgt->y, hp.x, hp.y, own != nullptr);
                vfx.impact(hp, view.dirToScreen({1, 0}), shipPx(1), tgt, own ? own : (const void*)((const char*)&anchor + shots), false);
                shots++;
            }
            if (freeze >= 0 && fired && age >= freeze) frozen = true;
        }
        if ((dentTest || breakTest) && state == State::Playing && !frozen && !test) {
            dt2 += dt; tt -= dt; age += dt;
            if (dentTest && dt2 > 0.25f) {  // DENT_TEST: bolts denting every on-screen enemy at random hull points
                dt2 = 0;
                enemies.list.each([&](Enemy& e) {
                    if (e.pos.x > view.forward()) return;
                    r3d.addDent(e.dent, e.nDent, e.dentNext, {e.pos.x + frand(-0.45f, 0.45f) * shipPx(e.def->scale), e.pos.y + frand(-0.15f, 0.15f) * shipPx(e.def->scale)}, e.pos, kLaserDamage * 3.f, r3d.enemyModel(e.def->presentation.model));
                });
            }
            if (breakTest && tt <= 0) {  // BREAK_TEST: a dented hull breaking up mid-screen every 1.8 s
                tt = 1.8f; age = 0; fired = true;
                float dn[8][4]{}; int n = 0, nx = 0;
                const Vector2 c{view.forward() * 0.5f, view.lateral() * 0.5f};
                for (int i = 0; i < 6; i++) r3d.addDent(dn, n, nx, {c.x + frand(-30, 30) * s, c.y + frand(-6, 6) * s}, c, 8);
                vfx.explode(view.toScreen(c), shipPx(1));
                r3d.spawnBreakup(c, 0, 0, dn, n, {120, 170, 255, 255});
            }
            if (freeze >= 0 && breakTest && fired && age >= freeze) frozen = true;
        }
        if (!frozen) {
            vfx.update(dt, s);
            static std::vector<Render3D::ChunkBlast> ev; ev.clear();
            r3d.updateBreakups(dt, ev);
            const auto& bc = r3d.breakCfg();
            for (auto& b : ev) {  // each chunk: its own smaller explosion + break-away shards
                vfx.explode(b.screen, shipPx(1) * bc.chunkExplosionScale * b.scale, b.scale > 1.f ? 0.8f : bc.chunkExplosionGain);
                vfx.shards(b.screen, b.dir, b.tint, {GetRandomValue(bc.shardsMin, bc.shardsMax), bc.shardSizeMin, bc.shardSizeMax,
                                                     bc.shardSpeedMin, bc.shardSpeedMax, bc.shardDrag, bc.shardLife, bc.maxShards});
            }
        } else vfx.follow();
        if (frozen) return;  // EXPLO_FREEZE capture: stop the whole sim so the hit hull stays under its effect
    }

    {   // per-level music: streams while playing, fades out on clear / leaving the level
        const bool inLevel = state == State::Playing && levelIdx >= 0 && levelIdx < (int)db.levels().size();
        // t231: the title theme plays across the menus and on through the intro after START / CONTINUE (then fades into
        // the level's song); on the title it waits for the scene's first load so the stream never stalls at its start
        static bool menuMusic = false;
        const bool menuState = state == State::Title || state == State::LevelSelect || state == State::Options || state == State::Development;
        menuMusic = menuState || (menuMusic && state == State::Cutscene);
        const bool titleMusic = menuMusic && (state != State::Title || ui::titleMusicReady(titleRevealTime));
        audio.music(titleMusic ? "music/title_theme.mp3" : inLevel ? db.levels()[levelIdx].music : std::string());
        audio.updateMusic(dt);
    }
    engineMute = std::max(0.f, engineMute - dt);
    audio.engine(state == State::Playing && engineMute <= 0);
    if (state != State::Playing) { audio.flamethrower(false); audio.plasma(false); audio.sizzle(false); }
    audio.hum(state != State::Title && state != State::LevelSelect && state != State::Options);  // whole play session
    switch (state) {
        case State::Title: case State::LevelSelect: case State::Options:
            titleBg.update(dt);
            if (getenv("OPTIONS_SHOT") && state == State::Title && stateTime > 1.f) { state = State::Options; stateTime = 0; }   // t260 test hook
            break;
        case State::Cutscene:
            cutscene.update(dt, audio);
            if (cutscene.done()) {
                if (afterCutscene == After::StartLevel) beginPlaying();
                else if (afterCutscene == After::NextStep) { step++; runStep(); }
                else state = State::Title;
            }
            break;
        case State::Playing: {
            const LevelDef& L = db.levels()[levelIdx];
            const bool pickupTestMode = getenv("PICKUP_TEST") != nullptr;
            const bool pickupEffectTest = getenv("PICKUP_TEST_COLLECT") != nullptr || getenv("PICKUP_TEST_MISS") != nullptr;
            bg.update(dt, player.edgeScroll * appr.scrollK);   // (Liftoff climb: forward speed bleeds off)
            updateCombatInput();
            {   // t251: AUTOPLAY_REST capture hook: the pilot rests in the Warden's rest zone 7 s of every 16 s while it is engaged
                static const bool rest = getenv("AUTOPLAY_REST") != nullptr;
                g_autopilotHold = {-1.f, -1.f};
                if (rest && enemies.safeOn && (enemies.warden.st == Enemies::WardenState::Engage || enemies.warden.st == Enemies::WardenState::Return)
                    && std::fmod(levelTime, 16.f) < 7.f)
                    g_autopilotHold = {enemies.safeRect.x + enemies.safeRect.width * 0.5f, enemies.safeRect.y + enemies.safeRect.height * 0.55f};
                if (enemies.safeOn && enemies.insideSafe(player.pos)) zoneTime += dt;
            }
            updatePlayer(dt);
            {   // edge throttle: ease the world scroll toward boost / slow / normal
                const float tgt = player.edgeHold > 0 ? kEdgeBoostMul : player.edgeHold < 0 ? kEdgeSlowMul : 1.f;
                player.edgeScroll = Lerp(player.edgeScroll, tgt, fminf(1.f, kEdgeBlendRate * dt));
                enemies.scrollMul = player.edgeScroll;
                corridorScrollTime += dt * player.edgeScroll;
                static const bool elog = getenv("EDGE_LOG") != nullptr;
                if (elog) { static float lt = 0; lt += dt; if (lt > 0.25f) { lt = 0; fprintf(stderr, "EDGE hold=%d scroll=%.3f\n", player.edgeHold, player.edgeScroll); } }
            }
            if (pickupTestMode) {
                // Deterministic asset-review scene: keep combat and scripted progression from
                // obscuring the five pickups. Production gameplay never enters this path.
                enemies.reset();
                weapons.lasers.clear(); weapons.missiles.clear();
            } else {
                enemies.playerHitR = std::max(player.def->size * 0.22f, player.def->size * (view.mode == Mode::SideScroll ? kShieldRadiusSide : kShieldRadiusTop)) * s;   // t251
                enemies.update(dt, player.pos, L, view, db, audio);
                enemies.list.each([&](Enemy& e) {
                    if (e.def->boss && view.mode == Mode::SideScroll && !enemies.wardenOn()) e.pos.y = std::min(e.pos.y, view.lateral() * 0.72f);
                    e.row = shipRow(view.mode, e.vel, -1, 60 * s);
                    for (int i = 0; i < std::min(e.nDent, 8); i++) {
                        float* fxState = e.surfaceFx[i];
                        fxState[kSurfacePlasmaHeat] = std::max(0.f, fxState[kSurfacePlasmaHeat] - 0.62f * dt);
                        fxState[kSurfaceFlameHeat] = std::max(0.f, fxState[kSurfaceFlameHeat] - 1.35f * dt);
                        fxState[kSurfaceChar] = std::min(1.f, fxState[kSurfaceChar] +
                            (fxState[kSurfacePlasmaHeat] * 0.10f + fxState[kSurfaceFlameHeat] * 0.06f) * dt);
                        fxState[kSurfaceCharSpread] = std::min(1.f, fxState[kSurfaceCharSpread] + fxState[kSurfaceChar] * 0.018f * dt);
                    }
                });
                updateLevel3Hazards(dt);
                updateGroundGuns(dt);
                updateHazardAvoidance(dt);
            }
            if (!pickupTestSpawned && pickupTestMode) {
                pickupTestSpawned = true; pickups.clear();
                const bool collectTest = getenv("PICKUP_TEST_COLLECT") != nullptr;
                const bool missTest = getenv("PICKUP_TEST_MISS") != nullptr;
                const bool maxTest = getenv("PICKUP_TEST_MAX") != nullptr;
                const int count = collectTest ? 1 : maxTest ? 32 : 5;
                for (int i = 0; i < count; i++) if (Pickup* p = pickups.spawn()) {
                    p->type = (PickupType)(i % 5);
                    if (collectTest) p->pos = player.pos;
                    else {
                        const int row = maxTest ? i / 8 : 0, col = maxTest ? i % 8 : i;
                        p->pos.x = missTest ? (kPickupVisualRadius + kPickupDriftSpeed * kPickupMissDuration + 2) * s
                                            : view.forward() * (maxTest ? 0.38f + 0.07f * row : 0.58f);
                        p->pos.y = view.lateral() * (col + 1.f) / (maxTest ? 9.f : 6.f);
                    }
                }
                fprintf(stderr, "PICKUP_TEST spawned=%d mode=%s\n", count, collectTest ? "collect" : missTest ? "miss" : maxTest ? "max" : "display");
            }
            if (!pickupTestMode || pickupEffectTest) collide();
            pickups.each([&](Pickup& p) {
                p.t += dt;
                if (p.state == PickupState::Active) {
                    if (!pickupTestMode || pickupEffectTest) {
                        p.pos.x -= kPickupDriftSpeed * s * dt * player.edgeScroll;
                        const float trigger = (kPickupVisualRadius + kPickupDriftSpeed * kPickupMissDuration) * s;
                        if (p.pos.x <= trigger) { p.state = PickupState::MissedFlash; p.stateTime = 0; }
                    }
                } else if (p.state == PickupState::MissedFlash) {
                    p.stateTime += dt; p.pos.x -= kPickupDriftSpeed * s * dt * player.edgeScroll;
                    if (p.stateTime >= kPickupMissDuration || p.pos.x <= kPickupVisualRadius * s) p.alive = false;
                } else {
                    p.stateTime += dt;
                    if (p.stateTime >= kPickupCollectDuration) p.alive = false;
                }
            });
            emitGlitter(dt);
            if (!pickupTestMode) updateBossDeath(dt);
            banner = std::max(0.f, banner - dt);
            if (state == State::Playing && !pickupTestMode) { updateLevel(dt); resolveHazardSpawns(); }
            updateApproach(dt);   // level 6 camera: sets the turn / shake used by the next draw
            updateFlightAudio(dt);   // t235: Liftoff / Planetfall sound (flight_audio.cpp)
            updateWardenSky(dt);  // L5: the sun approach (after updateApproach: it owns the scene light here)
            if (g_moonLevel) {
                g_moonLevel->update(dt, levelTime, player, weapons, fx, vfx, audio, r3d, view, s,
                                    [this](int dmg, Vector2 at) { damagePlayer(dmg, at); },
                                    score, shake);
                if (g_moonLevel && g_moonLevel->levelDone() && state == State::Playing) finishLevel();   // t252: after the moon explodes
                if (g_moonLevel && state == State::Playing && g_moonLevel->takeTrenchBlastDeath()) {   // t267: did not pull up: the blast took the ship
                    fprintf(stderr, "TRENCH blast death: lives %d -> %d\n", lives, lives - 1);
                    engineMute = 1.2f; shake = kShakeMax;
                    if (--lives <= 0) {
                        highScore = std::max(highScore, score); writeSave();
                        state = State::GameOver; stateTime = 0;
                        fprintf(stderr, "TRENCH blast death: GAME OVER\n");
                    } else {   // no checkpoints in the game: the usual retry is the level again; it restarts at the moon approach
                        beginPlaying();
                        levelTime = 13.0f;
                        hudMessage = "SHIP LOST - RETRY FROM THE APPROACH"; hudMessageT = 3.5f; hudMessageColor = Color{255, 200, 120, 255};
                        fprintf(stderr, "TRENCH retry from the approach (lives %d)\n", lives);
                        if (const char* d = getenv("TRENCH_SHOTS")) { if (FILE* f = fopen(TextFormat("%s/22_retry.txt", d), "w")) { fprintf(f, "retry lives %d\n", lives); fclose(f); } }
                    }
                }
            }
            updateEscapeShield();
            updateWeaponGifts(dt);
            weaponNoteT = std::max(0.f, weaponNoteT - dt); shieldNoteT = std::max(0.f, shieldNoteT - dt);
            hudMessageT = std::max(0.f, hudMessageT - dt);
            break;
        }
        case State::LevelClear:
            bg.update(dt);
            updateBossDeath(dt);   // t251: the shockwave / glow linger into the clear screen
            if (stateTime > 1.2f && (tapped() || ui::confirmPressed() || (getenv("AUTOCONTINUE") && stateTime > 2.5f))) {   // AUTOCONTINUE: capture runs
                const LevelDef& L = db.levels()[levelIdx];
                g_moonLevel.reset();
                if (freePlay) { state = State::LevelSelect; break; }
                if (!L.outro.empty()) playCutscene(L.outro, After::NextStep);
                else { step++; runStep(); }
            }
            break;
        case State::GameOver: case State::Victory:
            if (stateTime > 1.2f && (tapped() || ui::confirmPressed())) state = State::Title;
            break;
    }
}

// ============================================================ rendering
static int dbgMask() { static int m = getenv("DBG") ? atoi(getenv("DBG")) : 0; return m; }

void Game::drawWorld() const {
    const int dbg = dbgMask();
    const LevelDef& L = db.levels()[levelIdx];
    const bool playerVisible = state != State::GameOver && (player.invuln <= 0 || std::fmod(time, 0.16f) < 0.1f);

    const float k = rs;  // internal render scale (1 = native)
    Camera2D cam{}; cam.zoom = k;
    if (shake > 0) cam.offset = {frand(-1, 1) * shake * kShakePixels * s * k, frand(-1, 1) * shake * kShakePixels * s * k};

    if (appr.on && appr.liftoff && r3d.ok()) r3d.renderVolFog(approachScene(), appr.fog);   // own render targets: before the frame
    if (r3d.ok()) r3d.beginFrame(view, L.clear, cameraTurn);
    else ClearBackground(L.clear);  // clear is free on a tiler; a full-screen rect is not
    if (g_moonLevel && r3d.ok() && !(dbg & 1) && g_moonLevel->ownsFrame(levelTime)) {   // t252: the trench run draws the whole frame
        g_moonLevel->drawOwnedFrame(r3d, view, levelTime, &fx.glowTexture(), k);
        return;
    }
    const bool scissor = !(dbg & 16) && !appr.on;
    if (scissor) BeginScissorMode((int)(view.pf.x * k), (int)(view.pf.y * k), (int)std::ceil(view.pf.width * k), (int)std::ceil(view.pf.height * k));
    BeginMode2D(cam);
    prof::mark(prof::Bg);
    const bool wardenSky = wsky.on && r3d.ok() && !(dbg & 1);
    const bool moonSky = g_moonLevel && r3d.ok() && !(dbg & 1);
    if (!(dbg & 1) && !(appr.on && appr.cam3d && appr.rho < 0.5f) && !wardenSky && !moonSky) bg.draw();
    if (appr.on && appr.scenery && !(dbg & 1)) drawApproachBack(cam);
    if (wardenSky) drawWardenSky(cam);
    if (moonSky) { EndMode2D(); g_moonLevel->drawSky(r3d, view, levelTime, &fx.glowTexture(), k); BeginMode2D(cam); }
    if (r3d.ok() && !(dbg & 1)) {  // Ryan's 3D planets / terrain: own Mode3D pass right after the 2D bg
        if (moonSky) { EndMode2D(); g_moonLevel->draw3D(r3d, view, levelTime); BeginMode2D(cam); }
        static std::vector<Background::Bg3D> b3; bg.collect3d(b3);
        if (appr.on && !appr.terrain)  // level 6: the terrain is only there once we are through the clouds
            b3.erase(std::remove_if(b3.begin(), b3.end(), [](const Background::Bg3D& x) { return x.terrain; }), b3.end());
        if (!b3.empty()) { EndMode2D(); r3d.drawBackground3D(b3); BeginMode2D(cam); }
        // Level 3 is the enemy-ship infiltration route.  The temporary review-only gate has
        // served its purpose; ship-interior rendering is now part of the shipped level.
        if (L.id == "level_03") {
            EndMode2D(); r3d.drawShipCorridor(levelTime, corridorScrollTime);
            if (level3Turret.spawned && level3Turret.pos.x > -180.f * s && level3Turret.pos.x < view.forward() + 180.f * s)
                r3d.drawTurret(level3Turret.pos, level3Turret.aim, level3Turret.barrelAngle, level3Turret.recoil,
                    level3Turret.flashBarrel, level3Turret.muzzleFlash, !level3Turret.alive);
            if (level3Door.active)
                r3d.drawBulkhead(level3Door.x, level3Door.opening, levelTime, player.pos.x);
            BeginMode2D(cam);
        }
    }
    if (appr.on && appr.liftoff && appr.fog.on) {   // Liftoff: volumetric clouds + atmosphere beyond the ship
        EndMode2D(); r3d.drawVolFog(false, (float)GetScreenWidth(), (float)GetScreenHeight()); BeginMode2D(cam);
    } else if (appr.on && appr.haze > 0.001f)  // atmosphere haze the terrain emerges from (behind the ships)
        DrawRectangle(-10, -10, (int)W + 20, (int)H + 20, Fade(appr.liftoff ? Color{176, 150, 230, 255} : lerpCol({84, 64, 140, 255}, {170, 138, 222, 255}, ss(112.f, 124.f, appr.t)), appr.haze));  // pre-dawn: dim until the sun is up (Liftoff: daylight)
    if (appr.liftoff && levelTime < kLiftPadEnd + 1.f) {   // engine blast lighting the pad (on the ground, behind the ship)
        const float lift = ss(1.6f, kLiftPadEnd, levelTime), k = ss(0.2f, 1.4f, levelTime) * (1.f - lift);
        if (k > 0.01f) {
            const Texture2D& glw = fx.glowTexture(); const Rectangle gsr{0, 0, (float)glw.width, (float)glw.height};
            const Vector2 P = view.toScreen(player.pos); const float R = player.def->size * s * (1.4f + 1.2f * lift);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawTexturePro(glw, gsr, {P.x, P.y + 0.15f * R, 2.2f * R, 1.8f * R}, {1.1f * R, 0.9f * R}, 0, Fade({255, 150, 70, 255}, 0.35f * k));
            DrawTexturePro(glw, gsr, {P.x, P.y + 0.15f * R, 0.9f * R, 0.8f * R}, {0.45f * R, 0.4f * R}, 0, Fade({255, 236, 200, 255}, 0.35f * k));
            EndBlendMode();
        }
    }
    prof::mark(prof::Smoke);
    if (!(dbg & 2)) fx.drawSmoke();
    if (!appr.puffs.empty()) {   // Liftoff exhaust smoke: the glow texture is premultiplied white, so it stays white
        const Texture2D& gt = fx.glowTexture(); const Rectangle gs{0, 0, (float)gt.width, (float)gt.height};
        BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
        for (const auto& q : appr.puffs) {
            const float u = q.life / q.maxLife, al = q.a * sqrtf(u) * Clamp((1.f - u) / 0.06f, 0.f, 1.f);
            const unsigned char A = (unsigned char)(255.f * al), G = (unsigned char)(q.g * al);
            DrawTexturePro(gt, gs, {q.p.x, q.p.y, q.sz, q.sz}, {q.sz * 0.5f, q.sz * 0.5f}, 0, {G, G, (unsigned char)std::min(255, (int)(G * 1.04f)), A});
        }
        EndBlendMode();
    }
    prof::mark(prof::Additive);

    const float pPower = (1.f + (player.edgeScroll - 1.f) * kEdgeFlameGain) * (appr.liftoff ? appr.liftPower : 1.f);  // player flame: afterburner / throttled down at the edges (Liftoff: engine spool)

    // 3D ships drawn into the heat/scene RT (demo order).

    if (wsky.on && wsky.k > 0.05f) {   // L5 over the sun: dark rims under every hostile shot (and our bolts) so they read on the glare
        const Texture2D& gl = fx.glowTexture(); const Rectangle gr{0, 0, (float)gl.width, (float)gl.height};
        const float L = kLaserLength * kBoltQuadScale * view.s * 0.8f;
        enemies.shots.each([&](const EnemyShot& b) {
            const Vector2 p = view.toScreen(b.pos);
            const float d = b.bolt ? L * 0.7f : b.r * 5.f;
            DrawTexturePro(gl, gr, {p.x, p.y, d, d}, {d / 2, d / 2}, 0, Fade({20, 0, 30, 255}, 0.85f));
        });
        const float pl = kLaserLength * kBoltQuadScale * view.s;
        weapons.lasers.each([&](const Laser& l) {
            const Vector2 p = view.toScreen(l.pos), dd = Vector2Normalize(view.dirToScreen(l.vel));
            const Vector2 c = Vector2Subtract(p, Vector2Scale(dd, pl * 0.45f));
            DrawTexturePro(gl, gr, {c.x, c.y, pl * 1.1f, pl * 0.45f}, {pl * 0.55f, pl * 0.225f}, atan2f(dd.y, dd.x) * RAD2DEG, Fade({0, 10, 30, 255}, 0.6f));
        });
    }
    // additive batch: flames (sprites only in 2D), glows, weapons, particles, pickups glow
    const Texture2D& glow = fx.glowTexture();
    if (!(dbg & 4) && !appr.cam3d) weapons.drawFlameSmoke(view, glow);
    BeginBlendMode(BLEND_ADDITIVE);
    pickups.each([&](const Pickup& p) {
        Vector2 sp = view.toScreen(p.pos); float g = (60 + 8 * std::sin(p.t * 6) + 8 * pickupFlash(p)) * s;
        if (p.state == PickupState::CollectedDisperse) g *= 1.f - 0.65f * pickupDissolve(p);
        const float a = pickupOpacity(p);
        if (a > 0.001f) DrawTexturePro(glow, {0, 0, (float)glow.width, (float)glow.height},
                                       {sp.x, sp.y, g, g}, {g / 2, g / 2}, 0, Fade(pickupVisualColor(p), a));
    });
    if (!(dbg & 4) && !appr.cam3d) weapons.draw(view, glow);
    {   // enemy laser shots as warm bolts (additive), oriented along their velocity
        const Texture2D& bt = boltTexture(true);
        const float L = kLaserLength * kBoltQuadScale * view.s * 0.8f, Wd = L * kBoltAspect;
        enemies.shots.each([&](const EnemyShot& b) {
            if (!b.bolt) return;
            Vector2 p = view.toScreen(b.pos), d = view.dirToScreen(b.vel);
            DrawTexturePro(bt, {0, 0, (float)bt.width, (float)bt.height}, {p.x, p.y, L, Wd}, {L * 0.96f, Wd / 2}, std::atan2(d.y, d.x) * RAD2DEG, WHITE);
        });
    }
    if (!(dbg & 2)) fx.drawAdditive();
    if (g_moonLevel) g_moonLevel->draw2D(view, s);
    EndBlendMode();

    prof::mark(prof::Ships);
    prof::mark(prof::Shots);
    {   // enemy shots: two textured quads each (one texture, one batch) instead of two circle fans
        const Texture2D& dot = fx.glowTexture(); const Rectangle ds{0, 0, (float)dot.width, (float)dot.height};
        enemies.shots.each([&](const EnemyShot& b) {
            if (b.bolt) return;  // drawn in the additive pass
            Vector2 p = view.toScreen(b.pos); float d = b.r * 3.2f, c = b.r * 1.6f;
            DrawTexturePro(dot, ds, {p.x, p.y, d, d}, {d / 2, d / 2}, 0, b.col);
            DrawTexturePro(dot, ds, {p.x, p.y, c, c}, {c / 2, c / 2}, 0, WHITE);
        });
    }
    if (!r3d.pickupReady()) pickups.each([&](const Pickup& p) {
        Vector2 sp = view.toScreen(p.pos); int fs = (int)(22 * s); float a = pickupOpacity(p);
        DrawCircleV(sp, 13 * s * pickupExpand(p), Fade(BLACK, 0.5f * a));
        DrawText(pickupLetter(p.type), (int)(sp.x - MeasureText(pickupLetter(p.type), fs) / 2),
                 (int)(sp.y - fs / 2), fs, Fade(pickupVisualColor(p), a));
    });
    fx.drawText(s);
    EndMode2D();
    if (scissor) EndScissorMode();
    if (wardenSky && wsky.expo > 0.005f) {
        BeginBlendMode(BLEND_ALPHA);
        DrawRectangle(0, 0, (int)std::ceil(W * k), (int)std::ceil(H * k), Fade(BLACK, wsky.expo));
        EndBlendMode();
    }
    if (moonSky) {   // t248: the Warden's exposure-down (and the LEVEL CLEAR dim) ease off as we turn from the sun
        const float dim = g_moonLevel->skyDim(levelTime);
        if (dim > 0.005f) {
            BeginBlendMode(BLEND_ALPHA);
            DrawRectangle(0, 0, (int)std::ceil(W * k), (int)std::ceil(H * k), Fade(BLACK, dim));
            EndBlendMode();
        }
    }
    if (r3d.ok()) {
        // Demo order: ships drawn into the SAME scene RT as the 2D bg (still open), then
        // one blit + heat + flames. No second RT (that caused tearing / heat-without-ships).
        r3d.beginShips();
        if (r3d.pickupReady()) {
            r3d.beginPickupBatch();
            pickups.each([&](const Pickup& p) {
                r3d.drawPickup(p.pos, (int)p.type > (int)PickupType::Rapid ? (int)PickupType::Weapon : (int)p.type, (p.type == PickupType::Flamer || p.type == PickupType::Plasma ? 52 : 42) * s, pickupColor(p.type), pickupOpacity(p),
                                pickupFlash(p), pickupDissolve(p), pickupExpand(p));
            });
            r3d.endPickupBatch();
        }
        if (playerVisible) {
            r3d.drawShip(player.pos, player.def->size * s, false, pPitch, pRoll, WHITE, true, pPower,
                     nullptr, 0, nullptr, {0, 0, 0, 0}, player.hiddenAxis, weapons.heat);
        }
        enemies.list.each([&](const Enemy& e) {
            float ep = 0, er = 0;
            enemyAttitude(e, view.mode, s, ep, er);
            Color tint = e.flash > 0 ? WHITE : e.def->tint;
            if (e.def->presentation.model.empty()) {
                r3d.drawShip(e.pos, kEnemySize * e.def->scale * s, true, ep, er, tint, false, 0.8f, e.dent, e.nDent, nullptr, e.def->engineTint, e.hiddenAxis, 0.f, e.surfaceFx);
            } else {  // per-type animated GLB: Idle loops on the enemy's own clock, Fire plays after each shot
                EnemyAnim a;
                a.model = r3d.enemyModel(e.def->presentation.model);
                a.clips = &e.def->presentation.animationClips;
                a.idleT = e.t; a.fireT = e.fireAnimT;
                if (e.def->telegraph > 0 && e.fireT < e.def->telegraph) a.glow = Clamp(1.f - e.fireT / e.def->telegraph, 0.f, 1.f);
                if (e.burstLeft > 0) a.glow = 1.f;  // rotary: coil stays hot through the burst
                if (e.def->boss && enemies.wardenOn()) a.glow = std::max(a.glow, enemies.warden.glow);   // t251: attack telegraph
                r3d.drawShip(e.pos, kEnemySize * e.def->scale * s, true, ep, er, tint, false, 0.8f, e.dent, e.nDent, &a, e.def->engineTint, e.hiddenAxis, 0.f, e.surfaceFx);
            }
        });
        if (const Enemy* b = enemies.boss(); b && enemies.wardenOn()) {   // t251: charge orb + shield shell
            const auto& Wd = enemies.warden;
            if (Wd.chargeK > 0.01f) {
                r3d.addGlow(Wd.charge, b->hiddenAxis, 0.5f + 2.4f * Wd.chargeK, Fade(Wd.glowCol, Wd.chargeK));
                r3d.addGlow(Wd.charge, b->hiddenAxis, 0.2f + 0.8f * Wd.chargeK, Fade(WHITE, Wd.chargeK));
            }
            if (Wd.shield > 0.02f) {
                const float R = shipPx(b->def->scale) * 0.62f * CameraSystem::worldPerLocal(view) * CameraSystem::depthScale(view, b->hiddenAxis);
                const float pulse = 0.8f + 0.2f * std::sin(time * 5.f);
                r3d.addRing(b->pos, b->hiddenAxis, R, 0.f, Fade({110, 190, 255, 255}, 0.45f * Wd.shield * pulse));
                r3d.addRing(b->pos, b->hiddenAxis, R * 0.97f, 1.2f, Fade({110, 190, 255, 255}, 0.30f * Wd.shield * pulse), time * 0.4f);
            }
        }
        drawBossDeath3D();   // t251
        r3d.drawBreakups();  // dead ships' chunks (keep their dents)
        // The translucent shield goes last so ships at other depths sort correctly against it.
        if (playerVisible) r3d.drawPlayerShield(player.pos, pPitch, pRoll, player.shield / (float)kShieldMax, player.hiddenAxis);
        r3d.endShips();
        r3d.endFrame();       // blit scene (bg+ships) + heat refraction
        r3d.drawFlamesOnly(); // additive nozzle flames on backbuffer
    } else if (scissor) {
        EndScissorMode();
    }
    if (k != 1) rlScalef(k, k, 1);  // EndMode2D reset the matrix; panels + HUD draw in screen units
    if (!(dbg & 4) && !appr.cam3d) {
        BeginBlendMode(BLEND_ADDITIVE);
        weapons.drawBeamImpact(view, glow);
        EndBlendMode();
    }
    if (!(dbg & 2)) vfx.draw();     // over the 3D hulls (impacts sit on the model, explosions replace it)
    drawEnemyHealthBars();
    if (appr.on) { drawApproachFront(); return; }  // L6 is full screen: no playfield frame

    if (view.mode == Mode::TopDown) {  // side panels frame the vertical playfield
        DrawRectangle(0, 0, (int)view.pf.x, (int)H, {12, 12, 26, 255});
        DrawRectangle((int)(view.pf.x + view.pf.width), 0, (int)(W - view.pf.x - view.pf.width), (int)H, {12, 12, 26, 255});
        DrawLineEx({view.pf.x, 0}, {view.pf.x, H}, 2 * s, Fade(SKYBLUE, 0.4f));
        DrawLineEx({view.pf.x + view.pf.width, 0}, {view.pf.x + view.pf.width, H}, 2 * s, Fade(SKYBLUE, 0.4f));
    }
}

void Game::drawEnemyHealthBars() const {
    // Faint, thin bar just below a ship while it is being damaged. Anchored to the ship's drawn
    // screen point (the 3D eye-ray depth slide keeps that point), sized with its projected hull.
    const bool side = view.mode == Mode::SideScroll;
    for (const GroundGun& g : groundGuns) {   // Level 6 ground guns: same bar under the emplacement
        if (!g.t.alive || g.barAlpha <= 0.01f) continue;
        const Vector2 c = view.toScreen(g.t.pos);
        const float w = 46.f * s, h = std::max(2.f, 3.f * s), y = c.y + 30.f * s, a = g.barAlpha;
        const float frac = std::clamp(g.t.hp / 24.f, 0.f, 1.f);
        const Color fill = frac > 0.6f ? Color{90, 230, 120, 255} : frac > 0.3f ? Color{240, 210, 70, 255} : Color{240, 80, 60, 255};
        DrawRectangleRec({c.x - w / 2 - 1, y - 1, w + 2, h + 2}, Fade(BLACK, 0.35f * a));
        DrawRectangleRec({c.x - w / 2, y, w, h}, Fade({60, 70, 90, 255}, 0.35f * a));
        DrawRectangleRec({c.x - w / 2, y, w * frac, h}, Fade(fill, 0.6f * a));
    }
    enemies.list.each([&](const Enemy& e) {
        if (!e.alive || e.def->boss || e.barAlpha <= 0.01f) return;   // the boss has its own HUD bar
        if (e.pos.x < 0.f || e.pos.x > view.forward()) return;
        const float px = shipPx(e.def->scale) * CameraSystem::depthScale(view, e.hiddenAxis);
        Vector2 c = view.toScreen(e.pos);
        float y = c.y + (side ? 0.30f : 0.50f) * px, w = 0.55f * px;
        if (r3d.ok()) {  // just under the drawn hull: the model's projected bbox at the ship's depth
            const Rectangle R = r3d.shipScreenRect(e.pos, r3d.enemyModel(e.def->presentation.model), e.hiddenAxis);
            if (R.width > 0) { c.x = R.x + R.width * .5f; y = R.y + R.height + 3.f * s; w = (side ? .55f : .65f) * R.width; }
        }
        w = Clamp(w, 18.f * s, 90.f * s);
        const float h = std::max(2.f, 3.f * s);
        const float frac = std::clamp(e.hp / (float)std::max(1, e.maxHp), 0.f, 1.f);
        const float a = e.barAlpha;
        const Color fill = frac > 0.6f ? Color{90, 230, 120, 255} : frac > 0.3f ? Color{240, 210, 70, 255} : Color{240, 80, 60, 255};
        DrawRectangleRec({c.x - w / 2 - 1, y - 1, w + 2, h + 2}, Fade(BLACK, 0.35f * a));
        DrawRectangleRec({c.x - w / 2, y, w, h}, Fade({60, 70, 90, 255}, 0.35f * a));
        DrawRectangleRec({c.x - w / 2, y, w * frac, h}, Fade(fill, 0.6f * a));
    });
}

void Game::exitToMenu() {
    paused = false;
    weapons.lasers.each([&](Laser& l) { audio.stopLaser(l.flightSound); l.flightSound = 0; });
    g_moonLevel.reset();
    state = State::Title; stateTime = 0;   // music fades out (not in a level), engine loop stops
}

// Game::drawPauseMenu(): menus.cpp (t231).

void Game::drawHud() const {
    const LevelDef& L = db.levels()[levelIdx];
    const float panelW = view.mode == Mode::TopDown && !appr.on ? view.pf.x : 360 * s;
    DrawRectangleGradientEx({0, 0, panelW, 190 * s}, Fade(BLACK, 0.60f), Fade(BLACK, 0.f), Fade(BLACK, 0.f), Fade(BLACK, 0.f));
    const int fs = (int)(26 * s);
    float x = 12 * s;
    DrawText(TextFormat("SCORE %07d", score), (int)x, (int)(10 * s), fs, RAYWHITE);
    DrawText(TextFormat("HI %07d", highScore), (int)x, (int)(40 * s), (int)(18 * s), GOLD);
    const int nameFs = (int)(18 * s), nameY = (int)(62 * s);
    DrawText(L.name.c_str(), (int)x - 1, nameY, nameFs, BLACK); DrawText(L.name.c_str(), (int)x + 1, nameY, nameFs, BLACK);
    DrawText(L.name.c_str(), (int)x, nameY - 1, nameFs, BLACK); DrawText(L.name.c_str(), (int)x, nameY + 1, nameFs, BLACK);
    DrawText(L.name.c_str(), (int)x, nameY, nameFs, RAYWHITE);
    // Segmented glossy HP pills (top-left under score block) + thin shield strip + status text.
    {
        const int N = 10;
        const float segW = 18 * s, segH = 14 * s, gap = 3 * s, by = 86 * s;
        const float frac = std::clamp(std::max(0, player.hp) / (float)player.maxHp, 0.f, 1.f);
        Color c = frac > 0.6f ? Color{60, 220, 90, 255} : frac > 0.3f ? Color{240, 210, 50, 255} : Color{235, 55, 50, 255};
        if (frac <= 0.3f) { float k = 0.75f + 0.25f * sinf(time * 9.f); c = {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), 255}; }
        const Color dark = {(unsigned char)(c.r * 0.45f), (unsigned char)(c.g * 0.45f), (unsigned char)(c.b * 0.45f), 255};
        const int filled = (int)std::ceil(frac * N - 0.001f);
        for (int i = 0; i < N; i++) {
            Rectangle r{x + i * (segW + gap), by, segW, segH};
            DrawRectangleRounded({r.x - 1, r.y - 1, r.width + 2, r.height + 2}, 0.6f, 4, {0, 0, 0, 160});  // outline
            if (i < filled) {
                DrawRectangleRounded(r, 0.6f, 4, dark);                                                  // bottom/shadow
                DrawRectangleRounded({r.x, r.y, r.width, r.height * 0.78f}, 0.6f, 4, c);                // body
                DrawRectangleRounded({r.x + 2 * s, r.y + 2 * s, r.width - 4 * s, r.height * 0.28f}, 1.f, 4, {255, 255, 255, 120});  // specular
            } else {
                DrawRectangleRounded(r, 0.6f, 4, {40, 45, 60, 140});
            }
        }
        const float sw = N * (segW + gap) - gap, sy = by + segH + 4 * s;
        if (player.shield > 0) {
            const int sn = 10, sf = (int)std::ceil(sn * player.shield / (float)kShieldMax - 0.001f);
            const float w = (sw - (sn - 1) * 2 * s) / sn;
            for (int i = 0; i < sn; i++)
                DrawRectangleRounded({x + i * (w + 2 * s), sy, w, 5 * s}, 1.f, 3, i < sf ? Color{90, 220, 255, 230} : Color{40, 60, 80, 120});
        }
    DrawText(TextFormat("x%d  LV%d  M%d%s  HEAT %d%%", lives, weapons.level, weapons.ammo,
                        weapons.rapid > 0 ? "  RAPID" : "", (int)std::lround(weapons.heat * 100.f)),
                 (int)x, (int)(sy + 9 * s), (int)(18 * s), RAYWHITE);
        {   // t219: current primary + the plasma heat bar
            const float wy = sy + 32 * s;
            drawWeaponIcon({x + 13 * s, wy + 8 * s}, 15 * s, weapons.primary, 0.95f);
            const char* wn = primaryName(weapons.primary);
            DrawText(wn, (int)(x + 32 * s), (int)(wy), (int)(16 * s), RAYWHITE);
            if (weapons.primary == kPrimaryPlasma) {
                const float bx = x + 32 * s + MeasureText(wn, (int)(16 * s)) + 8 * s, bw = 110 * s, bh = 9 * s, byy = wy + 4 * s;
                const float hk = Clamp(weapons.plasmaHeat, 0.f, 1.f);
                const bool lock = weapons.plasmaLock > 0;
                Color hc = hk < 0.6f ? Color{120, 200, 255, 255} : hk < 0.85f ? Color{255, 200, 60, 255} : Color{255, 70, 50, 255};
                if (lock) hc = std::fmod(time, 0.3f) < 0.15f ? Color{255, 60, 40, 255} : Color{140, 20, 20, 255};
                DrawRectangleRounded({bx - 1 * s, byy - 1 * s, bw + 2 * s, bh + 2 * s}, 0.5f, 4, {0, 0, 0, 170});
                DrawRectangleRounded({bx, byy, bw * hk, bh}, 0.5f, 4, hc);
                if (lock) DrawText("OVERHEAT", (int)(bx + bw + 6 * s), (int)(wy), (int)(16 * s), hc);
            }
            if (showKeyboardHints && ownedCount() >= 2) DrawText("Q/E", (int)(x + 330 * s), (int)(wy), (int)(14 * s), Fade(GRAY, 0.8f));
        }
    }
    {   // t219: short captions (weapon unlocks / switches, Liftoff escape burn)
        auto caption = [&](const char* t, float y, Color c, float a) {
            const int f2 = (int)(30 * s); const int tw = MeasureText(t, f2);
            DrawRectangle((int)(W / 2 - tw / 2 - 14 * s), (int)(y - 6 * s), (int)(tw + 28 * s), (int)(f2 + 12 * s), Fade(BLACK, 0.45f * a));
            DrawText(t, (int)(W / 2 - tw / 2), (int)y, f2, Fade(c, a)); };
        if (banner > 0 && std::fmod(banner, 0.5f) < 0.35f) caption("WARNING: BOSS APPROACHING", H * 0.20f, RED, 0.95f);
        else if (shieldNoteT > 0.f) caption("SHIELD OFFLINE - ESCAPE BURN", H * 0.20f, {120, 200, 255, 255}, fminf(1.f, shieldNoteT / 0.5f));
        else if (shieldNoteT < 0.f) caption("SHIELD ONLINE", H * 0.20f, {120, 220, 255, 255}, fminf(1.f, -shieldNoteT / 0.5f));
        else if (weaponNoteT > 0.f) caption(weaponNote.c_str(), H * 0.20f, {255, 220, 120, 255}, fminf(1.f, weaponNoteT / 0.4f));
        else if (hudMessageT > 0.f && !hudMessage.empty()) caption(hudMessage.c_str(), H * 0.20f, hudMessageColor, fminf(1.f, hudMessageT / 0.25f));
    }
    if (showFps) {
        DrawText(TextFormat("3D %d FPS %d tris", GetFPS(), r3d.shipTris()), (int)(W - 260 * s), (int)(10 * s), (int)(20 * s), GREEN);
    }

    if (const Enemy* b = enemies.boss()) {  // big boss bar
        float w = view.pf.width * 0.6f, bx = view.pf.x + (view.pf.width - w) / 2, y = 70 * s;
        DrawRectangle((int)bx, (int)y, (int)w, (int)(14 * s), {60, 10, 10, 220});
        DrawRectangle((int)bx, (int)y, (int)(w * b->hp / b->maxHp), (int)(14 * s), {230, 40, 60, 255});
        const char* n = b->def->name.c_str();
        if (enemies.wardenOn()) {   // t251: phase + shield state
            if (enemies.warden.shield > 0.05f) DrawRectangle((int)bx, (int)y, (int)w, (int)(14 * s), Fade({110, 190, 255, 255}, 0.45f * enemies.warden.shield));
            n = TextFormat("%s  -  PHASE %d/4%s", b->def->name.c_str(), enemies.warden.phase + 1, enemies.bossShielded() ? "  -  SHIELDED" : "");
        }
        DrawText(n, (int)(bx + w / 2 - MeasureText(n, (int)(18 * s)) / 2), (int)(y - 23 * s), (int)(18 * s), RAYWHITE);
    }
    drawBossOverlay();   // t251
    if (state == State::Playing) drawTouchControls();
    drawEndOverlay();   // LEVEL CLEAR / GAME OVER (menus.cpp, t231)
}

void Game::drawTouchControls() const {
    const float r = kActionButtonRadius * s;
    const Vector2 fire{W - kFireRight * s, H - kFireBottom * s};
    const Vector2 secondary{W - kSecondaryRight * s, H - kSecondaryBottom * s};
    auto drawAction = [&](Vector2 c, bool pressed, bool enabled, const char* label) {
        const Color rim = enabled ? Fade(SKYBLUE, pressed ? 0.70f : 0.38f) : Fade(GRAY, 0.20f);
        const Color fill = enabled ? (pressed ? Color{35, 110, 145, 82} : Color{8, 24, 42, 58}) : Color{12, 14, 20, 38};
        DrawCircleV(c, r + 3 * s, Fade(BLACK, 0.18f));
        DrawCircleV(c, r, fill);
        DrawCircleLinesV(c, r, rim);
        DrawCircleLinesV(c, r - 3 * s, Fade(rim, 0.45f));
        const int fs = (int)(15 * s);
        DrawText(label, (int)(c.x - MeasureText(label, fs) / 2), (int)(c.y + r * 0.38f - fs / 2), fs, enabled ? Fade(RAYWHITE, 0.55f) : Fade(GRAY, 0.40f));
    };
    const bool missileReady = weapons.ammo > 0;
    drawAction(fire, combatInput.primaryHeld, true, "FIRE");
    drawAction(secondary, false, missileReady, "MISSILE");
    // Primary weapon glyph: the selected primary (laser bolt / flame / plasma lance).
    drawWeaponIcon({fire.x, fire.y - r * .09f}, r * 0.8f, weapons.primary, 0.7f);
    {   // t219: weapon switch (always shown; greyed out until two or more primaries are owned)
        const Vector2 sc = switchButtonPos(); const float sr = r * 0.72f;
        const bool on = ownedCount() >= 2;
        DrawCircleV(sc, sr + 3 * s, Fade(BLACK, 0.18f));
        DrawCircleV(sc, sr, on ? Color{8, 24, 42, 70} : Color{12, 14, 20, 38});
        DrawCircleLinesV(sc, sr, on ? Fade(SKYBLUE, 0.45f) : Fade(GRAY, 0.20f));
        drawWeaponIcon({sc.x, sc.y - sr * 0.12f}, sr * 0.75f, weapons.primary, on ? 0.8f : 0.3f);
        for (int i = 0; i < kPrimaryCount; i++) {   // owned dots, the selected one bright
            const bool own = (ownedWeapons >> i) & 1u;
            DrawCircleV({sc.x + (i - 1) * 9 * s, sc.y + sr * 0.55f}, 2.6f * s, !own ? Fade(GRAY, 0.18f) : i == weapons.primary ? Fade(WHITE, 0.85f) : Fade(SKYBLUE, 0.45f));
        }
        if (on) { const int f3 = (int)(11 * s); DrawText("SWAP", (int)(sc.x - MeasureText("SWAP", f3) / 2), (int)(sc.y - sr - 13 * s), f3, Fade(RAYWHITE, 0.45f)); }
    }
    // Secondary glyph: missile body, fin and its remaining inventory.
    DrawLineEx({secondary.x - r * .34f, secondary.y - r * .10f}, {secondary.x + r * .27f, secondary.y - r * .10f}, 4 * s, missileReady ? Fade(ORANGE, 0.72f) : Fade(GRAY, 0.35f));
    DrawTriangle({secondary.x + r * .42f, secondary.y - r * .10f}, {secondary.x + r * .20f, secondary.y - r * .24f}, {secondary.x + r * .20f, secondary.y + r * .04f}, missileReady ? Fade(ORANGE, 0.72f) : Fade(GRAY, 0.35f));
    const int ammoFs = (int)(13 * s);
    const char* ammo = TextFormat("x%d", weapons.ammo);
    DrawText(ammo, (int)(secondary.x - MeasureText(ammo, ammoFs) / 2), (int)(secondary.y + r * .12f), ammoFs, missileReady ? Fade(ORANGE, 0.72f) : Fade(GRAY, 0.45f));
}

bool Game::button(Rectangle r, const char* label, bool enabled, const Font* font) {
    Vector2 m = GetMousePosition();
    bool over = CheckCollisionPointRec(m, r);
    const bool titleStyle = font != nullptr;
    const bool pressed = over && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const Color face = titleStyle
        ? (enabled ? (over ? Color{20, 61, 91, 238} : Color{7, 20, 39, 224}) : Color{25, 25, 30, 200})
        : (enabled ? (pressed ? Color{60, 90, 160, 230} : Color{25, 35, 70, 220}) : Color{25, 25, 30, 200});
    DrawRectangleRounded(r, titleStyle ? 0.16f : 0.3f, 8, face);
    DrawRectangleRoundedLinesEx(r, titleStyle ? 0.16f : 0.3f, 8,
                                (titleStyle ? (over ? 2.4f : 1.4f) : 2.f) * s,
                                titleStyle ? (enabled ? Fade(over ? GOLD : SKYBLUE, over ? 0.95f : 0.62f) : DARKGRAY)
                                           : (enabled ? Fade(SKYBLUE, 0.8f) : DARKGRAY));
    int fs = (int)(r.height * 0.45f);
    if (font) {
        const float fontSize = (float)fs;
        const Vector2 textSize = MeasureTextEx(*font, label, fontSize, 1.2f * s);
        DrawTextEx(*font, label, {r.x + (r.width - textSize.x) / 2.f, r.y + (r.height - textSize.y) / 2.f - 1.f * s},
                   fontSize, 1.2f * s, enabled ? (over ? GOLD : RAYWHITE) : GRAY);
    } else {
        DrawText(label, (int)(r.x + r.width / 2 - MeasureText(label, fs) / 2), (int)(r.y + r.height / 2 - fs / 2), fs, enabled ? RAYWHITE : GRAY);
    }
    if (enabled && !tapConsumed && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r)) { tapConsumed = true; return true; }
    return false;
}

// Game::drawTitle(): the title screen lives in title.cpp (t228).

// Campaign position of a level (first step that plays it); levels outside the campaign follow it.
int Game::campaignPos(int li) const {
    const auto order = levelOrder();
    for (int k = 0; k < (int)order.size(); k++) if (order[k] == li) return k;
    return li;
}

// Level-select order = campaign order (L1 L2 L3 L6 L4 L7 L5 Warden, L8 Moon Breakup after it, t248), then any level not in the campaign.
// `unlocked` counts open entries of this list: finishing entry k opens entry k + 1.
std::vector<int> Game::levelOrder() const {
    std::vector<int> order;
    for (const CampaignStep& cs : db.campaign()) {
        const int li = db.levelIndex(cs.level);
        if (li >= 0 && std::find(order.begin(), order.end(), li) == order.end()) order.push_back(li);
    }
    for (int i = 0; i < (int)db.levels().size(); i++) if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    return order;
}

// Game::drawLevelSelect(), drawOptions(), drawDevelopment(): menus.cpp (t231).



void Game::draw() {
    tapConsumed = false;
    prof::mark(prof::Clear);
    BeginDrawing();
    switch (state) {
        case State::Title: ClearBackground({6, 6, 18, 255}); drawTitle(); break;
        case State::LevelSelect: ClearBackground({6, 6, 18, 255}); drawLevelSelect(); break;
        case State::Options: ClearBackground({6, 6, 18, 255}); drawOptions(); break;
        case State::Development: ClearBackground({6, 6, 18, 255}); drawDevelopment(); break;
        case State::Cutscene: cutscene.draw(W, H, s, fx.glowTexture()); break;
        case State::Victory: ClearBackground({6, 6, 18, 255}); drawVictory(); break;   // menus.cpp (t231)
        default: {
            const bool scaled = rs < 0.999f;
            if (scaled) {
                const int rw = (int)(W * rs), rh = (int)(H * rs);
                if (rt.id == 0 || rt.texture.width != rw || rt.texture.height != rh) {
                    if (rt.id) UnloadRenderTexture(rt);
                    rt = LoadRenderTexture(rw, rh);
                    SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
                    fprintf(stderr, "GFX render target %dx%d (scale %.2f)\n", rw, rh, rs);
                }
                BeginTextureMode(rt);
            }
            drawWorld(); prof::mark(prof::Hud); if (!(dbgMask() & 8)) drawHud();
            if (state == State::Playing) drawPauseMenu();
            if (scaled) {
                rlLoadIdentity();
                EndTextureMode();
                ClearBackground(BLACK);  // full clear = no tile preload on the screen pass
                rlDisableColorBlend();   // opaque upscale: one full-screen quad, no blending
                DrawTexturePro(rt.texture, {0, 0, (float)rt.texture.width, -(float)rt.texture.height}, {0, 0, W, H}, {}, 0, WHITE);
                rlDrawRenderBatchActive();
                rlEnableColorBlend();
            }
            break;
        }
    }
    if (static int dt = getenv("DRAWTEST") ? atoi(getenv("DRAWTEST")) : 0; dt > 0) {  // perf probe: forces 2*dt batch flushes
        const Texture2D& gt = fx.glowTexture();
        for (int i = 0; i < dt; i++) { DrawTexture(gt, i, 0, {255, 255, 255, 1}); DrawRectangle(i, 70, 1, 1, {255, 255, 255, 1}); }
    }
    if (static int ft = getenv("FILLTEST") ? atoi(getenv("FILLTEST")) : 0; ft > 0) {  // perf probe: full-screen fills
        const Texture2D& gt = fx.glowTexture(); static int mode = getenv("FILLMODE") ? atoi(getenv("FILLMODE")) : 0;
        if (mode == 0) BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < ft; i++) {
            if (mode == 2) DrawRectangle(0, 0, (int)W, (int)H, {0, 0, 0, 8});
            else DrawTexturePro(gt, {0, 0, (float)gt.width, (float)gt.height}, {0, 0, W, H}, {}, 0, {0, 0, 0, 8});
        }
        if (mode == 0) EndBlendMode();
    }
    if (const char* capture = getenv("TURRET_CAPTURE"); capture && *capture) {
        static bool captured = false;
        // raylib saves a screenshot in the working directory using only the file name.
        const char* name = GetFileName(capture);
        const bool wreckCapture = getenv("TURRET_CAPTURE_WRECK") != nullptr;
        const bool targetState = wreckCapture ? (level3Turret.spawned && !level3Turret.alive && level3Turret.wreckTime > .7f) : level3Turret.alive;
        if (!captured && state == State::Playing && targetState &&
            level3Turret.pos.x < view.forward() * (wreckCapture ? .95f : .8f) && !FileExists(name)) {
            captured = true; rlDrawRenderBatchActive(); TakeScreenshot(name);
        }
    }
    if (const char* capture = getenv("BULKHEAD_CAPTURE"); capture && *capture) {
        static bool captured = false;
        const char* name = GetFileName(capture);
        if (!captured && state == State::Playing && level3Door.active && level3Door.phase > 2.8f &&
            level3Door.x > view.forward() * .2f && level3Door.x < view.forward() * .7f && !FileExists(name)) {
            captured = true;
            rlDrawRenderBatchActive(); TakeScreenshot(name);
        }
    }
    if (const char* capture = getenv("BULKHEAD_CAPTURE_OPEN"); capture && *capture) {
        static bool captured = false;
        const char* name = GetFileName(capture);
        if (!captured && state == State::Playing && level3Door.active && level3Door.phase < 1.2f &&
            level3Door.x > view.forward() * .2f && level3Door.x < view.forward() * .8f && !FileExists(name)) {
            captured = true;
            rlDrawRenderBatchActive(); TakeScreenshot(name);
        }
    }
    if (const char* dir = getenv("DEATH_FRAMES"); dir && *dir && bdx.on && bdx.real < 9.5f) {   // t251: every frame of the boss death (video)
        static int n = 0;
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ImageResize(&img, 1920, (int)(1920.f * img.height / img.width) & ~1);   // full frame (ultrawide kept), even height for H.264
        ExportImage(img, TextFormat("%s/d%04d.png", dir, n++)); UnloadImage(img);
    }
    if (const char* dir = getenv("FLOW_SHOTS"); dir && *dir) {   // t248 capture runs: the level hand-over
        static int lastIdx = -1; static bool gotClear0 = false, gotClear1 = false; static float lastPlayShot = -10.f;
        const std::string& id = db.levels()[levelIdx].id;
        if (lastIdx != levelIdx) { lastIdx = levelIdx; gotClear0 = gotClear1 = false; lastPlayShot = -10.f; }
        auto grab = [&](const char* name) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            const int tw = img.height * 16 / 9;
            static const bool nocrop = getenv("FLOW_NOCROP") != nullptr;   // t251: keep the full (ultrawide) frame
            if (tw < img.width && !nocrop) ImageCrop(&img, Rectangle{(float)((img.width - tw) / 2), 0.f, (float)tw, (float)img.height});
            ExportImage(img, TextFormat("%s/%s", dir, name)); UnloadImage(img);
            fprintf(stderr, "FLOWSHOT %s t=%.2f\n", name, levelTime);
        };
        static const float every = getenv("FLOW_SHOT_EVERY") ? (float)atof(getenv("FLOW_SHOT_EVERY")) : 10.f;   // t250: any level
        if (state == State::Playing && id != "level_08" && levelTime - lastPlayShot >= every) { lastPlayShot = levelTime; grab(TextFormat("%s_%03ds.png", id == "level_05" ? "warden" : id.c_str(), (int)levelTime)); }
        if (state == State::LevelClear && id == "level_05" && !gotClear0) { gotClear0 = true; grab("warden_last.png"); }
        if (id == "level_05") {   // t251: boss proof captures (telegraph, rest zone, backed off, escalated phase, death)
            static int nTele = 0, nRest = 0, nP4 = 0, helpPhase = -1, nDeath = 0; static float lastTele = -9.f, lastRest = -9.f, lastP4 = -9.f;
            static bool gotSize = false;
            if (enemies.boss() && enemies.wardenOn() && state == State::Playing) {
                const auto& Wd = enemies.warden;
                if (!gotSize && Wd.st == Enemies::WardenState::Engage && Wd.phase == 0 && Wd.stT > 6.f) { gotSize = true; grab("t251_size.png"); }
                if (Wd.atkState == 1 && Wd.glow > 0.85f && nTele < 8 && levelTime - lastTele > 3.f) { lastTele = levelTime; grab(TextFormat("t251_tele_%d_p%d.png", nTele++, Wd.phase + 1)); }
                if (enemies.insideSafe(player.pos) && enemies.shots.count() > 8 && nRest < 6 && levelTime - lastRest > 4.f) { lastRest = levelTime; grab(TextFormat("t251_rest_%d_p%d.png", nRest++, Wd.phase + 1)); }
                if (Wd.st == Enemies::WardenState::Helpers && Wd.stT > 3.5f && helpPhase != Wd.phase) { helpPhase = Wd.phase; grab(TextFormat("t251_helpers_p%d.png", Wd.phase + 1)); }
                if (Wd.phase == 3 && Wd.atkState == 2 && Wd.atkT > 0.5f && nP4 < 4 && levelTime - lastP4 > 3.f) { lastP4 = levelTime; grab(TextFormat("t251_phase4_%d.png", nP4++)); }
            }
            static const float kDeathAt[7] = {0.02f, 0.9f, 1.9f, 2.42f, 2.7f, 3.3f, 4.6f};
            if (bdx.on && nDeath < 7 && bdx.t >= kDeathAt[nDeath]) grab(TextFormat("t251_death_%d_%.2fs.png", nDeath++, bdx.t));
        }
        if (state == State::LevelClear && id == "level_05" && !gotClear1 && stateTime > 2.3f) { gotClear1 = true; grab("warden_clear.png"); }
        static int moonFrames = 0; static float nextMoonShot = 0.f;
        if (state == State::Playing && id == "level_08" && g_moonLevel) {
            if (moonFrames++ == 0) grab("moon_first.png");
            if (levelTime >= nextMoonShot && levelTime < MoonLevel::kTrenchStart) {
                grab(TextFormat("moonflow_%04.1fs.png", levelTime));
                fprintf(stderr, "FLOWDIM t=%.2f dim=%.3f\n", levelTime, g_moonLevel->skyDim(levelTime));
                nextMoonShot += nextMoonShot < 2.f ? 0.5f : 2.f;
            }
        }
    }
    if (g_moonLevel) g_moonLevel->endOfFrame();   // t252: trench capture hooks
    prof::mark(prof::Present);
    EndDrawing();
    prof::end();
}

// ============================================================ graphics quality
const char* Game::gfxName() const { return gfxMode == 1 ? "HIGH" : gfxMode == 2 ? "LOW" : "AUTO"; }

void Game::applyQuality() {
    if (const char* e = getenv("RENDER_SCALE")) { rs = std::clamp((float)atof(e), 0.4f, 1.f); gfx::low = getenv("LOWFX") != nullptr; return; }
    if (gfxMode == 1) { rs = 1; gfx::low = false; }
    else if (gfxMode == 2) { rs = 1; gfx::low = true; }
    else { autoStep = std::clamp(autoStep, 0, 1); gfx::low = autoStep >= 1; rs = 1; }  // reduced render scale gave no gain on U11 (CPU/sync bound) and is env-only
}

// Auto: step 0 = high, 1 = low fx, 2.. = low fx + smaller internal scale. Steps down while
// gameplay averages under kAutoTargetFps over 2 s windows; never steps back up (no oscillation).
void Game::updateQuality(float dt) {
    if (gfxMode != 0 || getenv("RENDER_SCALE")) return;
    if (state != State::Playing) { qT = 0; qFrames = 0; return; }
    qT += dt; qFrames++;
    if (qT < 2.0f) return;
    float fps = qFrames / qT; qT = 0; qFrames = 0;
    if (fps < gfx::kAutoTargetFps && autoStep < 1) {
        autoStep++; applyQuality();
        fprintf(stderr, "GFX auto: %.0f fps -> step %d (low=%d scale=%.2f)\n", fps, autoStep, (int)gfx::low, rs);
    }
}

// ---- Level 6: planet approach --------------------------------------------------------------
// Gameplay is top-down for the whole level (one flight plane: positions, sizes, collision and aim
// never change). Only the camera moves.
//  0-24 s   the playfield image is turned 90 degrees (forward = screen-right, hulls in profile):
//           the side-scroller look, with combat until 20 s; then the enemies disengage.
//  24 s     invisible hand-off to a REAL 3D camera at the ship's side that frames the ship exactly
//           where / as big as the turned image did (Render3D::setRenderCamera draws the ships from
//           it; world positions still come from the gameplay camera).
//  24-42 s  that camera orbits round behind the ship and drops just below its rear centreline,
//           looking slightly up past it: low chase view, planet limb ahead / below, stars above.
//  42-80 s  chase approach, the planet grows to the low-orbit horizon of Ryan's reference.
//  80-100 s entry burn: plasma envelope, shake.   100-118 s cloud layers rush past; from 103 s the
//  camera rises and is exactly the overhead gameplay camera at 118 s, when the clouds are behind.
namespace {
struct ApCam { Vector3 P, T, up; float fovy; };
Vector3 nlerp3(Vector3 a, Vector3 b, float k) { return Vector3Normalize(Vector3Lerp(a, b, k)); }
}

// Level time -> authored approach timeline. Planetfall (approach_end E, duration D) ends at E: the
// part before the clouds (0..kApCloud) is stretched over D - (E - kApCloud) seconds and the clouds /
// rise / overhead hold run 1:1. A continuation level (approach_start A) runs A + t.
float Game::apTime(float lt) const {
    const LevelDef& L = db.levels()[levelIdx];
    if (L.liftoff) return liftT(lt);
    if (L.approachStart > 0.f) return L.approachStart + lt;
    if (L.approachEnd > kApCloud && L.duration > 0.f) {
        const float head = fmaxf(1.f, L.duration - (L.approachEnd - kApCloud));
        return lt < head ? lt * kApCloud / head : kApCloud + (lt - head);
    }
    return lt;
}
float Game::apLevelTime(float at) const {
    const LevelDef& L = db.levels()[levelIdx];
    if (L.liftoff) return liftLevelTime(at);
    if (L.approachStart > 0.f) return at - L.approachStart;
    if (L.approachEnd > kApCloud && L.duration > 0.f) {
        const float head = fmaxf(1.f, L.duration - (L.approachEnd - kApCloud));
        return at < kApCloud ? at * head / kApCloud : head + (at - kApCloud);
    }
    return at;
}

// Liftoff launch pad: the nearest flattened, prop-free site (the ground-gun pads) to the ship's
// spawn point, preferring one above the lowlands. The ground is scrolled so the pad lies exactly
// under the ship, the ship moves sideways onto it and sits ON it: its depth is the pad's depth
// below the flight plane less half the hull, so the hull rests on the surface (never inside it).
void Game::setupLiftoffPad() {
    const LevelDef& L = db.levels()[levelIdx];
    std::string model; for (const auto& b : L.layers) if (b.type == LayerType::Terrain3D) model = b.model;
    appr.padDepth = 0; appr.padPos = player.pos; appr.padPx = 0; appr.liftScroll = 0;
    if (model.empty() || !r3d.ensurePlanet(model)) return;
    const planet::Params* P = r3d.planetParams();
    if (!P) return;
    View v0 = view; v0.rot = 0; v0.shake = {0, 0};
    const Camera3D ref = CameraSystem::makeCamera(v0, 0.f);
    const float pxPerUnit = fmaxf(1.f, view.pf.height) / CameraSystem::kViewHeight;
    const Vector2 c{view.forward() * 0.5f, view.lateral() * 0.5f};
    const Vector3 f0 = CameraSystem::toWorld(v0, ref, c, 0.f);
    const Vector3 fx = Vector3Subtract(CameraSystem::toWorld(v0, ref, {c.x + 1.f, c.y}, 0.f), f0);
    const Vector3 fy = Vector3Subtract(CameraSystem::toWorld(v0, ref, {c.x, c.y + 1.f}, 0.f), f0);
    const Vector3 pw = CameraSystem::toWorld(v0, ref, player.pos, 0.f);
    const float halfH = CameraSystem::kViewHeight * 0.5f, halfW = halfH * view.pf.width / fmaxf(1.f, view.pf.height);
    planet::GunSite best{}; float bestScore = 1e9f; bool found = false;
    const long c0 = planet::gunCellOf(*P, pw.z);
    for (long cell = c0 - 6; cell <= c0 + 6; cell++) {
        const planet::GunSite g = planet::gunSite(*P, cell);
        if (fabsf(g.x) > halfW * 0.55f) continue;
        const float score = fabsf(g.x - pw.x) * 0.3f + fabsf(g.z - pw.z) * 0.05f + (g.y < 0.f ? 6.f - 4.f * g.y : 0.f);
        if (score < bestScore) { bestScore = score; best = g; found = true; }
    }
    if (!found) return;
    const float S = pw.z - best.z;                         // ground scroll (units) that brings the pad under the ship
    appr.padPx = S * pxPerUnit;
    bg.setTerrainOffset(appr.padPx);
    // local point whose focal-plane world position is (pad.x, ship z): invert the affine map
    const float ax = best.x - f0.x, az = pw.z - f0.z, det = fx.x * fy.z - fy.x * fx.z;
    if (fabsf(det) > 1e-8f) {
        const float a = (ax * fy.z - fy.x * az) / det, b = (fx.x * az - ax * fx.z) / det;
        player.pos = {Clamp(c.x + a, 0.f, view.forward()), Clamp(c.y + b, 0.f, view.lateral())};
    }
    appr.padPos = player.pos;
    const float wpl = CameraSystem::worldPerLocal(v0), shipW = player.def->size * s * wpl;
    const float dWorld = -(-Render3D::kGroundBase - best.y) + 0.16f * shipW;   // hull bottom on the pad
    appr.padDepth = dWorld / fmaxf(1e-5f, wpl);
    player.hiddenAxis = appr.padDepth;
    const float visHalfW = halfH * (view.pf.width + 2.f * view.vis) / fmaxf(1.f, view.pf.height);
    r3d.warmPlanet(model, -visHalfW - 1.2f, visHalfW + 1.2f, -halfH - 1.5f - S, halfH + 1.5f - S, 4.f);
    fprintf(stderr, "LIFTOFF pad x=%.2f z=%.2f y=%.2f scroll=%.2f depth=%.1f ship=(%.0f,%.0f)\n", best.x, best.z, best.y, S, appr.padDepth, player.pos.x, player.pos.y);
}

void Game::updateLiftoffPad(float dt) {
    const LevelDef& L = db.levels()[levelIdx];
    const float lt = levelTime;
    float speed = 0.f; for (const auto& b : L.layers) if (b.type == LayerType::Terrain3D) speed = b.speed;
    // the ground stays put under the parked ship; once off the pad it accelerates to cruise
    appr.liftScroll += speed * view.s * ss(2.6f, 7.5f, lt) * dt;
    bg.setTerrainOffset(appr.padPx + appr.liftScroll);
    const float lift = ss(1.6f, kLiftPadEnd, lt);           // vertical climb from the pad to the flight plane
    if (lt < kLiftPadEnd) player.hiddenAxis = Lerp(appr.padDepth, 0.f, lift);
    else if (player.hiddenAxis < -0.01f) player.hiddenAxis = 0.f;
    if (lt < 4.f) { player.pos = appr.padPos; player.vel = {}; }
    // engine blast: dust rolling out over the ground, strongest at lift-off, gone as the ship climbs
    const float k = ss(0.6f, 1.6f, lt) * (1.f - ss(2.2f, kLiftPadEnd + 0.5f, lt));
    static float acc = 0; acc += dt * 60.f * k;
    const Vector2 P = view.toScreen(appr.padPos);
    const float R = player.def->size * s;
    while (acc >= 1.f) {
        acc -= 1.f;
        const float a = frand(0, 2 * PI), v = frand(0.5f, 1.f) * 2.2f * R;
        const Vector2 d{cosf(a), sinf(a) * 0.9f};
        fx.dust(Vector2Add(P, Vector2Scale(d, 0.25f * R)), Vector2Scale(d, v), frand(0.25f, 0.45f) * R, frand(0.9f, 1.6f),
                Color{(unsigned char)frand(150, 185), (unsigned char)frand(120, 145), (unsigned char)frand(170, 205), 130});
    }
}

void Game::updateApproach(float dt) {
    const LevelDef& L = db.levels()[levelIdx];
    appr.on = L.approach && L.mode == Mode::TopDown;
    if (!appr.on) {
        if (view.rot != 0.f) { view.rot = 0; bg.setView(view); }
        r3d.setProfileRoll(0); r3d.setSceneLight(false); r3d.setRenderCamera(false); enemies.retreat = false;
        r3d.setGroundDrop(0.f); r3d.setClimbPitch(0.f); r3d.setFlameBoost(1.f); appr.fog.on = false; appr.scrollK = 1.f; appr.puffs.clear();
        bg.starAlpha = 1.f;
        return;
    }
    const float t = apTime(levelTime);
    appr.t = t;
    // Planetfall (approach_end) has no overhead combat any more: quiet from the turn to the end.
    appr.lull = t >= kApLullFrom && (t < kApLullTo || L.approachEnd > 0.f);
    enemies.retreat = appr.lull;
    appr.cam3d = t >= kApHand && t < kApOver;
    appr.o = ss(kApHand, kApOrbit, t);
    appr.rho = powf(ss(kApRise, kApOver, t), 1.6f);   // slow start: the chase look holds into the clouds
    // ---- planet distance (radii from its centre) ----
    if (t < kApHand) appr.dist = 30.f - 8.f * t / kApHand;
    else if (t < kApDesc) appr.dist = 22.f * powf(1.12f / 22.f, powf((t - kApHand) / (kApDesc - kApHand), 0.7f));
    else if (t < kApCloud) appr.dist = 1.12f * powf(1.03f / 1.12f, ss(kApDesc, kApCloud, t));
    else appr.dist = 1.03f * powf(1.004f / 1.03f, ss(kApCloud, kApOver, t));
    if (!L.liftoff) appr.spin += dt * (0.010f + 0.035f / appr.dist);
    appr.heat = ss(kApDesc, 90.f, t) * (1.f - ss(110.f, kApOver, t));
    appr.shakeAmp = ss(82.f, 92.f, t) * (1.f - ss(110.f, kApOver - 1.f, t)) * (0.7f + 0.3f * sinf(levelTime * 7.3f));
    appr.clouds = ss(kApCloud, 104.f, t) * (1.f - ss(114.f, kApOver, t));
    appr.haze = ss(108.f, 113.f, t) * (1.f - ss(116.f, 124.f, t));
    appr.terrain = t >= kApTerrain;
    appr.scenery = t < 115.f;
    appr.cityK = ss(2.2f, 5.f, appr.dist) * 0.5f;
    bg.starAlpha = t < kApHand ? 1.f - ss(kApT1, kApHand, t) : ss(kApOver - 4.f, kApOver, t);
    appr.stars3d = ss(kApT1, kApHand, t);
    // side frame dims, vanishes during the orbit, is back once overhead
    appr.panel = t < kApOver - 4.f ? 0.62f * (1.f - ss(kApHand, kApHand + 6.f, t)) : ss(kApOver - 4.f, kApOver, t);
    appr.liftoff = L.liftoff;
    if (L.liftoff) {   // Liftoff: near-vertical climb, volumetric deck + atmosphere, then out to space
        const float lt = levelTime;
        appr.lull = lt < kLiftCombatFrom || (lt >= kLiftClimb && t >= kApT1);   // quiet from the climb until the side view
        enemies.retreat = appr.lull;
        appr.heat = 0.f;                                                          // climbing out: no entry plasma
        appr.panel = 0.f;
        appr.alt = liftAlt(lt);
        appr.dist = 1.f + appr.alt;
        appr.spin += dt * (0.0004f + 0.012f * ss(0.02f, 0.6f, appr.alt));     // ground track: slow near the surface
        const float inDeck = ss(kLiftDeck0 * 0.85f, kLiftDeck0 * 1.25f, appr.alt) * (1.f - ss(kLiftDeck1 * 0.9f, kLiftDeck1 * 1.15f, appr.alt));
        appr.shakeAmp = 0.35f * inDeck * (0.7f + 0.3f * sinf(levelTime * 7.3f));  // camera buffeting in the deck (the hull stays still)
        appr.clouds = 0.f; appr.haze = 0.f;                                        // (volumetric now: see appr.fog)
        appr.terrain = lt < kLiftPullUp0 + 0.5f;                                   // the ground until the deck has swallowed it
        appr.scenery = lt >= kLiftPullUp0 - 0.5f;                                  // the globe from inside the deck on
        appr.scrollK = 1.f - 0.8f * ss(kLiftClimb, 89.f, lt);                      // forward speed bleeds off: the climb goes near vertical
        appr.drop = 20.f * ss(kLiftClimb, 91.5f, lt);                              // ...and the ground falls straight away below
        appr.climbPitch = 40.f * DEG2RAD * ss(kLiftClimb, 90.f, lt) * (1.f - ss(kLiftPullUp0, kLiftPullUp1, lt));   // nose up (the camera takes over the pitch)
        appr.padLock = lt < 4.f;
        appr.liftPower = lt < kLiftPadEnd + 1.f ? 0.25f + 0.75f * ss(0.2f, 1.6f, lt) + 0.35f * ss(1.6f, 2.4f, lt) * (1.f - ss(4.f, kLiftPadEnd + 1.f, lt))
                                                : 1.f + 0.3f * ss(kLiftClimb, 90.f, lt) * (1.f - ss(110.f, 118.f, lt));
        if (lt < 7.5f) updateLiftoffPad(dt);
        else if (appr.padDepth != 0.f) { player.hiddenAxis = 0.f; appr.padDepth = 0.f; }   // started past the pad (capture hook)
        // volumetric clouds + atmosphere (rendered in drawWorld before the frame)
        Render3D::VolFog& v = appr.fog;
        v.deck = {kLiftDeck0, kLiftDeck1};
        v.cloudK = 2600.f;   // thin, translucent wisps: the terrain reads through the thinner parts (also from orbit)
        // the cloud patches are volumetric all the way: the same clouds from inside, from above and from orbit
        v.gain = ss(kLiftClimb, 86.f, lt);
        v.on = v.gain > 0.003f;
        v.deckK = 1.f;
        v.cover = 0.62f;
        v.atmoK = 240.f * (1.f - ss(0.10f, 0.24f, appr.alt));     // the far-view atmosphere shell takes over the haze
        v.trackPatch = lt < kLiftPullUp0 + 2.f;                    // the patch we punch through stays where it is on the globe
        v.time = levelTime;
        v.wind = {levelTime * 0.02f, 0.f, levelTime * 0.008f};
        // camera .. ship distance in radii (the overhead camera is 20 world units above, the 3D camera ~9)
        v.sNear = (appr.cam3d ? 9.f : 20.f) * 7.9e-6f;
        // engine: 2.5x plume + a big billowing exhaust column for the punch-out, back to normal in space
        const float punch = ss(kLiftClimb, 86.5f, lt);
        appr.flameBoost = 1.f + 1.5f * punch * (1.f - ss(0.03f, 0.09f, appr.alt));   // 2.5x long and wide while punching out
        const float smokeK = punch * (1.f - ss(0.02f, 0.06f, appr.alt));
        if (smokeK > 0.01f && appr.shipPx > 1.f) {
            appr.smokeDebt += dt * 34.f * smokeK;
            Vector2 e = Vector2Subtract(appr.shipScr, appr.bowScr);
            const float el = Vector2Length(e); e = el > 1e-3f ? Vector2Scale(e, 1.f / el) : Vector2{0, 1};
            const Vector2 pe{-e.y, e.x};
            while (appr.smokeDebt >= 1.f) {
                appr.smokeDebt -= 1.f;
                Approach::Puff q;
                q.sz = appr.shipPx * frand(0.55f, 0.85f);
                // overhead camera: we climb toward the lens, so the column stays under the ship and swells
                // there (slow on screen); chase camera: it streams back along the exhaust
                const bool td = !appr.cam3d;
                q.p = Vector2Add(appr.shipScr, Vector2Add(Vector2Scale(e, appr.shipPx * (td ? frand(0.7f, 1.0f) : frand(1.2f, 1.7f))), Vector2Scale(pe, appr.shipPx * frand(-0.12f, 0.12f))));
                q.v = Vector2Add(Vector2Scale(e, (td ? frand(0.5f, 1.0f) : frand(2.4f, 3.6f)) * appr.shipPx), Vector2Scale(pe, frand(-0.6f, 0.6f) * appr.shipPx));
                q.grow = appr.shipPx * frand(1.3f, 2.0f);
                q.maxLife = q.life = frand(2.4f, 3.4f);
                q.a = (td ? 0.62f : 0.80f) * smokeK; q.g = (unsigned char)frand(196, 248);
                if (appr.puffs.size() < 200) appr.puffs.push_back(q);
            }
        } else appr.smokeDebt = 0.f;
        static const bool llog = getenv("TERRAIN_LOG") != nullptr;
        if (llog && (int)(lt * 2.f) != (int)((lt - dt) * 2.f))
            fprintf(stderr, "LIFT lt=%.1f alt=%.5f rho=%.2f ks=%.2f fog=%d gain=%.2f deckK=%.2f pitch=%.1f scroll=%.2f drop=%.1f terr=%d scen=%d\n", lt, appr.alt, appr.rho, appr.kside,
                    (int)v.on, v.gain, v.deckK, appr.climbPitch * RAD2DEG, appr.scrollK, appr.drop, (int)appr.terrain, (int)appr.scenery);
        if (llog && (int)(lt * 2.f) != (int)((lt - dt) * 2.f)) { const Vector2 P = view.toScreen(player.pos), dy = view.dirToScreen({0, 1}), dx = view.dirToScreen({1, 0});
            fprintf(stderr, "PUFF n=%d shipPx=%.1f scr=(%.0f,%.0f) bow=(%.0f,%.0f) P=(%.0f,%.0f) dx=(%.2f,%.2f) dy=(%.2f,%.2f) sz=%.1f cam3d=%d\n", (int)appr.puffs.size(), appr.shipPx,
                    appr.shipScr.x, appr.shipScr.y, appr.bowScr.x, appr.bowScr.y, P.x, P.y, dx.x, dx.y, dy.x, dy.y, player.def->size * view.s, (int)appr.cam3d); }
    } else { appr.padLock = false; appr.liftPower = 1.f; appr.drop = 0.f; appr.scrollK = 1.f; appr.climbPitch = 0.f; appr.fog.on = false; }
    r3d.setGroundDrop(appr.drop);
    r3d.setClimbPitch(appr.climbPitch);
    r3d.setFlameBoost(appr.liftoff ? appr.flameBoost : 1.f);
    for (auto& q : appr.puffs) {   // exhaust billows: drift back along the exhaust, slow down, swell, fade
        q.p = Vector2Add(q.p, Vector2Scale(q.v, dt)); q.v = Vector2Scale(q.v, expf(-1.8f * dt));   // slows and lingers behind the ship
        q.sz += q.grow * dt; q.life -= dt;
    }
    appr.puffs.erase(std::remove_if(appr.puffs.begin(), appr.puffs.end(), [](const Approach::Puff& q) { return q.life <= 0.f; }), appr.puffs.end());

    // ---- hand-off: from the turned image to the real camera ----
    const float F = view.forward(), Lt = view.lateral();
    if (!appr.handDone && t >= kApHand && !L.liftoff) {
        appr.handDone = true;
        View vf = view; vf.rot = PI * 0.5f; vf.shake = {0, 0};
        appr.qHand = vf.toScreen(player.pos);
        enemies.reset(); pickups.clear(); weapons.lasers.clear(); weapons.missiles.clear();
    }
    if (!appr.handDone || L.liftoff) { View vf = view; vf.rot = PI * 0.5f; vf.shake = {0, 0}; appr.qHand = vf.toScreen(player.pos); }
    view.rot = appr.cam3d || t >= kApOver ? 0.f : PI * 0.5f;
    r3d.setProfileRoll(view.rot);
    view.shake = {0, 0};
    if (appr.cam3d) {  // chase framing: forward eased to a fixed station, lateral kept in the middle half
        const float kx = fminf(1.f, 2.5f * dt);
        player.pos.x = Lerp(player.pos.x, 0.22f * F, kx);
        player.pos.y = Lerp(player.pos.y, Clamp(player.pos.y, 0.25f * Lt, 0.75f * Lt), kx * 2.f);
    }

    // ---- cameras ----
    View v0 = view; v0.rot = 0; v0.shake = {0, 0};
    const Camera3D ref = CameraSystem::makeCamera(v0, cameraTurn);
    const Vector3 S = CameraSystem::toWorld(v0, ref, player.pos, player.hiddenAxis);
    const float Dc0 = Vector3Distance(ref.position, ref.target);
    if (L.liftoff) {   // Liftoff cameras: overhead -> (pull-up) front camera -> (orbit) side camera
        const float lt = levelTime;
        appr.rho = 1.f - ss(kLiftPullUp0, kLiftPullUp1, lt);
        appr.kside = ss(kLiftOrbit0, kLiftSide, lt);
        appr.o = 1.f - appr.kside;
    }
    const float o = appr.o, rho = appr.rho;
    const Vector3 Y{0, 1, 0}, FW{0, 0, -1};
    ApCam c;
    Vector3 fC, uC, rC, fF{0, 0, -1}, uF{0, 1, 0};   // (fF/uF: Liftoff front-camera basis)
    if (!L.liftoff) {
    const float yaw = -0.5f * PI * o;
    Vector3 V{-cosf(yaw), 0, sinf(yaw)};                 // (-1,0,0) side ... (0,0,-1) behind
    V = Vector3Normalize(Vector3Add(V, Vector3Scale(Y, tanf(4.f * DEG2RAD * o))));  // camera below, looking slightly up
    const float Dc = Lerp(Dc0, 9.f, o), fov = Lerp(ref.fovy, 50.f, o);
    const Vector3 R = Vector3Normalize(Vector3CrossProduct(V, Y)), U = Vector3CrossProduct(R, V);
    const Vector2 qC{W * 0.5f, H * 0.56f};
    const Vector2 q = Vector2Lerp(appr.qHand, qC, o);
    const float kpx = 2.f * Dc * tanf(fov * 0.5f * DEG2RAD) / H;   // world units per pixel at the ship
    Vector3 Sf = S; Sf.x = Lerp(ref.target.x, S.x, Lerp(1.f, 0.6f, o));
    const Vector3 T0 = Vector3Add(Sf, Vector3Add(Vector3Scale(R, -(q.x - W * 0.5f) * kpx), Vector3Scale(U, (q.y - H * 0.5f) * kpx)));
    c = {Vector3Subtract(T0, Vector3Scale(V, Dc)), T0, U, fov};
    // chase basis (before the rise) drives the sun so it does not swing with the rising camera
    fC = V; uC = U; rC = R;
    } else {
        // Front camera: ahead of and above the ship looking back down along its path (the overhead
        // view pitched 60 deg about its right axis): the nose keeps pointing up the screen, the planet,
        // the deck and the exhaust fall away behind/below it. Side camera: the turned image's camera.
        const Vector3 fO = Vector3Normalize(Vector3Subtract(ref.target, ref.position));
        const Vector3 rO = Vector3Normalize(Vector3CrossProduct(fO, ref.up)), uO = Vector3CrossProduct(rO, fO);
        auto basisQ = [](Vector3 f, Vector3 u) {
            const Vector3 r = Vector3Normalize(Vector3CrossProduct(f, u)), uu = Vector3CrossProduct(r, f), b = Vector3Negate(f);
            Matrix M = {r.x, uu.x, b.x, 0, r.y, uu.y, b.y, 0, r.z, uu.z, b.z, 0, 0, 0, 0, 1};
            return QuaternionNormalize(QuaternionFromMatrix(M));
        };
        const Quaternion qO = basisQ(fO, uO);
        const Quaternion qF = QuaternionNormalize(QuaternionMultiply(QuaternionFromAxisAngle(rO, -60.f * DEG2RAD), qO));
        const Quaternion qS = basisQ({-1, 0, 0}, Y);
        const float ks = appr.kside;
        const Quaternion qc = QuaternionSlerp(qF, qS, ks);
        const Vector3 V = Vector3Normalize(Vector3RotateByQuaternion({0, 0, -1}, qc)), U = Vector3Normalize(Vector3RotateByQuaternion({0, 1, 0}, qc));
        const Vector3 R = Vector3Normalize(Vector3CrossProduct(V, U));
        const float Dc = Lerp(9.f, Dc0, ks), fov = Lerp(50.f, ref.fovy, ks);
        const Vector2 q = Vector2Lerp({W * 0.5f, H * 0.42f}, appr.qHand, ks);
        const float kpx = 2.f * Dc * tanf(fov * 0.5f * DEG2RAD) / H;
        Vector3 Sf = S; Sf.x = Lerp(ref.target.x, S.x, Lerp(1.f, 0.6f, 1.f - ks));
        const Vector3 T0 = Vector3Add(Sf, Vector3Add(Vector3Scale(R, -(q.x - W * 0.5f) * kpx), Vector3Scale(U, (q.y - H * 0.5f) * kpx)));
        c = {Vector3Subtract(T0, Vector3Scale(V, Dc)), T0, U, fov};
        fC = V; uC = U; rC = R;
        fF = Vector3RotateByQuaternion({0, 0, -1}, qF); uF = Vector3RotateByQuaternion({0, 1, 0}, qF);
    }
    // rise to overhead
    c.P = Vector3Lerp(c.P, ref.position, rho); c.T = Vector3Lerp(c.T, ref.target, rho);
    c.up = nlerp3(c.up, ref.up, rho); c.fovy = Lerp(c.fovy, ref.fovy, rho);
    // shake: camera jitter (angles), shared by the scenery camera
    const float ja = appr.shakeAmp * 0.022f;
    appr.jitter = {frand(-1, 1) * ja, frand(-1, 1) * ja};
    Vector3 f = Vector3Normalize(Vector3Subtract(c.T, c.P));
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, c.up)), u = Vector3CrossProduct(r, f);
    const float cd = Vector3Distance(c.P, c.T);
    if (appr.cam3d) {
        Camera3D rc{}; rc.position = c.P; rc.up = u; rc.fovy = c.fovy; rc.projection = CAMERA_PERSPECTIVE;
        rc.target = Vector3Add(c.T, Vector3Scale(Vector3Add(Vector3Scale(r, appr.jitter.x), Vector3Scale(u, appr.jitter.y)), cd));
        r3d.setRenderCamera(true, rc);
    } else r3d.setRenderCamera(false);
    if (!appr.cam3d && t < kApHand) { f = {-1, 0, 0}; u = Y; r = Vector3CrossProduct(f, u); }   // the turned image = side camera
    appr.f = f; appr.u = u; appr.fovy = appr.cam3d ? c.fovy : ref.fovy;
    // screen points for the plasma (logical px)
    {
        const Vector3 fj = Vector3Normalize(Vector3Add(f, Vector3Add(Vector3Scale(r, appr.jitter.x), Vector3Scale(u, appr.jitter.y))));
        const Vector3 rj = Vector3Normalize(Vector3CrossProduct(fj, u)), uj = Vector3CrossProduct(rj, fj);
        const float fp = H * 0.5f / tanf(appr.fovy * 0.5f * DEG2RAD);
        auto proj = [&](Vector3 w) { const Vector3 d = Vector3Subtract(w, c.P); const float z = fmaxf(0.05f, Vector3DotProduct(d, fj));
                                     return Vector2{W * 0.5f + Vector3DotProduct(d, rj) / z * fp, H * 0.5f - Vector3DotProduct(d, uj) / z * fp}; };
        const float shipW = player.def->size * s * CameraSystem::worldPerLocal(v0);
        appr.shipScr = proj(S);
        appr.bowScr = proj(Vector3Add(S, Vector3Scale(FW, 0.55f * shipW)));
        appr.wingL = proj(Vector3Add(S, {-0.5f * shipW, 0, 0.1f * shipW}));
        appr.wingR = proj(Vector3Add(S, {0.5f * shipW, 0, 0.1f * shipW}));
        appr.shipPx = shipW * fp / fmaxf(0.05f, Vector3DotProduct(Vector3Subtract(S, c.P), fj));
        const Vector3 td = Vector3Normalize({0, (L.liftoff ? 1.f : -1.f) * sinf(12.f * DEG2RAD), -cosf(12.f * DEG2RAD)});  // flight path (descending / Liftoff: climbing)
        const float x = Vector3DotProduct(td, rj), y = Vector3DotProduct(td, uj), z = Vector3DotProduct(td, fj);
        Vector2 fl = z > 0.05f ? Vector2{W * 0.5f + x / z * fp, H * 0.5f - y / z * fp} : Vector2Add({W * 0.5f, H * 0.5f}, Vector2Scale(Vector2Normalize({x, -y}), 4000.f));
        appr.flow = {Clamp(fl.x, -4000.f, W + 4000.f), Clamp(fl.y, -4000.f, H + 4000.f)};
    }
    // ---- planet direction: camera-relative while far / turning, then along the flight path, so
    // the forward ray dives into the planet just inside its limb (limb above the screen centre).
    {
        const Vector3 side = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(r, 0.20f), Vector3Scale(u, -0.17f)), Vector3Scale(f, 0.96f)));
        const float th = L.liftoff ? fminf(0.5f * PI, asinf(fminf(1.f, 1.f / appr.dist)) + 10.f * DEG2RAD)   // Liftoff: limb below the flight path
                                   : asinf(fminf(1.f, 0.92f / appr.dist));
        const Vector3 traj = Vector3Normalize(Vector3Subtract(Vector3Scale(FW, cosf(th)), Vector3Scale(Y, sinf(th))));
        appr.pdir = nlerp3(side, traj, ss(kApHand, kApOrbit + 6.f, t));
    }
    // ---- ONE sun: just behind the limb toward screen right-up (sunrise), then rising after 118 s
    {
        const Vector3 bf = t < kApHand ? f : nlerp3(f, fC, 1.f), bu = t < kApHand ? u : uC, br = t < kApHand ? r : rC;
        (void)bf;
        const float beta = Lerp(0.3f, 1.15f, appr.o);
        const float angR = asinf(fminf(1.f, 1.f / fmaxf(appr.dist, 1.0005f)));
        appr.sunTheta = Lerp(angR - 1.6f * DEG2RAD, 140.f * DEG2RAD, ss(kApOver, 135.f, t));
        Vector3 w = Vector3Add(Vector3Scale(br, cosf(beta)), Vector3Scale(bu, sinf(beta)));
        const Vector3 pd = appr.pdir;
        w = Vector3Normalize(Vector3Subtract(w, Vector3Scale(pd, Vector3DotProduct(w, pd))));
        appr.sun = Vector3Normalize(Vector3Add(Vector3Scale(pd, cosf(appr.sunTheta)), Vector3Scale(w, sinf(appr.sunTheta))));
    }
    if (L.liftoff) {   // the planet is BEHIND / below the climbing ship in every view; ONE sun ahead of it
        const float lt = levelTime;
        const float fpS = H * 0.5f / tanf(ref.fovy * 0.5f * DEG2RAD);
        const Vector3 fS{-1, 0, 0}, uS = Y, rS = Vector3CrossProduct(fS, uS);   // the side (turned image) camera
        auto onSide = [&](float sx, float sy) {   // world direction seen at screen fraction (sx, sy) of the side view
            return Vector3Normalize(Vector3Add(fS, Vector3Add(Vector3Scale(rS, (sx - 0.5f) * W / fpS), Vector3Scale(uS, (0.5f - sy) * H / fpS)))); };
        const float far = ss(1.8f, 7.f, appr.dist);
        const Vector3 pOver{0, -1, 0};
        const Vector3 pFront = Vector3Normalize(Vector3Subtract(Vector3Scale(fF, cosf(50.f * DEG2RAD)), Vector3Scale(uF, sinf(50.f * DEG2RAD))));
        const Vector3 pSide = onSide(Lerp(-0.25f, 0.10f, far), Lerp(0.62f, 0.66f, far));   // left of (behind) the ship, a little low
        appr.pdir = appr.rho > 0.f ? nlerp3(pFront, pOver, appr.rho) : nlerp3(pFront, pSide, appr.kside);
        const Quaternion qp = QuaternionFromVector3ToVector3(pOver, appr.pdir);   // the planet turns with it: same ground below us
        appr.prot = QuaternionToMatrix(qp);
        // sun: fixed in the planet's sky (high, as over the pad) during the climb, then composed upper right ahead
        const Vector3 day = Vector3Normalize({-0.48f, 0.87f, -0.08f});
        appr.sun = nlerp3(Vector3RotateByQuaternion(day, qp), onSide(0.72f, 0.26f), ss(kLiftOrbit0 - 4.f, kLiftSide, lt));
    }
    // ---- ship / terrain light from that sun ----
    if (t < kApHand) {  // turned image: same screen-relative light as the side camera would give
        const Vector3 sun = appr.sun;
        const float lx = Vector3DotProduct(sun, r), ly = Vector3DotProduct(sun, u), lz = -Vector3DotProduct(sun, f);
        const float cr = cosf(view.rot), sr = sinf(view.rot);
        const float px = lx * cr - ly * sr, py = lx * sr + ly * cr;
        r3d.setSceneLight(true, Vector3Negate(Vector3Normalize({px, lz, -py})), L.liftoff ? 0.30f : 0.20f, 0.16f);
    } else {            // real camera: the scenery world IS the gameplay world
        r3d.setSceneLight(true, Vector3Negate(appr.sun), 0.20f, 0.16f);
    }
    bg.setView(view);
}

Render3D::ApproachScene Game::approachScene() const {
    Render3D::ApproachScene sc;
    sc.f = appr.f; sc.u = appr.u; sc.pdir = appr.pdir; sc.sun = appr.sun; sc.fovy = appr.fovy; sc.dist = appr.dist;
    sc.spin = appr.spin; sc.cloudSpin = appr.spin * 1.3f + 0.4f; sc.cityK = appr.cityK; sc.stars = appr.stars3d; sc.jitter = appr.jitter;
    if (appr.liftoff) {
        static const float sunR = getenv("LIFT_SUNR") ? (float)atof(getenv("LIFT_SUNR")) * DEG2RAD : kLiftSunR;   // review hook: sun close-up
        sc.sunR = sunR; sc.time = levelTime;
        sc.prot = appr.prot;
        sc.cloudSpin = appr.spin;                     // the cloud shell is the deck seen from afar: it moves with the ground
        sc.cloudR = 1.f + kLiftDeck1;                 // ...at the deck's height (the volumetric deck hands over to it)
        sc.cloudA = 0.f;                              // the volumetric patches are the clouds from orbit too (no texture shell)
        sc.nebula = ss(0.04f, 0.35f, appr.alt);       // the gas / dust sky once the daylight sky is gone
        sc.atmoK = ss(0.10f, 0.24f, appr.alt);        // the far-view shell takes over from the volumetric atmosphere
    }
    for (const BgLayer& layer : db.levels()[levelIdx].layers) if (layer.type == LayerType::Nebula) {
        sc.nebulaTint = layer.color;
        break;
    }
    return sc;
}

void Game::drawApproachBack(const Camera2D& cam) const {
    if (!r3d.ok()) return;
    EndMode2D();
    const Render3D::ApproachScene sc = approachScene();
    Render3D::ApproachOut out;
    r3d.drawApproachScenery(sc, out);
    if (out.sunOn && out.sunVis > 0.f) {   // the sun's glare around the 3D disc (screen space, fades as the planet covers it)
        const Texture2D& glow = fx.glowTexture(); const Rectangle gs{0, 0, (float)glow.width, (float)glow.height};
        const Vector2 P = out.sunDiscPx; const float R = fmaxf(2.f, out.sunRadPx), v = out.sunVis;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(glow, gs, {P.x, P.y, R * 9.f, R * 9.f}, {R * 4.5f, R * 4.5f}, 0, Fade({255, 120, 40, 255}, 0.22f * v));   // soft outer bloom (the corona itself is 3D)
        EndBlendMode();
    }
    // The atmosphere is a 3D shell drawn by drawApproachScenery (same camera and centre as the
    // planet). Only the sunrise flare stays 2D: it is placed at the scenery camera's projection of
    // the sunrise limb point, in screen pixels (outside the 2D camera, so no shake offset).
    const float flareK = appr.liftoff ? 1.f - ss(0.35f, 1.f, out.sunVis) : 1.f;   // Liftoff: the limb flare only while the sun is (nearly) eclipsed
    if (out.flareOk && flareK > 0.01f) {
        const Vector2 P = out.flarePx, P2 = out.flareTanPx;
        const float Rpx = fmaxf(1.f, out.radiusPx);
        const Texture2D& glow = fx.glowTexture(); const Rectangle gs{0, 0, (float)glow.width, (float)glow.height};
        const float g = fminf(Rpx * 0.6f, 200.f * s) + 16.f * s;
        const float ang = atan2f(P2.y - P.y, P2.x - P.x);
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(glow, gs, {P.x, P.y, g * 2.6f, g * 2.6f}, {g * 1.3f, g * 1.3f}, 0, Fade({150, 90, 255, 255}, 0.30f * flareK));
        DrawTexturePro(glow, gs, {P.x, P.y, g, g}, {g / 2, g / 2}, 0, Fade({255, 236, 212, 255}, 0.85f * flareK));
        const float Ls = fminf(Rpx * 1.6f, 1500.f * s), h = fminf(20.f * s, 4.f * s + Rpx * 0.05f);
        DrawTexturePro(glow, gs, {P.x, P.y, Ls, h}, {Ls / 2, h / 2}, ang * RAD2DEG, Fade({255, 226, 240, 255}, 0.55f * flareK));
        EndBlendMode();
    }
    BeginMode2D(cam);
}

void Game::drawApproachFront() const {
    if (appr.liftoff && appr.fog.on) r3d.drawVolFog(true, W, H);   // the deck between the camera and the ship
    const Texture2D& glow = fx.glowTexture();
    const Rectangle gs{0, 0, (float)glow.width, (float)glow.height};
    const float t = levelTime;
    auto G = [&](Vector2 p, float w, float h, float rotDeg, Color c, float a) {
        if (a > 0.003f) DrawTexturePro(glow, gs, {p.x, p.y, w, h}, {w / 2, h / 2}, rotDeg, Fade(c, a));
    };
    if (appr.heat > 0.01f) {  // entry burn: plasma envelope around the ship, shock trails, heat haze
        const float hk = appr.heat, sw = fmaxf(20.f * s, appr.shipPx);
        const Vector2 S = appr.shipScr, B = appr.bowScr;
        const float fl = 0.80f + 0.20f * sinf(t * 37.f) * sinf(t * 23.f) + 0.08f * frand(-1, 1);
        BeginBlendMode(BLEND_ADDITIVE);
        // heat haze: shimmering warm layers wrapped round the hull
        for (int i = 0; i < 7; i++) {
            const float ph = t * (5.f + i) + i * 1.7f;
            const Vector2 o{sinf(ph) * 0.06f * sw, cosf(ph * 1.3f) * 0.05f * sw};
            const float rr = sw * (1.1f + 0.18f * i);
            G(Vector2Add(Vector2Lerp(S, B, 0.4f), o), rr * 1.5f, rr, sinf(ph) * 6.f, {255, 120, 60, 255}, 0.055f * hk);
        }
        // bow shock: red outer envelope, orange body, white-hot cap at the nose
        G(B, 2.8f * sw * fl, 2.2f * sw * fl, 0, {255, 60, 30, 255}, 0.38f * hk);
        G(B, 1.7f * sw * fl, 1.3f * sw * fl, 0, {255, 130, 40, 255}, 0.55f * hk);
        G(B, 0.8f * sw, 0.6f * sw, 0, {255, 210, 140, 255}, 0.55f * hk * fl);
        G(B, 0.35f * sw, 0.28f * sw, 0, {255, 250, 230, 255}, 0.6f * hk * fl);
        // flickering plasma trails: ionised gas peeling off the shock, streaming back past the camera
        for (int i = 0; i < 46; i++) {
            const float base = h01(i, 21) * 2.f * PI;
            const float u = fmodf(h01(i, 22) + t * (1.6f + 1.4f * h01(i, 23)), 1.f);
            const Vector2 dir{cosf(base), sinf(base) * 0.75f + 0.35f};   // biased down/outward: toward the viewer
            const Vector2 dn = Vector2Normalize(dir);
            const float r0 = sw * (0.35f + 1.6f * u), len = sw * (0.35f + 0.7f * h01(i, 24)) * (0.6f + 0.4f * fl);
            const Vector2 a = Vector2Add(B, Vector2Scale(dn, r0)), b = Vector2Add(a, Vector2Scale(dn, len));
            const Color c = lerpCol({255, 220, 150, 255}, {255, 50, 20, 255}, u);
            const float al = hk * (1.f - u) * (0.25f + 0.35f * h01(i, 25)) * (0.7f + 0.3f * sinf(t * 31.f + i));
            const Vector2 m = Vector2Lerp(a, b, 0.5f);
            const float th = (5.f + 10.f * (1.f - u)) * s * (0.5f + 0.5f * sw / (120.f * s));
            G(m, len * 1.4f, th, atan2f(dn.y, dn.x) * RAD2DEG, c, al * 1.4f);
        }
        // wingtip vortices of fire
        for (int w = 0; w < 2; w++) {
            const Vector2 P = w ? appr.wingR : appr.wingL;
            const Vector2 out = Vector2Normalize(Vector2Add(Vector2Subtract(P, S), {0, 0.25f * sw}));
            for (int k = 0; k < 6; k++) {
                const float u = fmodf(t * 2.3f + k / 6.f + w * 0.37f, 1.f);
                G(Vector2Add(P, Vector2Scale(out, u * 1.4f * sw)), (0.5f + 0.9f * u) * sw * 0.6f, (0.3f + 0.6f * u) * sw * 0.4f,
                  atan2f(out.y, out.x) * RAD2DEG, lerpCol({255, 170, 70, 255}, {255, 40, 20, 255}, u), 0.35f * hk * (1.f - u));
            }
        }
        // hot streaks rushing past from the flight-path point
        for (int i = 0; i < 40; i++) {
            const float ang = h01(i, 4) * 2.f * PI, u = fmodf(h01(i, 7) + t * (0.9f + h01(i, 3)), 1.f);
            const Vector2 dn{cosf(ang), sinf(ang)};
            const float r1 = 40.f * s + u * u * 0.9f * std::max(W, H);
            const float ln = (40.f + 220.f * u) * s;
            const Vector2 m = Vector2Add(appr.flow, Vector2Scale(dn, r1 + ln * 0.5f));
            G(m, ln * 1.3f, (3.f + 6.f * u) * s, ang * RAD2DEG, lerpCol({255, 200, 120, 255}, {255, 80, 30, 255}, u), 0.40f * hk * u);
        }
        const float e = 0.22f * H;
        const Color hot = Fade({255, 90, 40, 255}, 0.5f * hk * fl), none = {0, 0, 0, 0};
        DrawRectangleGradientV(0, 0, (int)W, (int)e, hot, none);
        DrawRectangleGradientV(0, (int)(H - e), (int)W, (int)e, none, hot);
        DrawRectangleGradientH(0, 0, (int)e, (int)H, hot, none);
        DrawRectangleGradientH((int)(W - e), 0, (int)e, (int)H, none, hot);
        EndBlendMode();
    }
    if (appr.clouds > 0.01f) {  // cloud layers rushing past: out of the flight-path point in the chase
        // view, streaming down the screen once the camera looks down (blended by the rise).
        const float span = 0.5f * std::max(W, H), rho = appr.rho;
        // sun's screen direction for the cloud lighting
        const Vector3 f0 = Vector3Normalize(appr.f), r0 = Vector3Normalize(Vector3CrossProduct(f0, appr.u)), u0 = Vector3CrossProduct(r0, f0);
        const Vector2 sunS = Vector2Normalize({Vector3DotProduct(appr.sun, r0) + 1e-4f, -Vector3DotProduct(appr.sun, u0)});
        if (appr.liftoff && rho > 0.01f) {   // Liftoff overhead: climbing up through the deck, puffs fall away below us
            const Vector2 C{W * 0.5f, H * 0.58f};
            for (int i = 0; i < 44; i++) {
                const float z = 0.06f + 0.94f * fmodf(h01(i, 1) + t * (0.45f + 0.30f * h01(i, 2)), 1.f);
                const float ang = h01(i, 4) * 2.f * PI, rad = (0.15f + 0.9f * h01(i, 6)) * span;
                const Vector2 p = Vector2Add(C, Vector2Scale({cosf(ang), sinf(ang)}, rad * 0.35f / z));
                const float sz = (120.f + 140.f * h01(i, 8)) * s / z;
                const float a = appr.clouds * rho * ss(1.f, 0.6f, z) * ss(0.06f, 0.16f, z) * (sz > H ? 0.12f : 0.26f);
                if (a < 0.01f) continue;
                const float lit = 0.5f + 0.5f * (cosf(ang) * sunS.x + sinf(ang) * sunS.y);
                const Color c = lerpCol({120, 108, 178, 255}, {255, 240, 228, 255}, lit * (0.45f + 0.55f * h01(i, 10)));
                DrawTexturePro(glow, gs, {p.x, p.y, sz, sz * 0.7f}, {sz / 2, sz * 0.35f}, ang * RAD2DEG * 0.2f, Fade(c, a));
            }
        }
        for (int i = 0; i < 44; i++) {
            if (appr.liftoff && rho >= 0.99f) break;   // (Liftoff: the chase-view set only once the camera tips)
            const float z = 0.06f + 0.94f * (1.f - fmodf(h01(i, 1) + t * (0.55f + 0.35f * h01(i, 2)), 1.f));
            const float ang = h01(i, 4) * 2.f * PI, rad = (0.12f + 0.9f * h01(i, 6)) * span;
            const Vector2 pc = Vector2Add(appr.flow, Vector2Scale({cosf(ang), sinf(ang)}, rad * 0.35f / z));
            const Vector2 po{W * (h01(i, 12) * 1.2f - 0.1f), -0.3f * H + 1.5f * H * (1.f - (z - 0.06f) / 0.94f)};
            const Vector2 p = Vector2Lerp(pc, po, rho);
            const float sz = (120.f + 140.f * h01(i, 8)) * s / z;
            const float a = appr.clouds * ss(1.f, 0.6f, z) * (sz > H ? 0.12f : 0.26f) * (appr.liftoff ? 1.f - rho : 1.f);
            if (a < 0.01f) continue;
            const float lit = 0.5f + 0.5f * (cosf(ang) * sunS.x + sinf(ang) * sunS.y);
            const Color c = lerpCol({92, 82, 160, 255}, {255, 236, 222, 255}, lit * (0.35f + 0.65f * h01(i, 10)));
            DrawTexturePro(glow, gs, {p.x, p.y, sz, sz * 0.7f}, {sz / 2, sz * 0.35f}, Lerp(ang * RAD2DEG, 0.f, rho), Fade(c, a));
        }
        const float layer = appr.clouds * powf(0.5f + 0.5f * sinf(t * 1.9f), 8.f) * 0.38f;  // punching through a deck
        if (layer > 0.01f) DrawRectangle(0, 0, (int)W, (int)H, Fade({236, 228, 255, 255}, layer));
    }
}

void Game::drawApproachPanels() const {
    // Dark frame around the turned square playfield (a pinwheel of four quads: no overlap).
    if (appr.panel <= 0.005f) return;
    const Vector2 c = view.centre();
    const float hw = view.pf.width * 0.5f, hh = view.pf.height * 0.5f, big = 4.f * std::max(W, H);
    const Color pc = Fade({12, 12, 26, 255}, appr.panel);
    for (int k = 0; k < 4; k++)
        DrawRectanglePro({c.x, c.y, big, big}, {-hw, hh}, view.rot * RAD2DEG + 90.f * k, pc);
    const Vector2 q[4] = {view.camOut({view.pf.x, view.pf.y}), view.camOut({view.pf.x + view.pf.width, view.pf.y}),
                          view.camOut({view.pf.x + view.pf.width, view.pf.y + view.pf.height}), view.camOut({view.pf.x, view.pf.y + view.pf.height})};
    const float la = appr.panel > 0.6f ? 0.4f : 0.4f * appr.panel / 0.6f;
    for (int k = 0; k < 4; k++) DrawLineEx(q[k], q[(k + 1) % 4], 2 * s, Fade(SKYBLUE, la));
}

// ============================================================ t219: selectable primary weapons
void Game::selectWeapon(int w, bool announce) {
    if (w < 0 || w >= kPrimaryCount || !((ownedWeapons >> w) & 1u)) return;
    if (weapons.primary != w) {
        weapons.primary = w;
        weapons.beamOn = false; weapons.flameOn = false; weapons.beamAcc = 0.f;
        if (announce) { weaponNote = primaryName(w); weaponNoteT = 1.4f; audio.play(Sfx::Pickup, 0.45f, 0.02f); }
        writeSave();
    }
}

void Game::cycleWeapon(int dir) {
    if (ownedCount() < 2) return;   // inert until a second primary is owned
    int w = weapons.primary;
    for (int i = 0; i < kPrimaryCount; i++) {
        w = (w + (dir >= 0 ? 1 : kPrimaryCount - 1)) % kPrimaryCount;
        if ((ownedWeapons >> w) & 1u) { selectWeapon(w, true); return; }
    }
}

void Game::grantWeapon(int w) {
    const bool had = (ownedWeapons >> w) & 1u;
    ownedWeapons |= 1u << w;
    writeSave();
    fprintf(stderr, "WEAPON granted %s owned=%u\n", primaryName(w), ownedWeapons);                       // kept for the rest of the game: every level, every replay
    selectWeapon(w, false);
    weaponNote = had ? primaryName(w) : (w == kPrimaryFlame ? "FLAMETHROWER ONLINE" : "PLASMA LANCE ONLINE");
    weaponNoteT = 2.6f;
    audio.play(Sfx::WeaponPickup, 0.9f, 0.015f);
}

void Game::updateWeaponGifts(float dt) {
    (void)dt;
    const LevelDef& L = db.levels()[levelIdx];
    const int want = L.id == "level_04" ? kPrimaryFlame : L.id == "level_07" ? kPrimaryPlasma : -1;
    if (want < 0 || ((ownedWeapons >> want) & 1u) || weaponGiftT <= 0.f || levelTime < weaponGiftT) return;
    if (L.liftoff && levelTime > kLiftGunsTo - 6.f) { weaponGiftT = 0.f; return; }   // given at the clear instead
    bool live = false;
    pickups.each([&](const Pickup& p) { if (p.type == PickupType::Flamer || p.type == PickupType::Plasma) live = true; });
    if (!live) if (Pickup* p = pickups.spawn()) {
        p->pos = {view.forward() * 0.96f, view.lateral() * Clamp(player.pos.y / std::max(1.f, view.lateral()), 0.3f, 0.7f)};
        p->type = want == kPrimaryFlame ? PickupType::Flamer : PickupType::Plasma;
        fprintf(stderr, "WEAPON gift %s spawned t=%.1f\n", primaryName(want), levelTime);
    }
    weaponGiftT = levelTime + 15.f;    // missed: it comes round again
}

Vector2 Game::switchButtonPos() const {
    return {W - kSecondaryRight * s, H - (kSecondaryBottom + kActionButtonRadius * 1.86f) * s};
}

void Game::drawWeaponIcon(Vector2 c, float r, int w, float a) const {
    if (w == kPrimaryFlame) {
        DrawCircleV({c.x - r * 0.10f, c.y + r * 0.10f}, r * 0.34f, Fade({255, 90, 20, 255}, a));
        DrawTriangle({c.x - r * 0.42f, c.y + r * 0.02f}, {c.x + r * 0.05f, c.y - r * 0.62f}, {c.x + r * 0.22f, c.y + r * 0.10f}, Fade({255, 120, 30, 255}, a));
        DrawCircleV({c.x - r * 0.06f, c.y + r * 0.16f}, r * 0.20f, Fade({255, 210, 90, 255}, a));
        DrawCircleV({c.x - r * 0.04f, c.y + r * 0.20f}, r * 0.09f, Fade(WHITE, a));
    } else if (w == kPrimaryPlasma) {
        for (int i = 0; i < 10; i++) {
            const float x0 = c.x - r * 0.5f + i * r * 0.1f, x1 = x0 + r * 0.1f;
            const float y0 = c.y + sinf(i * 1.3f + time * 9.f) * r * 0.10f, y1 = c.y + sinf((i + 1) * 1.3f + time * 9.f) * r * 0.10f;
            DrawLineEx({x0, y0}, {x1, y1}, r * 0.22f, Fade({180, 70, 255, 255}, a * 0.6f));
        }
        DrawLineEx({c.x - r * 0.5f, c.y}, {c.x + r * 0.5f, c.y}, r * 0.08f, Fade(WHITE, a));
        DrawCircleV({c.x + r * 0.52f, c.y}, r * 0.14f, Fade({240, 190, 255, 255}, a));
    } else {
        DrawLineEx({c.x - r * 0.46f, c.y}, {c.x + r * 0.40f, c.y}, r * 0.10f, Fade(SKYBLUE, a));
        DrawCircleV({c.x + r * 0.42f, c.y}, r * 0.11f, Fade(WHITE, a));
    }
}

// ============================================================ t219: Liftoff escape burn (no shield)
void Game::updateEscapeShield() {
    const LevelDef& L = db.levels()[levelIdx];
    const float lt = levelTime;
    const bool off = L.liftoff && (lt < kLiftCombatFrom || (lt >= kLiftClimb && lt < kLiftOrbit0));
    if (off) {
        if (!shieldOffline) { shieldOffline = true; if (lt >= kLiftClimb) shieldNoteT = 3.5f; }
        shieldStash = std::min(kShieldMax, shieldStash + player.shield); player.shield = 0;   // banked, not lost
    } else if (shieldOffline) {
        shieldOffline = false;
        if (shieldStash > 0) { player.shield = std::min(kShieldMax, player.shield + shieldStash); shieldStash = 0; shieldNoteT = -2.f; }
    }
}

// ============================================================ t219: The Warden's sun approach
void Game::updateWardenSky(float dt) {
    if (!wsky.on || !r3d.ok()) return;
    g_wardenSkyTime = levelTime;
    if (bossSpawned && wsky.bossAt < 0.f) wsky.bossAt = levelTime;
    float tgt = ss(3.f, 58.f, levelTime);
    if (wsky.bossAt >= 0.f) tgt = std::max(tgt, ss(wsky.bossAt, wsky.bossAt + 12.f, levelTime));   // the fight is at 2/3 screen
    static const float forceK = getenv("WARDEN_K") ? (float)atof(getenv("WARDEN_K")) : -1.f;      // capture hook
    if (forceK >= 0.f) tgt = forceK;
    wsky.k = forceK >= 0.f ? forceK : wsky.k + (tgt - wsky.k) * fminf(1.f, dt * 1.2f);
    const float ke = wsky.k * wsky.k * (3.f - 2.f * wsky.k);
    const Camera3D c = CameraSystem::makeCamera(view, 0.f);
    wsky.fovy = c.fovy;
    const Vector3 f0 = Vector3Normalize(Vector3Subtract(c.target, c.position));
    const Vector3 r0 = Vector3Normalize(Vector3CrossProduct(f0, c.up)), u0 = Vector3CrossProduct(r0, f0);
    const float fp = H * 0.5f / tanf(c.fovy * 0.5f * DEG2RAD);
    // sun on screen: from the Liftoff exit (upper right, small) along an arc to centre-right, 2/3 screen
    const float sx = Lerp(0.74f, 0.71f, ke) + 0.10f * sinf(PI * ke), sy = Lerp(0.27f, 0.47f, ke);
    wsky.sunCam = Vector3Normalize(Vector3Add(f0, Vector3Add(Vector3Scale(r0, (sx - 0.5f) * W / fp), Vector3Scale(u0, (0.5f - sy) * H / fp))));
    static const float endR = getenv("WARDEN_SUNR") ? (float)atof(getenv("WARDEN_SUNR")) : 22.5f;
    wsky.sunR = Lerp(6.f, endR, powf(ke, 1.35f)) * DEG2RAD;
    // orbiting the star: the whole sky (nebula + stars) swings past while the sun holds its place
    const Matrix R = MatrixRotateY(-50.f * DEG2RAD * ke - 0.004f * levelTime);
    wsky.f = Vector3Transform(f0, R); wsky.u = Vector3Transform(u0, R); wsky.sun = Vector3Transform(wsky.sunCam, R);
    wsky.expo = 0.32f * ss(0.25f, 1.f, ke);   // exposure down as the disc grows: shots and ships stay readable
    r3d.setSceneLight(true, Vector3Negate(wsky.sunCam), 0.34f, 0.16f);   // the sun is the one ship light
}

void Game::drawWardenSky(const Camera2D& cam) const {
    EndMode2D();
    Render3D::ApproachScene sc;
    sc.f = wsky.f; sc.u = wsky.u; sc.sun = wsky.sun; sc.pdir = Vector3Negate(wsky.sun); sc.fovy = wsky.fovy; sc.dist = 30.f;
    sc.stars = 1.f; sc.nebula = 0.5f; sc.sunR = wsky.sunR; sc.time = levelTime; sc.planet = false;
    const Camera3D camera = CameraSystem::makeCamera(view, 0.f);
    sc.starF = Vector3Normalize(Vector3Subtract(camera.target, camera.position)); sc.starU = camera.up; sc.separateStarFrame = true;
    for (const BgLayer& layer : db.levels()[levelIdx].layers) if (layer.type == LayerType::Nebula) { sc.nebulaTint = layer.color; break; }
    Render3D::ApproachOut out;
    r3d.drawApproachScenery(sc, out);
    if (out.sunOn) {   // soft outer bloom (the corona and flares themselves are 3D)
        const Texture2D& glow = fx.glowTexture(); const Rectangle gs{0, 0, (float)glow.width, (float)glow.height};
        const Vector2 P = out.sunDiscPx; const float R = fmaxf(2.f, out.sunRadPx);
        const float g = fminf(R * 9.f, R * 2.6f + 900.f * s);
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(glow, gs, {P.x, P.y, g, g}, {g / 2, g / 2}, 0, Fade({255, 120, 40, 255}, 0.20f));
        EndBlendMode();
    }
    BeginMode2D(cam);
}
