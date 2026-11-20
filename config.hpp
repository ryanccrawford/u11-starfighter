#pragma once
#include <cstdlib>
// ---------------------------------------------------------------------------
// All gameplay + look-and-feel tunables live here.
// Distances/speeds are in "design pixels" and multiplied by the UI scale s
// (s = min(screenW, screenH) / 600), so the game is resolution independent.
// ---------------------------------------------------------------------------
namespace cfg {
// --- Ships / animation -----------------------------------------------------
constexpr float kPlayerSize = 110;     // on-screen sprite frame size
constexpr float kEnemySize = 96;
constexpr float kSideEnemyScaleMul = 0.65f * 0.85f; // requested 15% reduction from the prior side-scroll size
constexpr float kTopDownEnemyScaleMul = 0.50f;       // requested 50% reduction for overhead enemies
constexpr float kAnimFps = 20;         // sprite + flame frame rate
constexpr float kPlayerSpeed = 650;
constexpr float kPlayerMaxX = 0.6f;    // fraction of screen width the player may use
// Edge throttle (Ryan, Nov 11): holding the ship INTO the forward stop edge = afterburner, holding it into the
// back edge = throttling down. The world scroll (background, planets, enemy travel, pickups) and the
// player's engine flame follow. The player itself does not move further; enemy flames are unaffected.
constexpr float kEdgeBoostMul = 1.15f;  // world scroll at the forward edge (+15%)
constexpr float kEdgeSlowMul = 0.85f;   // world scroll at the back edge (-15%)
constexpr float kEdgeFlameGain = 2.0f;  // player flame length/brightness change per unit of scroll change (+/-30%)
constexpr float kEdgeBlendRate = 3.0f;  // 1/s: ease in/out so it never snaps
constexpr float kTouchOffsetX = 70;    // ship sits ahead of the finger

// --- Right-thumb action controls --------------------------------------------
constexpr float kActionButtonRadius = 48;       // design pixels; multiplied by UI scale
constexpr float kSecondaryRight = 58, kSecondaryBottom = 138;
constexpr float kFireRight = 170, kFireBottom = 64;
constexpr float kTouchJoystickDeadzone = 8;     // design pixels; taps inside this radius do not move
constexpr float kTouchJoystickRadius = 90;      // design pixels; displacement that reaches full flight speed

// --- Engine flames (75% scale, anchored at nozzles) ------------------------
constexpr float kFlameScale = 0.75f;
constexpr float kFlameAnchorX = 0.62f, kFlameAnchorY = 0.69f;        // side flame base (points left)
constexpr float kFlameAnchorTopX = 0.50f, kFlameAnchorTopY = 0.15f;  // top-down flame base (points down)
constexpr float kTopDownAspect = 1.3f;  // top-down playfield width = screen height * this (centred column)

// --- Flame glow overlay ------------------------------------------------------
constexpr float kGlowSize = 0.55f;
constexpr float kGlowAlpha = 0.30f;
constexpr float kGlowPulseAmount = 0.15f;
constexpr float kGlowPulseSpeed = 9.0f;
constexpr unsigned char kGlowR = 150, kGlowG = 110, kGlowB = 255;  // soft purple halo (reference video)

// --- Flame shape (procedural strip, see Particles::buildFlame) -------------------
constexpr int kFlameFrames = 8;
constexpr float kFlameLength = 0.42f;   // visible flame length as a fraction of the 75%-scaled frame (~2.3 nozzle diameters)
constexpr float kFlameAspect = 2.3f;    // length / base width
constexpr float kFlameSwapHz = 30;      // erratic frame swaps per second (no smooth pulse)

// --- Flame glitter -----------------------------------------------------------
constexpr float kGlitterRate = 60;      // sparkles / s / nozzle
constexpr float kGlitterLife = 0.22f;   // sparks fade fast
constexpr float kGlitterSpeed = 260;
constexpr float kGlitterSize = 2.2f;
constexpr float kGlitterBrightness = 1.0f;

// --- Player damage -----------------------------------------------------------
constexpr int kPlayerHp = 100;
constexpr int kShieldMax = 60;
constexpr float kInvulnTime = 0.7f;
constexpr int kBulletDamage = 12, kRamDamage = 30;
// The rendered field is a physical boundary, not merely a visual.  The side view needs a
// wider projected radius because the fighter is full-scale there; the overhead fighter is half-scale.
constexpr float kShieldRadiusSide = 0.80f, kShieldRadiusTop = 0.50f;
constexpr int kShieldContactDamage = 18;
constexpr float kShieldContactCooldown = 0.35f;

// --- Laser -------------------------------------------------------------------
constexpr float kLaserInterval = 0.10f;       // seconds between volleys
constexpr float kRapidMultiplier = 0.5f;      // interval multiplier while rapid-fire is active
constexpr float kLaserSpeed = 1300;
constexpr float kLaserLength = 34;
// --- Laser bolt look (port of Ryan's makeLaserBolt settings; unit = bolt length / 1.7) --------
constexpr float kBoltQuadScale = 1.6f;   // bolt quad length = kLaserLength * this (visible body spans x .18-.96)
constexpr float kBoltAspect = 1.91f / 1.7f;  // width / length
constexpr float kSparkRate = 25;         // sparks / s / bolt
constexpr float kSparkLifeMin = 0.12f, kSparkLifeMax = 0.27f;
constexpr float kSparkSizeMin = 1.25f, kSparkSizeMax = 4.75f;  // px (x UI scale)
constexpr float kSparkDriftMin = 3.2f, kSparkDriftMax = 3.7f;  // units / s backward
constexpr float kSparkSpread = 1.15f, kSparkSpawnSpread = 0.12f, kSparkGain = 0.86f, kSparkShrink = 0.5f;
constexpr int kLaserDamage = 1;
constexpr int kMaxWeaponLevel = 3;            // 1 single, 2 double, 3 spread

// --- Missiles ----------------------------------------------------------------
constexpr int kMissileStartAmmo = 6, kMissileMaxAmmo = 20;
constexpr float kMissileInterval = 0.6f;      // auto-launch cooldown when targets exist
constexpr float kMissileSpeed = 520, kMissileTurn = 5.0f;  // turn rate eases in over time
constexpr int kMissileDamage = 6;

// Runtime development controls.  These deliberately start at the shipped values and are
// changed only from the in-game Development menu; they are not part of campaign save data.
struct DevSettings {
    float weaponCooldown = kLaserInterval;
    int laserDamage = kLaserDamage;
    int missileDamage = kMissileDamage;
    float playerSpeed = kPlayerSpeed;
    int startingLives = 3;
    float musicLevel = 1.f;
};
inline DevSettings dev{};

// --- Power-ups ---------------------------------------------------------------
constexpr float kDropChance = 0.18f;          // per normal kill
constexpr float kRapidTime = 8.0f;
constexpr int kRepairAmount = 35;
constexpr int kShieldPickup = 40;

// --- Enemies -----------------------------------------------------------------
constexpr int kBossEvery = 4;                 // mini-boss every N waves
constexpr float kEnemyBulletSpeed = 280;

// --- Juice -------------------------------------------------------------------
constexpr float kShakeMax = 0.6f;
constexpr float kShakePixels = 28;
}  // namespace cfg

// --- Graphics quality (runtime; set by Options > Graphics / Auto) -------------
// Fill rate is the limit on the U11 (~330 Mpix/s blended; one full-screen blended quad ~2.4 ms),
// so "low" trims large additive quads and the renderer can draw the game at a reduced internal scale.
namespace gfx {
inline bool low = false;               // fewer/smaller glows, no nebula/planet halo, smaller explosion flashes
constexpr float kAutoTargetFps = 84;   // Auto steps quality down while gameplay averages below this
constexpr float kScales[] = {1.0f, 0.85f, 0.75f, 0.66f};  // internal render scales Auto may use
}
