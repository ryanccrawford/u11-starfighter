#include "moon_level.hpp"
#include "camera_system.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

// t247 (Moon Breakup look pass, matched to assets/moon/moon_explosion_reference.png):
// grey cratered moon lit only by the sun, an asteroid impact on the sun-facing side, baked
// lightning-like crack network that grows out of the molten crater, charred plates, a sun-lit
// dust plume + rock spray (round particles, no square sprites), then the breakup into plates.
// Gameplay (debris waves, collisions, weapons) is Gemini's, kept as it was.

// -----------------------------------------------------------------------------
// Shaders (GLSL 330)
// -----------------------------------------------------------------------------

static const char* MOON_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float wobble;
uniform float time;
uniform float forceOuter; // 1.0 = whole moon (all crust), 0.0 = shard (vertexColor.a marks cut faces)

out vec3 worldPos;
out vec3 worldNormal;
out vec3 objPos;
out float isCut;

void main() {
    objPos = vertexPosition;
    isCut = (forceOuter > 0.5) ? 0.0 : vertexColor.a;
    vec3 pos = vertexPosition;
    if (wobble > 0.0) {   // seismic tremor just before the breakup
        float tremor = sin(time * 38.0 + vertexPosition.y * 12.0) * cos(time * 32.0 + vertexPosition.x * 12.0);
        pos += vertexNormal * (wobble * (0.010 + 0.008 * tremor));
    }
    worldPos = (matModel * vec4(pos, 1.0)).xyz;
    worldNormal = normalize((matModel * vec4(vertexNormal, 0.0)).xyz);
    gl_Position = mvp * vec4(pos, 1.0);
}
)";

static const char* MOON_FS = R"(#version 330
in vec3 worldPos;
in vec3 worldNormal;
in vec3 objPos;
in float isCut;

uniform sampler2D texture0;   // Ryan's regolith (detail luminance only)
uniform sampler2D craterTex;  // rgb = object-space normal with craters, a = albedo
uniform sampler2D crackTex;   // r = distance to crack / half width / 4, g = arrival, b = char, a = plate noise
uniform vec4 colDiffuse;
uniform mat4 matModel;
uniform vec3 lightDir;        // direction the sunlight travels (world)
uniform vec3 camPos;
uniform vec3 impactDir;       // object space, unit
uniform float impactAge;      // seconds since impact (< 0 before)
uniform float crackProgress;  // 0..1 crack front (arrival units)
uniform float widen;          // 0..1.6 crack width growth
uniform float heat;           // 0..1 overall heat (cools after the breakup)
uniform float time;
uniform float dbg;

out vec4 finalColor;

const float PI = 3.14159265;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}

vec3 heatColor(float h) {   // 0 dark red .. 1 yellow-white (HDR-ish, clipped by the display)
    vec3 red = vec3(0.55, 0.06, 0.01);
    vec3 orange = vec3(1.45, 0.48, 0.05);
    vec3 yellow = vec3(1.9, 1.45, 0.55);
    vec3 white = vec3(2.2, 2.0, 1.5);
    if (h < 0.4) return mix(red, orange, h / 0.4);
    if (h < 0.8) return mix(orange, yellow, (h - 0.4) / 0.4);
    return mix(yellow, white, (h - 0.8) / 0.2);
}

void main() {
    vec3 L = normalize(-lightDir);
    vec3 V = normalize(camPos - worldPos);
    vec3 n = normalize(objPos);

    if (isCut > 0.5) {
        // Fresh fracture face: dark basalt lit by the sun, glowing veins that cool down
        vec3 N = normalize(worldNormal);
        float mu0 = max(0.0, dot(N, L));
        float grain = 0.75 + 0.25 * noise(objPos * 40.0);
        vec3 rock = vec3(0.24, 0.225, 0.21) * grain * (0.03 + 1.25 * mu0);
        float depth = 1.0 - length(objPos);                 // 0 at the crust .. ~0.8 deep inside
        float nv = noise(objPos * 11.0) + 0.5 * noise(objPos * 27.0 + 3.1);
        float vein = 1.0 - smoothstep(0.008, 0.035, abs(nv - 0.75));
        float hcut = heat * mix(0.30, 1.0, smoothstep(0.02, 0.45, depth));
        rock += heatColor(0.2 + 0.7 * hcut) * vein * hcut * 1.3;
        rock += vec3(1.0, 0.30, 0.04) * pow(hcut, 2.0) * 0.55;   // the hot interior glows
        finalColor = vec4(rock, colDiffuse.a);
        return;
    }

    // Equirect lookup from the object-space direction (same mapping as the CPU bake)
    vec2 uv = vec2(atan(n.z, n.x) / (2.0 * PI) + 0.5, asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5);
    vec4 ct = texture(craterTex, uv);
    vec4 ck = texture(crackTex, uv);
    float lum = dot(texture(texture0, uv).rgb, vec3(0.30, 0.59, 0.11));

    // Albedo: light grey regolith, craters baked; Ryan's texture adds fine mottling (no blue tint)
    float alb = clamp(ct.a + (lum - 0.25) * 0.55, 0.06, 0.95);
    vec3 albedo = vec3(alb) * vec3(1.0, 0.975, 0.94);

    vec3 nObj = normalize(ct.rgb * 2.0 - 1.0);
    vec3 N = normalize(mat3(matModel) * nObj);
    vec3 Ng = normalize(mat3(matModel) * n);   // Ryan's GLB stores inward normals: use the sphere direction

    // Impact zone
    float cA = dot(n, impactDir);
    float a = acos(clamp(cA, -1.0, 1.0));     // angular distance from the impact (rad)
    vec3 emissive = vec3(0.0);
    if (impactAge >= 0.0) {
        float grow = clamp(impactAge / 0.6, 0.0, 1.0);
        float craterR = 0.125 * (0.35 + 0.65 * sqrt(grow));
        // charred plates around the crater (baked mask), darken as the heat soaks out
        float charM = ck.b * clamp(impactAge / 3.0, 0.0, 1.0);
        albedo *= mix(1.0, 0.16 + 0.10 * ck.a, charM);
        // raised, sunlit crater rim
        float rim = exp(-pow((a - craterR * 1.12) / (0.022), 2.0));
        albedo *= 1.0 + 0.35 * rim * (1.0 - charM);
        // molten crater floor
        float m = 1.0 - smoothstep(craterR * 0.75, craterR, a);
        float lava = 0.65 + 0.35 * noise(n * 38.0 + vec3(0.0, time * 0.25, 0.0));
        float hc = clamp(1.0 - a / craterR, 0.0, 1.0);
        float cool = mix(1.0, 0.72, clamp(impactAge / 25.0, 0.0, 1.0)) * heat;
        emissive += heatColor(clamp(0.45 + 0.6 * hc * lava, 0.0, 1.0) * cool) * m * (1.1 + 0.9 * hc) * lava;
        // glow soaked into the charred ring
        emissive += vec3(1.0, 0.22, 0.03) * charM * (1.0 - smoothstep(craterR, craterR * 2.0, a)) * 0.12 * heat;
        // seismic ripple running across the surface, lit dust lifted by it
        float rr = impactAge * 0.75;
        float band = exp(-pow((a - rr) / 0.05, 2.0)) * clamp(1.0 - impactAge / 3.2, 0.0, 1.0);
        albedo *= 1.0 + 0.55 * band;
        N = normalize(N + (n - impactDir * cA) * band * 0.6);
    }

    // Lightning cracks: baked network, revealed along its arrival time, widening over time
    if (crackProgress > 0.0) {
        float d = ck.r * 4.0;            // distance in half-widths of that crack
        float arr = ck.g;
        float tip = clamp((crackProgress - arr) / 0.05, 0.0, 1.0);
        if (tip > 0.0) {
            float w = max(0.05, widen * tip);
            float core = 1.0 - smoothstep(w * 0.5, w, d);
            float halo = exp(-d / (0.45 + 0.9 * w)) * (1.0 - smoothstep(2.6, 4.0, d)) * tip;
            float h = clamp(1.05 - arr * 1.35, 0.0, 1.0) * heat;
            float fresh = clamp((crackProgress - arr) / 0.25, 0.0, 1.0);
            float hh = clamp(h * (0.70 + 0.20 * fresh) + 0.06, 0.0, 0.86) + 0.14 * core * h * h;
            emissive += heatColor(hh) * core * (0.75 + 0.45 * h);
            emissive += heatColor(h * 0.45) * halo * 0.35;
            albedo *= 1.0 - 0.7 * (1.0 - smoothstep(w, w * 2.6 + 0.6, d)) * tip;   // scorched crack lips
        }
    }

    // Sun-only lighting: Lommel-Seeliger + Lambert blend (real regolith), soft terminator
    float mu0 = dot(N, L);
    float mu = max(0.05, dot(N, V));
    float mu0g = dot(Ng, L);
    float term = smoothstep(-0.06, 0.18, mu0g);         // geometric terminator, softened
    float m0 = max(0.0, mu0);
    float ls = m0 / (m0 + mu);
    float lit = (0.45 * m0 + 1.1 * ls) * term;
    vec3 col = albedo * (1.30 * lit + 0.018);           // tiny starlight floor (never pure black)
    col += emissive;
    if (dbg > 0.5 && dbg < 1.5) col = Ng * 0.5 + 0.5;
    else if (dbg > 1.5 && dbg < 2.5) col = N * 0.5 + 0.5;
    else if (dbg > 2.5 && dbg < 3.5) col = L * 0.5 + 0.5;
    else if (dbg > 3.5) col = vec3(term, m0, ls);
    finalColor = vec4(col, colDiffuse.a);
}
)";

// Faceted rock shader: real face normals, triplanar regolith detail, sun-only light
static const char* ROCK_VS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 objPos;
out vec3 worldNormal;
out vec3 worldPos;
void main() {
    objPos = vertexPosition;
    worldPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    worldNormal = normalize((matModel * vec4(vertexNormal, 0.0)).xyz);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* ROCK_FS = R"(#version 330
in vec3 objPos;
in vec3 worldNormal;
in vec3 worldPos;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 camPos;
uniform float rockHeat;   // 0..1 faint glow on fresh pieces
uniform float albedoK;    // grey level (asteroid darker)
out vec4 finalColor;
void main() {
    vec3 n = normalize(worldNormal);
    vec3 L = normalize(-lightDir);
    vec3 V = normalize(camPos - worldPos);
    vec3 b = abs(normalize(objPos));
    b = max(b, 0.0001); b /= (b.x + b.y + b.z);
    float s = 0.40;
    vec3 t = texture(texture0, objPos.yz * s).rgb * b.x + texture(texture0, objPos.xz * s).rgb * b.y + texture(texture0, objPos.xy * s).rgb * b.z;
    float lum = dot(t, vec3(0.30, 0.59, 0.11));
    vec3 albedo = vec3(clamp(albedoK + (lum - 0.25) * 0.8, 0.04, 0.9)) * vec3(1.0, 0.95, 0.88);
    float mu0 = max(0.0, dot(n, L));
    float mu = max(0.05, dot(n, V));
    float lit = 0.45 * mu0 + 1.1 * mu0 / (mu0 + mu);
    vec3 col = albedo * (1.30 * lit + 0.02);
    if (rockHeat > 0.01) col += vec3(1.0, 0.26, 0.03) * rockHeat * (1.0 - mu0) * (0.5 + 0.5 * step(lum, 0.22)) * 0.22;
    finalColor = vec4(col, colDiffuse.a);
}
)";

// Soft additive core (crater bloom / interior glow): round, radial alpha, no square edges
static const char* FIREBALL_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 uv;
out vec4 col;
void main() {
    uv = vertexTexCoord;
    col = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* FIREBALL_FS = R"(#version 330
in vec2 uv;
in vec4 col;
uniform float time;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p);
    vec2 u = f*f*(3.0-2.0*f);
    return mix(mix(hash(i + vec2(0.0,0.0)), hash(i + vec2(1.0,0.0)), u.x),
               mix(hash(i + vec2(0.0,1.0)), hash(i + vec2(1.0,1.0)), u.x), u.y);
}
void main() {
    vec2 p = uv * 2.0 - 1.0;
    float r = length(p);
    if (r > 1.0) discard;
    float core = exp(-r * r * 9.0);
    float body = pow(1.0 - r, 2.2);
    float n1 = noise(p * 5.0 + vec2(time * 0.7, -time * 0.4));
    float billow = body * (0.7 + 0.3 * n1);
    vec3 c = col.rgb * billow + vec3(1.0, 0.92, 0.75) * core * 0.9;
    finalColor = vec4(c * col.a, 1.0);
}
)";

// Round billboard lit by the sun: soft dust (hard = 0) or small rock bits (hard = 1)
static const char* DUST_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 uv;
out vec4 col;
void main() {
    uv = vertexTexCoord;
    col = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* DUST_FS = R"(#version 330
in vec2 uv;
in vec4 col;
uniform vec3 lightCam;   // toward the sun, camera space (x right, y up, z toward the viewer)
uniform float hard;
out vec4 finalColor;
float h1(float n) { return fract(sin(n * 12.9898) * 43758.5453); }
void main() {
    vec2 p = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float seed = float(gl_PrimitiveID / 2);
    float r2 = dot(p, p);
    if (hard > 0.5) {   // irregular rock outline (5-7 lumpy lobes per bit)
        float ang = atan(p.y, p.x) / 6.2831853 + 0.5;
        float lobes = 5.0 + floor(h1(seed) * 3.0);
        float k = ang * lobes;
        float r0 = mix(0.62 + 0.38 * h1(seed + floor(k)), 0.62 + 0.38 * h1(seed + floor(k) + 1.0), smoothstep(0.0, 1.0, fract(k)));
        p /= r0;
        r2 = dot(p, p);
    }
    if (r2 > 1.0) discard;
    vec3 n = vec3(p, sqrt(1.0 - r2));
    float d = dot(n, lightCam);
    float lit, a;
    if (hard > 0.5) {
        lit = max(d, 0.0) * 1.25 + 0.03;
        a = col.a * (1.0 - smoothstep(0.80, 1.0, sqrt(r2)));
    } else {
        // thin dust: wrapped light + a little forward scatter when back-lit
        float fwd = pow(max(0.0, -lightCam.z), 2.0) * 0.35;
        lit = clamp(0.62 * d + 0.42, 0.0, 1.0) * 1.10 + fwd + 0.03;
        a = col.a * pow(1.0 - r2, 1.6);
    }
    finalColor = vec4(col.rgb * lit, a);
}
)";

// -----------------------------------------------------------------------------
// Helper math
// -----------------------------------------------------------------------------

static float frand01() {
    return (float)rand() / (float)RAND_MAX;
}

static float frange(float minVal, float maxVal) {
    return minVal + frand01() * (maxVal - minVal);
}

static float ss(float a, float b, float x) {
    x = Clamp((x - a) / (b - a), 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

// Deterministic RNG for the bakes (same moon every run)
struct BakeRng {
    uint32_t s;
    explicit BakeRng(uint32_t seed) : s(seed ? seed : 1u) {}
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; }
    float range(float a, float b) { return a + (b - a) * next(); }
    float gauss() { float u = 0; for (int i = 0; i < 4; i++) u += next(); return (u - 2.f) * 1.732f; }
};

static Vector3 randomUnit(BakeRng& r) {
    const float z = r.range(-1.f, 1.f), ph = r.range(0.f, 2.f * PI), q = sqrtf(std::max(0.f, 1.f - z * z));
    return {q * cosf(ph), z, q * sinf(ph)};
}

static Vector3 randomUnitF() {
    const float z = frange(-1.f, 1.f), ph = frange(0.f, 2.f * PI), q = sqrtf(std::max(0.f, 1.f - z * z));
    return {q * cosf(ph), z, q * sinf(ph)};
}

// random direction inside a cone about `axis` (half angle in rad)
static Vector3 coneDir(Vector3 axis, float halfAngle) {
    Vector3 up = fabsf(axis.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Vector3 t1 = Vector3Normalize(Vector3CrossProduct(up, axis));
    Vector3 t2 = Vector3CrossProduct(axis, t1);
    const float ca = cosf(halfAngle);
    const float z = frange(ca, 1.f), ph = frange(0.f, 2.f * PI), q = sqrtf(std::max(0.f, 1.f - z * z));
    return Vector3Normalize(Vector3Add(Vector3Scale(axis, z), Vector3Add(Vector3Scale(t1, q * cosf(ph)), Vector3Scale(t2, q * sinf(ph)))));
}

// -----------------------------------------------------------------------------
// Capture hook (only with MOON_SHOTS=<dir>; never writes files in normal play)
// -----------------------------------------------------------------------------
struct CapturePoint {
    float time;
    const char* filename;
    bool taken;
};

static CapturePoint s_captures[] = {
    {   0.0f, "moon_000s.png", false },
    {   2.0f, "moon_002s.png", false },
    {   5.0f, "moon_005s.png", false },
    {   7.0f, "moon_007s.png", false },
    {   9.5f, "moon_009_5s.png", false },
    {  12.0f, "moon_012s.png", false },
    {  15.0f, "moon_015s.png", false },
    {  22.0f, "moon_022s.png", false },
    {  26.5f, "moon_026_5s.png", false },
    {  28.4f, "moon_028_4s.png", false },
    {  30.0f, "moon_030s.png", false },
    {  34.0f, "moon_034s.png", false },
    {  38.5f, "moon_038_5s.png", false },
    {  42.0f, "moon_042s.png", false },
    {  50.0f, "moon_050s.png", false },
    {  75.0f, "moon_075s.png", false }
};

MoonLevel::MoonLevel() = default;

MoonLevel::~MoonLevel() {
    shutdown();
}

Vector3 MoonLevel::sunDirection() {
    // Toward the sun, camera frame of the side view: from the upper right, a little in front of
    // the moon, so the sun-facing hemisphere is the one we see (~65% lit) like the reference.
    Camera3D c{};
    c.target = {0, 0, 0};
    c.position = Vector3Scale(Vector3Normalize({0, 5, 30}), CameraSystem::kCameraDistance);
    c.up = {0, 1, 0};
    const Vector3 f = Vector3Normalize(Vector3Subtract(c.target, c.position));
    const Vector3 r = Vector3Normalize(Vector3CrossProduct(f, c.up));
    const Vector3 u = Vector3CrossProduct(r, f);
    static const bool inFrame = getenv("MOON_SUN_INFRAME") != nullptr;
    if (inFrame) return Vector3Normalize(Vector3Add(Vector3Scale(f, 1.0f), Vector3Add(Vector3Scale(r, 0.42f), Vector3Scale(u, 0.24f))));
    return Vector3Normalize(Vector3Add(Vector3Scale(r, 0.60f), Vector3Add(Vector3Scale(u, 0.42f), Vector3Scale(f, -0.68f))));   // t248: behind-right of the camera, moon ~80% lit
}


// -----------------------------------------------------------------------------
// t248: opening turn from the Warden's sun
// -----------------------------------------------------------------------------
static float smoother(float a, float b, float x) {
    const float t = Clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}
static Vector3 basisToWorld(Vector3 r, Vector3 u, Vector3 f, Vector3 l) {
    return Vector3Add(Vector3Scale(r, l.x), Vector3Add(Vector3Scale(u, l.y), Vector3Scale(f, l.z)));
}
static Vector3 basisToLocal(Vector3 r, Vector3 u, Vector3 f, Vector3 w) {
    return {Vector3DotProduct(w, r), Vector3DotProduct(w, u), Vector3DotProduct(w, f)};
}
static Vector3 rotYaw(Vector3 v, float a) {     // about local up
    const float c = cosf(a), s = sinf(a);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}
static Vector3 rotPitch(Vector3 v, float a) {   // about local right
    const float c = cosf(a), s = sinf(a);
    return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

static constexpr float kTurnHold = 1.0f;    // hold the Warden's last pose (continuity)
static constexpr float kTurnEnd = 8.0f;     // facing the moon
static constexpr float kSkySwitch = 4.5f;   // sun and planet both out of view: Warden scenery -> moon scenery

float MoonLevel::turnE(float levelTime) const {
    if (!hasEntry_) return 1.f;
    return smoother(kTurnHold, kTurnEnd, levelTime);
}

// Delta(a) = Yaw(a*psi) * Pitch(a*phi) maps the final sun (local) onto the opening sun (local).
// The view basis at e is C1 * Delta(1 - e)^-1 (no roll: a yaw and a pitch only).
Vector3 MoonLevel::camToWorld(float levelTime, Vector3 l) const {
    const float a = 1.f - turnE(levelTime);
    // K = Delta(a)^-1 = Pitch(-a phi) * Yaw(-a psi)  (yaw applied first)
    const Vector3 k = rotPitch(rotYaw(l, -a * turnYaw_), -a * turnPitch_);
    return basisToWorld(c1r_, c1u_, c1f_, k);
}

Vector3 MoonLevel::worldToCam(float levelTime, Vector3 w) const {
    const float a = 1.f - turnE(levelTime);
    const Vector3 l1 = basisToLocal(c1r_, c1u_, c1f_, w);
    return rotYaw(rotPitch(l1, a * turnPitch_), a * turnYaw_);   // Delta(a) = Yaw * Pitch (pitch first)
}

Camera3D MoonLevel::turnCamera(const Camera3D& base, float levelTime) const {
    if (finaleLT_ >= 0.f && (finJit_.x != 0.f || finJit_.y != 0.f)) {   // t267: the finale's camera shake (angular)
        Camera3D c = (!hasEntry_ || turnE(levelTime) >= 1.f) ? base : [&] { Camera3D q = base; const Vector3 d = Vector3Subtract(base.target, base.position);
            q.target = Vector3Add(base.position, camToWorld(levelTime, basisToLocal(c1r_, c1u_, c1f_, d))); q.up = camToWorld(levelTime, basisToLocal(c1r_, c1u_, c1f_, base.up)); return q; }();
        const Vector3 d = Vector3Subtract(c.target, c.position); const float L = Vector3Length(d);
        const Vector3 f = Vector3Scale(d, 1.f / std::max(L, 1e-4f)), r = Vector3Normalize(Vector3CrossProduct(f, c.up)), u = Vector3CrossProduct(r, f);
        c.target = Vector3Add(c.target, Vector3Add(Vector3Scale(r, finJit_.x * L), Vector3Scale(u, finJit_.y * L)));
        return c;
    }
    if (!hasEntry_ || turnE(levelTime) >= 1.f) return base;
    Camera3D c = base;
    const Vector3 d = Vector3Subtract(base.target, base.position);
    c.target = Vector3Add(base.position, camToWorld(levelTime, basisToLocal(c1r_, c1u_, c1f_, d)));
    c.up = camToWorld(levelTime, basisToLocal(c1r_, c1u_, c1f_, base.up));
    return c;
}

Vector3 MoonLevel::skyMap(Vector3 w) const {
    if (!hasEntry_) return w;
    return basisToWorld(bwr_, bwu_, bwf_, basisToLocal(c0r_, c0u_, c0f_, w));
}

Vector3 MoonLevel::starMap(Vector3 w) const {
    if (!hasEntry_) return w;
    return basisToWorld(c1r_, c1u_, c1f_, basisToLocal(c0r_, c0u_, c0f_, w));
}

void MoonLevel::setEntrySky(const EntrySky& e, const View& view) {
    entry_ = e;
    const Camera3D c = CameraSystem::makeCamera(view, 0.f);
    c1f_ = Vector3Normalize(Vector3Subtract(c.target, c.position));
    c1r_ = Vector3Normalize(Vector3CrossProduct(c1f_, c.up));
    c1u_ = Vector3CrossProduct(c1r_, c1f_);
    const Vector3 sun0 = Vector3Normalize(basisToLocal(c1r_, c1u_, c1f_, e.sunCam));         // opening (Warden end)
    const Vector3 sun1 = Vector3Normalize(basisToLocal(c1r_, c1u_, c1f_, sunDirection()));   // moon pose
    // pitch: bring sun1's elevation to sun0's
    const float rho = sqrtf(sun1.y * sun1.y + sun1.z * sun1.z);
    const float beta = atan2f(sun1.z, sun1.y);
    const float ac = acosf(Clamp(sun0.y / std::max(rho, 1e-4f), -1.f, 1.f));
    const float p1 = ac - beta, p2 = -ac - beta;
    auto wrap = [](float x) { while (x > PI) x -= 2.f * PI; while (x < -PI) x += 2.f * PI; return x; };
    turnPitch_ = fabsf(wrap(p1)) < fabsf(wrap(p2)) ? wrap(p1) : wrap(p2);
    const Vector3 sp = rotPitch(sun1, turnPitch_);
    turnYaw_ = wrap(atan2f(sun0.x, sun0.z) - atan2f(sp.x, sp.z));
    hasEntry_ = true;
    // opening basis C0 = C1 * Delta(1)^-1
    c0r_ = camToWorld(0.f, {1, 0, 0}); c0u_ = camToWorld(0.f, {0, 1, 0}); c0f_ = camToWorld(0.f, {0, 0, 1});
    bwf_ = Vector3Normalize(e.f);
    bwr_ = Vector3Normalize(Vector3CrossProduct(bwf_, e.u));
    bwu_ = Vector3CrossProduct(bwr_, bwf_);
    const Vector3 chk = worldToCam(0.f, sunDirection());
    fprintf(stderr, "MOON entry fromWarden=%d yaw=%.1f pitch=%.1f sun0=(%.3f %.3f %.3f) check=(%.3f %.3f %.3f) sunR=%.1fdeg\n",
            e.fromWarden ? 1 : 0, turnYaw_ * RAD2DEG, turnPitch_ * RAD2DEG, sun0.x, sun0.y, sun0.z, chk.x, chk.y, chk.z, e.sunR * RAD2DEG);
}

float MoonLevel::skyDim(float levelTime) const {
    levelTime = storyTime(levelTime);   // t252
    if (!hasEntry_) return 0.f;
    const float e = turnE(levelTime);
    const float expo = entry_.expo * (1.f - smoother(0.15f, 0.55f, e));          // Warden's exposure-down eases off
    const float clearDim = entry_.fromWarden ? 0.5f * (1.f - smoother(0.f, 0.9f, levelTime)) : 0.f;   // LEVEL CLEAR dim lifts
    return 1.f - (1.f - expo) * (1.f - clearDim);
}

Camera3D MoonLevel::sceneCamera(const View& view, float levelTime) const {
    Camera3D c = CameraSystem::makeCamera(view, 0.f);
    // Slow cinematic drift (Gemini's), kept small
    const Vector3 d{sinf(levelTime * 0.12f) * 0.25f, cosf(levelTime * 0.09f) * 0.15f, 0.f};
    c.target = Vector3Add(c.target, d);
    c.position = Vector3Add(c.position, d);
    return c;
}

// The moon sits far behind the playfield (never intersects ships), scaled so its screen size and
// position stay what the layout asks for.
static constexpr float kMoonBehind = 60.f;

void MoonLevel::moonFrame(const View& view, const Camera3D& c, float levelTime, Vector3& centre, float& radius) const {
    const float D = Vector3Distance(c.position, c.target);
    const float k = (D + kMoonBehind) / std::max(D, 0.01f);
    const Vector3 focal = CameraSystem::toWorld(view, c, moonPosLocal_, 0.f);
    centre = Vector3Add(c.position, Vector3Scale(Vector3Subtract(focal, c.position), k));
    const float growth = ss(0.0f, 24.0f, levelTime);
    radius = moonBaseRadius_ * Lerp(0.82f, 1.0f, growth) * CameraSystem::worldPerLocal(view) * k;
}

Vector3 MoonLevel::deepWorld(const View& view, const Camera3D& c, Vector2 local, float depthWorld) const {
    // like CameraSystem::toWorld but allows going far behind the focal plane (depthWorld < 0)
    const float D = Vector3Distance(c.position, c.target);
    const Vector3 focal = CameraSystem::toWorld(view, c, local, 0.f);
    const float k = (D - depthWorld) / std::max(D, 0.01f);
    return Vector3Add(c.position, Vector3Scale(Vector3Subtract(focal, c.position), k));
}

Matrix MoonLevel::moonRotation(float levelTime) const {
    // slow spin about a tilted axis (keeps the impact site in view)
    return MatrixMultiply(MatrixRotateY(0.004f * levelTime), MatrixRotateZ(0.18f));
}

static void setupLitShaderLocs(Shader& sh) {
    sh.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(sh, "vertexPosition");
    sh.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(sh, "vertexTexCoord");
    sh.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(sh, "vertexNormal");
    sh.locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(sh, "vertexColor");
    sh.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(sh, "mvp");
    sh.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(sh, "matModel");
    sh.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(sh, "texture0");
    sh.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(sh, "colDiffuse");
}

void MoonLevel::init(const View& view) {
    if (initialized_) return;
    initialized_ = true;
    if (!getenv("NO_TRENCH")) {   // t252: the Moon Base trench run (NO_TRENCH=1: the old breakup level)
        trench_ = std::make_unique<TrenchRun>();
        if (!trench_->init()) trench_.reset();
    }
    finaleLT_ = -1.f;
    impacted_ = false;
    detonated_ = false;
    astAlive_ = false;
    flashAlpha_ = 0.f;
    coreGlow_ = 0.f;
    interiorGlow_ = 0.f;
    ventAcc_ = 0.f;
    nextWaveTime_ = 42.0f;
    waveCount_ = 0;
    sunInFrame_ = getenv("MOON_SUN_INFRAME") != nullptr;
    for (auto& cap : s_captures) cap.taken = false;

    // Layout: whole moon in frame, ~45% of the screen height, right of centre (room for the plume)
    fwd_ = view.forward();
    moonPosLocal_ = {view.forward() * 0.58f, view.lateral() * 0.43f};
    moonBaseRadius_ = view.lateral() * 0.225f;

    // Impact site: on the sun-facing hemisphere, toward us and up-right, fixed in the moon's frame
    {
        const Camera3D c = CameraSystem::makeCamera(view, 0.f);
        const Vector3 f = Vector3Normalize(Vector3Subtract(c.target, c.position));
        const Vector3 r = Vector3Normalize(Vector3CrossProduct(f, c.up));
        const Vector3 u = Vector3CrossProduct(r, f);
        Vector3 dw = Vector3Normalize(Vector3Add(Vector3Scale(f, -0.90f), Vector3Add(Vector3Scale(r, 0.30f), Vector3Scale(u, 0.14f))));
        if (sunInFrame_) dw = Vector3Normalize(Vector3Add(Vector3Scale(f, -0.25f), Vector3Add(Vector3Scale(r, 0.80f), Vector3Scale(u, 0.45f))));
        impactCam_ = dw;
        viewBack_ = Vector3Negate(f);
        impactObj_ = Vector3Normalize(Vector3Transform(dw, MatrixTranspose(moonRotation(kImpactTime))));
        // oblique fall: comes in from the upper right, mostly across the screen plane so it stays visible
        astInDir_ = Vector3Normalize(Vector3Add(Vector3Scale(f, -0.22f), Vector3Add(Vector3Scale(r, 0.66f), Vector3Scale(u, 0.72f))));
    }

    Image radialImg = GenImageGradientRadial(128, 128, 0.0f, WHITE, BLANK);
    softDotTexture_ = LoadTextureFromImage(radialImg);
    UnloadImage(radialImg);
    softDotLoaded_ = true;

    // Shaders
    moonShader_ = LoadShaderFromMemory(MOON_VS, MOON_FS);
    if (moonShader_.id > 0) {
        moonShaderLoaded_ = true;
        setupLitShaderLocs(moonShader_);
        moonShader_.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(moonShader_, "craterTex");
        moonShader_.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(moonShader_, "crackTex");
    }
    rockShader_ = LoadShaderFromMemory(ROCK_VS, ROCK_FS);
    if (rockShader_.id > 0) {
        rockShaderLoaded_ = true;
        setupLitShaderLocs(rockShader_);
    }
    fireballShader_ = LoadShaderFromMemory(FIREBALL_VS, FIREBALL_FS);
    if (fireballShader_.id > 0) fireballShaderLoaded_ = true;
    dustShader_ = LoadShaderFromMemory(DUST_VS, DUST_FS);
    if (dustShader_.id > 0) dustShaderLoaded_ = true;

    // Ryan's moon model (its own materials stay untouched; we draw its meshes with moonMat_)
    const char* moonPath = "assets/moon/moon.glb";
#if defined(__aarch64__)
    if (FileExists("assets/moon/moon_tablet.glb"))
        moonPath = "assets/moon/moon_tablet.glb";
#endif
    if (FileExists(moonPath)) {
        wholeMoonModel_ = LoadModel(moonPath);
        wholeMoonLoaded_ = (wholeMoonModel_.meshCount > 0);
        fprintf(stderr, "MOON model %s loaded=%d meshes=%d\n", moonPath, (int)wholeMoonLoaded_, wholeMoonModel_.meshCount);
    }
    if (!wholeMoonLoaded_) {
        wholeMoonModel_ = LoadModelFromMesh(GenMeshSphere(1.0f, 64, 64));
        wholeMoonLoaded_ = true;
    }
    texturesLoaded_ = false;
    for (int i = 0; i < wholeMoonModel_.materialCount; i++) {
        const Material& mat = wholeMoonModel_.materials[i];
        if (mat.maps[MATERIAL_MAP_DIFFUSE].texture.id > 0 && mat.maps[MATERIAL_MAP_DIFFUSE].texture.width > 32) {
            moonTexture_ = mat.maps[MATERIAL_MAP_DIFFUSE].texture;
            texturesLoaded_ = true;
        }
    }
    if (!texturesLoaded_) {   // fallback: neutral grey detail
        Image g = GenImageColor(4, 4, Color{64, 64, 64, 255});
        moonTexture_ = LoadTextureFromImage(g);
        UnloadImage(g);
    }
    SetTextureWrap(moonTexture_, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(moonTexture_, TEXTURE_FILTER_BILINEAR);

    bakeSurfaceTextures();

    moonMat_ = LoadMaterialDefault();
    rockMat_ = LoadMaterialDefault();
    if (moonShaderLoaded_) moonMat_.shader = moonShader_;
    if (rockShaderLoaded_) rockMat_.shader = rockShader_;
    moonMat_.maps[MATERIAL_MAP_DIFFUSE].texture = moonTexture_;
    moonMat_.maps[MATERIAL_MAP_NORMAL].texture = craterTex_;
    moonMat_.maps[MATERIAL_MAP_ROUGHNESS].texture = crackTex_;
    rockMat_.maps[MATERIAL_MAP_DIFFUSE].texture = moonTexture_;
    matsReady_ = true;

    buildMoonShards();
    buildRockVariants();
    coreModel_ = LoadModelFromMesh(GenMeshSphere(0.56f, 32, 32));   // molten interior seen between the plates
    coreLoaded_ = true;
}

// CPU bake (once per level start, ~0.1-0.3 s): crater normals + albedo, and the crack network.
// Equirect, u = atan2(z, x) / 2pi + 0.5, v = asin(y) / pi + 0.5 (same as MOON_FS).
void MoonLevel::bakeSurfaceTextures() {
    const int W = 2048, H = 1024;
    std::vector<float> cosLat(H), sinLat(H), cosLon(W), sinLon(W);
    for (int j = 0; j < H; j++) { const float lat = ((j + 0.5f) / H - 0.5f) * PI; cosLat[j] = cosf(lat); sinLat[j] = sinf(lat); }
    for (int i = 0; i < W; i++) { const float lon = ((i + 0.5f) / W - 0.5f) * 2.f * PI; cosLon[i] = cosf(lon); sinLon[i] = sinf(lon); }
    auto dirOf = [&](int i, int j) -> Vector3 { return {cosLat[j] * cosLon[i], sinLat[j], cosLat[j] * sinLon[i]}; };
    // visit every texel within angular radius `rad` of unit vector p
    auto forCap = [&](Vector3 p, float rad, auto&& fn) {
        const float lat = asinf(Clamp(p.y, -1.f, 1.f)), lon = atan2f(p.z, p.x);
        const int j0 = std::max(0, (int)floorf(((lat - rad) / PI + 0.5f) * H));
        const int j1 = std::min(H - 1, (int)ceilf(((lat + rad) / PI + 0.5f) * H));
        const float maxLat = std::max(fabsf(lat - rad), fabsf(lat + rad));
        int i0, i1;
        if (maxLat > 1.45f) { i0 = 0; i1 = W - 1; }
        else {
            const float dl = rad / std::max(0.12f, cosf(maxLat));
            i0 = (int)floorf(((lon - dl) / (2.f * PI) + 0.5f) * W);
            i1 = (int)ceilf(((lon + dl) / (2.f * PI) + 0.5f) * W);
            if (i1 - i0 >= W) { i0 = 0; i1 = W - 1; }
        }
        const float cr = 2.f * sinf(std::min(rad, 3.1f) * 0.5f), cr2 = cr * cr;
        for (int j = j0; j <= j1; j++)
            for (int ii = i0; ii <= i1; ii++) {
                const int i = ((ii % W) + W) % W;
                const Vector3 q = dirOf(i, j);
                const float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > cr2) continue;
                fn(i, j, q, sqrtf(d2));
            }
    };

    // ---- craters: height (radians) + albedo ----
    std::vector<float> h(W * H, 0.f), alb(W * H, 0.60f);
    BakeRng rng(0x5EED1234u);
    // a few broad darker plains (subtle, the reference moon is fairly even)
    for (int k = 0; k < 7; k++) {
        const Vector3 c = randomUnit(rng);
        const float R = rng.range(0.25f, 0.55f), depth = rng.range(0.05f, 0.10f);
        forCap(c, R, [&](int i, int j, Vector3, float d) { const float x = d / R; alb[j * W + i] -= depth * (1.f - x * x) * (1.f - x * x); });
    }
    struct Cr { int n; float r0, r1; };
    const Cr classes[] = {{8, 0.10f, 0.20f}, {60, 0.035f, 0.09f}, {420, 0.012f, 0.035f}, {1400, 0.004f, 0.012f}};
    for (const Cr& cl : classes) {
        for (int k = 0; k < cl.n; k++) {
            const Vector3 c = randomUnit(rng);
            const float R = cl.r0 * powf(cl.r1 / cl.r0, rng.next() * rng.next());
            const float depth = R * rng.range(0.16f, 0.24f);
            const float fresh = rng.next();
            forCap(c, R * 1.9f, [&](int i, int j, Vector3, float d) {
                const float x = d / R;
                float dh;
                if (x < 1.f) dh = depth * (x * x - 1.f) * 0.85f + depth * 0.25f * expf(-(x - 1.f) * (x - 1.f) / 0.02f);
                else dh = depth * 0.25f * expf(-(x - 1.f) * (x - 1.f) / 0.06f);
                h[j * W + i] += dh;
                // fresh craters: bright rims / ejecta, slightly dark floors
                float da = 0.f;
                if (x < 0.85f) da = -0.035f * (0.5f + fresh);
                da += 0.07f * fresh * expf(-(x - 1.f) * (x - 1.f) / 0.05f);
                if (x > 1.f) da += 0.03f * fresh * expf(-(x - 1.f) * 1.6f);
                alb[j * W + i] += da;
            });
        }
    }
    Image ci = GenImageColor(W, H, BLANK);
    Color* cpx = (Color*)ci.data;
    const float dLon = 2.f * PI / W, dLat = PI / H, kSlope = 1.15f;
    for (int j = 0; j < H; j++) {
        const int jn = std::min(H - 1, j + 1), js = std::max(0, j - 1);
        const float cl = std::max(0.06f, cosLat[j]);
        for (int i = 0; i < W; i++) {
            const int ie = (i + 1) % W, iw = (i + W - 1) % W;
            const float gE = (h[j * W + ie] - h[j * W + iw]) / (2.f * dLon * cl);
            const float gN = (h[jn * W + i] - h[js * W + i]) / ((jn - js) * dLat);
            const Vector3 n = dirOf(i, j);
            const Vector3 east = Vector3Normalize({-sinLon[i], 0.f, cosLon[i]});
            const Vector3 north = Vector3CrossProduct(n, east);   // points toward +y on the sphere
            Vector3 nn = Vector3Subtract(n, Vector3Add(Vector3Scale(east, gE * kSlope), Vector3Scale(north, gN * kSlope)));
            nn = Vector3Normalize(nn);
            const float a = Clamp(alb[j * W + i], 0.08f, 0.92f);
            cpx[j * W + i] = Color{(unsigned char)Clamp((nn.x * 0.5f + 0.5f) * 255.f, 0.f, 255.f),
                                   (unsigned char)Clamp((nn.y * 0.5f + 0.5f) * 255.f, 0.f, 255.f),
                                   (unsigned char)Clamp((nn.z * 0.5f + 0.5f) * 255.f, 0.f, 255.f),
                                   (unsigned char)(a * 255.f)};
        }
    }
    craterTex_ = LoadTextureFromImage(ci);
    UnloadImage(ci);
    SetTextureFilter(craterTex_, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(craterTex_, TEXTURE_WRAP_REPEAT);

    // ---- crack network ----
    std::vector<float> dmin(W * H, 4.f), arrv(W * H, 1.f), charv(W * H, 0.f), plate(W * H, 0.f);
    const Vector3 P = impactObj_;
    const float kCraterR = 0.125f, kArrNorm = 2.3f;
    struct Seg { Vector3 a, b; float wa, wb, arrA, arrB; };
    std::vector<Seg> segs;
    segs.reserve(16000);
    BakeRng cr(0xC4AC3u);
    auto tangentAway = [&](Vector3 p) {   // unit tangent at p pointing away from the impact
        Vector3 t = Vector3Subtract(p, Vector3Scale(P, Vector3DotProduct(p, P)));
        t = Vector3Subtract(t, Vector3Scale(p, Vector3DotProduct(t, p)));
        const float l = Vector3Length(t);
        return l > 1e-5f ? Vector3Scale(t, 1.f / l) : Vector3{0, 1, 0};
    };
    auto rotateInTangent = [&](Vector3 p, Vector3 d, float ang) {
        const Vector3 b = Vector3CrossProduct(p, d);
        return Vector3Normalize(Vector3Add(Vector3Scale(d, cosf(ang)), Vector3Scale(b, sinf(ang))));
    };
    // recursive lightning walk
    struct Walk { Vector3 p, d; float w, arr, len; int depth; };
    std::vector<Walk> stack;
    const int mains = 12;
    for (int k = 0; k < mains; k++) {
        const float th = (k + cr.range(-0.35f, 0.35f)) * 2.f * PI / mains;
        Vector3 t0 = tangentAway(Vector3Normalize(Vector3Add(P, {0.013f, 0.021f, -0.017f})));
        Vector3 d0 = rotateInTangent(P, Vector3Normalize(Vector3Subtract(t0, Vector3Scale(P, Vector3DotProduct(t0, P)))), th);
        // start at the crater edge
        Vector3 p0 = Vector3Normalize(Vector3Add(Vector3Scale(P, cosf(kCraterR * 0.8f)), Vector3Scale(d0, sinf(kCraterR * 0.8f))));
        d0 = tangentAway(p0);
        stack.push_back({p0, d0, cr.range(0.010f, 0.016f), kCraterR * 0.8f / kArrNorm, cr.range(1.15f, 2.0f), 0});
    }
    int guard = 0;
    while (!stack.empty() && guard++ < 8000) {
        Walk wk = stack.back(); stack.pop_back();
        const float step = 0.009f;
        const float total = wk.len;
        float left = wk.len, heading = 0.f;
        Vector3 p = wk.p, d = wk.d;
        float w = wk.w, arr = wk.arr;
        int kinkIn = 0;
        while (left > 0.f && segs.size() < 15800) {
            if (--kinkIn <= 0) {   // lightning: sharp kinks every few steps, otherwise straight
                heading = cr.gauss() * 0.42f;
                kinkIn = 1 + (int)(cr.next() * 4.f);
                d = rotateInTangent(p, d, heading);
            }
            // keep the walk heading outward (no loops back over the sphere)
            const Vector3 away = tangentAway(p);
            d = Vector3Normalize(Vector3Add(d, Vector3Scale(away, 0.10f + 0.05f * wk.depth)));
            const Vector3 np = Vector3Normalize(Vector3Add(p, Vector3Scale(d, step)));
            d = Vector3Normalize(Vector3Subtract(d, Vector3Scale(np, Vector3DotProduct(d, np))));
            const float nw = std::max(0.0016f, w * (1.f - step / std::max(0.05f, left) * 0.85f));
            const float narr = arr + step / kArrNorm;
            segs.push_back({p, np, w, nw, arr, narr});
            p = np; w = nw; arr = narr; left -= step;
            // side branches, thinner and shorter
            if (wk.depth < 3 && cr.next() < 0.055f + 0.02f * wk.depth && left > 0.08f) {
                const float side = cr.next() < 0.5f ? -1.f : 1.f;
                const Vector3 bd = rotateInTangent(p, d, side * cr.range(0.45f, 1.05f));
                stack.push_back({p, bd, w * cr.range(0.45f, 0.65f), arr, std::min(left, total) * cr.range(0.25f, 0.6f), wk.depth + 1});
            }
            if (w <= 0.0017f && left < total * 0.1f) break;
        }
    }
    for (const Seg& s : segs) {
        const Vector3 m = Vector3Normalize(Vector3Add(s.a, s.b));
        const Vector3 ab = Vector3Subtract(s.b, s.a);
        const float len2 = std::max(1e-10f, Vector3DotProduct(ab, ab));
        const float wmax = std::max(s.wa, s.wb);
        forCap(m, sqrtf(len2) * 0.5f + 4.f * wmax + 0.002f, [&](int i, int j, Vector3 q, float) {
            const Vector3 aq = Vector3Subtract(q, s.a);
            const float t = Clamp(Vector3DotProduct(aq, ab) / len2, 0.f, 1.f);
            const Vector3 c = Vector3Add(s.a, Vector3Scale(ab, t));
            const float dist = Vector3Distance(q, c);
            const float w = s.wa + (s.wb - s.wa) * t;
            const float dn = dist / w;
            const int idx = j * W + i;
            if (dn < dmin[idx]) { dmin[idx] = dn; arrv[idx] = s.arrA + (s.arrB - s.arrA) * t; }
        });
    }
    // charred voronoi plates around the crater, with glowing seams that open early
    {
        const float capR = 0.46f;
        std::vector<Vector3> seeds;
        BakeRng pr(0xB1A57u);
        Vector3 up = fabsf(P.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        const Vector3 t1 = Vector3Normalize(Vector3CrossProduct(up, P)), t2 = Vector3CrossProduct(P, t1);
        for (int k = 0; k < 56; k++) {
            const float a = capR * 1.1f * sqrtf(pr.next()), ph = pr.range(0.f, 2.f * PI);
            seeds.push_back(Vector3Normalize(Vector3Add(Vector3Scale(P, cosf(a)), Vector3Add(Vector3Scale(t1, sinf(a) * cosf(ph)), Vector3Scale(t2, sinf(a) * sinf(ph))))));
        }
        forCap(P, capR, [&](int i, int j, Vector3 q, float dP) {
            const float a = 2.f * asinf(std::min(1.f, dP * 0.5f));
            int b1 = -1; float d1 = 1e9f, d2 = 1e9f; int b2 = -1;
            for (int k = 0; k < (int)seeds.size(); k++) {
                const float dd = Vector3DistanceSqr(q, seeds[k]);
                if (dd < d1) { d2 = d1; b2 = b1; d1 = dd; b1 = k; } else if (dd < d2) { d2 = dd; b2 = k; }
            }
            if (b1 < 0 || b2 < 0) return;
            const float sep = std::max(1e-5f, Vector3Distance(seeds[b1], seeds[b2]));
            const float edge = (d2 - d1) / (2.f * sep);   // distance to the bisector
            const float hw = 0.0075f * (1.f - a / capR) + 0.0022f;
            const float fall = ss(capR, capR * 0.55f, a);
            const float dn = edge / hw + (1.f - fall) * 3.f;
            const int idx = j * W + i;
            if (a > kCraterR * 0.9f && dn < dmin[idx]) { dmin[idx] = dn; arrv[idx] = (kCraterR + (a - kCraterR) * 0.55f) / kArrNorm; }
            charv[idx] = std::max(charv[idx], fall * ss(kCraterR * 0.6f, kCraterR * 1.2f, a));
            plate[idx] = (float)((b1 * 2654435761u) % 1000u) / 1000.f;
        });
    }
    Image ki = GenImageColor(W, H, BLANK);
    Color* kpx = (Color*)ki.data;
    for (int idx = 0; idx < W * H; idx++) {
        kpx[idx] = Color{(unsigned char)(Clamp(dmin[idx] / 4.f, 0.f, 1.f) * 255.f),
                         (unsigned char)(Clamp(arrv[idx], 0.f, 1.f) * 255.f),
                         (unsigned char)(Clamp(charv[idx], 0.f, 1.f) * 255.f),
                         (unsigned char)(Clamp(plate[idx], 0.f, 1.f) * 255.f)};
    }
    crackTex_ = LoadTextureFromImage(ki);
    UnloadImage(ki);
    SetTextureFilter(crackTex_, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(crackTex_, TEXTURE_WRAP_REPEAT);
    bakedLoaded_ = true;
    TraceLog(LOG_INFO, "MoonLevel: baked craters + %d crack segments", (int)segs.size());
}

// Icosphere (unit), subdivided `levels` times
static void makeIcosphere(int levels, std::vector<Vector3>& V, std::vector<int>& T) {
    const float t = (1.f + sqrtf(5.f)) * 0.5f;
    V = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
         {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& v : V) v = Vector3Normalize(v);
    T = {0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11, 1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
         3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9, 4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1};
    for (int l = 0; l < levels; l++) {
        std::vector<int> nt;
        std::vector<std::pair<long long, int>> cache;
        auto mid = [&](int a, int b) {
            const long long key = a < b ? ((long long)a << 32) | b : ((long long)b << 32) | a;
            for (auto& c : cache) if (c.first == key) return c.second;
            V.push_back(Vector3Normalize(Vector3Scale(Vector3Add(V[a], V[b]), 0.5f)));
            cache.push_back({key, (int)V.size() - 1});
            return (int)V.size() - 1;
        };
        cache.reserve(T.size());
        for (size_t k = 0; k < T.size(); k += 3) {
            const int a = T[k], b = T[k + 1], c = T[k + 2];
            const int ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
            const int add[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            nt.insert(nt.end(), add, add + 12);
        }
        T.swap(nt);
    }
}

static float vnoise3(Vector3 p) {   // CPU value noise (bakes / meshes only)
    auto h = [](int x, int y, int z) { uint32_t n = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(z * 83492791); n = (n << 13) ^ n; return (float)((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) / 2147483648.f; };
    const int xi = (int)floorf(p.x), yi = (int)floorf(p.y), zi = (int)floorf(p.z);
    const float xf = p.x - xi, yf = p.y - yi, zf = p.z - zi;
    const float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf), w = zf * zf * (3 - 2 * zf);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    return L(L(L(h(xi, yi, zi), h(xi + 1, yi, zi), u), L(h(xi, yi + 1, zi), h(xi + 1, yi + 1, zi), u), v),
             L(L(h(xi, yi, zi + 1), h(xi + 1, yi, zi + 1), u), L(h(xi, yi + 1, zi + 1), h(xi + 1, yi + 1, zi + 1), u), v), w);
}

// The moon cut into 40 plates that tile the sphere exactly (no overlaps), each a wedge: the
// outer crust (triangles of an icosphere nearest its seed, jagged fracture outline) plus fracture
// walls down to an inner point. Vertex colour alpha 255 marks the walls (dark rock, hot veins).
void MoonLevel::buildMoonShards() {
    std::vector<Vector3> V; std::vector<int> T;
    makeIcosphere(4, V, T);   // 5120 triangles
    BakeRng rng(0xF4AC7u);
    Vector3 seeds[kNumShards];
    for (int i = 0; i < kNumShards; i++) {   // jittered Fibonacci seeds: irregular plate sizes
        const float y = 1.0f - (i + 0.5f) / kNumShards * 2.0f;
        const float r = sqrtf(std::max(0.0f, 1.0f - y * y));
        const float phi = i * 2.39996323f;
        seeds[i] = Vector3Normalize(Vector3Add({cosf(phi) * r, y, sinf(phi) * r}, Vector3Scale(randomUnit(rng), 0.18f)));
    }
    // more, smaller plates around the impact
    for (int i = 0; i < 8; i++) {
        const Vector3 d = randomUnit(rng);
        seeds[i * 5] = Vector3Normalize(Vector3Add(impactObj_, Vector3Scale(d, 0.35f)));
    }
    const int nT = (int)T.size() / 3;
    std::vector<int> owner(nT);
    // warp the partition so plate edges are not straight
    auto cellOf = [&](Vector3 p) {
        const Vector3 w = {vnoise3(Vector3Scale(p, 3.f)) - 0.5f, vnoise3(Vector3Add(Vector3Scale(p, 3.f), {7.1f, 2.3f, 5.5f})) - 0.5f, vnoise3(Vector3Add(Vector3Scale(p, 3.f), {3.3f, 9.1f, 1.7f})) - 0.5f};
        const Vector3 q = Vector3Add(p, Vector3Scale(w, 0.22f));
        int best = 0; float bd = 1e9f;
        for (int k = 0; k < kNumShards; k++) { const float d = Vector3DistanceSqr(q, seeds[k]); if (d < bd) { bd = d; best = k; } }
        return best;
    };
    for (int t = 0; t < nT; t++) owner[t] = cellOf(Vector3Normalize(Vector3Add(Vector3Add(V[T[t * 3]], V[T[t * 3 + 1]]), V[T[t * 3 + 2]])));
    // edge -> the two triangles that share it
    std::vector<std::pair<long long, int>> edges; edges.reserve(T.size());
    for (int t = 0; t < nT; t++)
        for (int e = 0; e < 3; e++) {
            const int a = T[t * 3 + e], b = T[t * 3 + (e + 1) % 3];
            edges.push_back({a < b ? ((long long)a << 32) | b : ((long long)b << 32) | a, t});
        }
    std::sort(edges.begin(), edges.end());
    for (int s = 0; s < kNumShards; s++) {
        std::vector<float> pos, nrm, uv; std::vector<unsigned char> col;
        auto addV = [&](Vector3 p, Vector3 n, unsigned char a) {
            pos.insert(pos.end(), {p.x, p.y, p.z}); nrm.insert(nrm.end(), {n.x, n.y, n.z});
            uv.insert(uv.end(), {0.f, 0.f}); col.insert(col.end(), {255, 255, 255, a});
        };
        Vector3 centroid{0, 0, 0}; int cnt = 0;
        for (int t = 0; t < nT; t++) {
            if (owner[t] != s) continue;
            for (int e = 0; e < 3; e++) { const Vector3 p = V[T[t * 3 + e]]; addV(p, p, 0); centroid = Vector3Add(centroid, p); cnt++; }
        }
        if (cnt == 0) continue;
        centroid = Vector3Normalize(centroid);
        const float dk = rng.range(0.58f, 0.74f);   // slab floor radius (thick crust plate)
        for (int t = 0; t < nT; t++) {   // fracture floor: the same cell, deeper, facing inward
            if (owner[t] != s) continue;
            const Vector3 a = Vector3Scale(V[T[t * 3]], dk), b = Vector3Scale(V[T[t * 3 + 1]], dk), c = Vector3Scale(V[T[t * 3 + 2]], dk);
            const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(c, a), Vector3Subtract(b, a)));
            addV(a, n, 255); addV(c, n, 255); addV(b, n, 255);
        }
        // boundary edges: neighbour triangle owned by another plate -> fracture wall to the inner point
        for (int t = 0; t < nT; t++) {
            if (owner[t] != s) continue;
            for (int e = 0; e < 3; e++) {
                const int a = T[t * 3 + e], b = T[t * 3 + (e + 1) % 3];
                const long long key = a < b ? ((long long)a << 32) | b : ((long long)b << 32) | a;
                auto it = std::lower_bound(edges.begin(), edges.end(), std::make_pair(key, -1));
                bool boundary = false;
                for (; it != edges.end() && it->first == key; ++it) if (it->second != t && owner[it->second] != s) boundary = true;
                if (!boundary) continue;
                // fracture wall quad between the crust edge and the floor edge, facing outward
                const Vector3 pa = V[a], pb = V[b], qa = Vector3Scale(V[a], dk), qb = Vector3Scale(V[b], dk);
                const Vector3 m = Vector3Scale(Vector3Add(pa, pb), 0.5f);
                Vector3 n = Vector3Subtract(m, Vector3Scale(centroid, Vector3DotProduct(m, centroid)));
                n = Vector3Length(n) > 1e-5f ? Vector3Normalize(n) : centroid;
                addV(pb, n, 255); addV(pa, n, 255); addV(qa, n, 255);
                addV(pb, n, 255); addV(qa, n, 255); addV(qb, n, 255);
            }
        }
        Mesh mesh{};
        mesh.vertexCount = (int)pos.size() / 3;
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = (float*)MemAlloc((unsigned)(pos.size() * sizeof(float)));
        mesh.normals = (float*)MemAlloc((unsigned)(nrm.size() * sizeof(float)));
        mesh.texcoords = (float*)MemAlloc((unsigned)(uv.size() * sizeof(float)));
        mesh.colors = (unsigned char*)MemAlloc((unsigned)col.size());
        memcpy(mesh.vertices, pos.data(), pos.size() * sizeof(float));
        memcpy(mesh.normals, nrm.data(), nrm.size() * sizeof(float));
        memcpy(mesh.texcoords, uv.data(), uv.size() * sizeof(float));
        memcpy(mesh.colors, col.data(), col.size());
        UploadMesh(&mesh, false);
        shards_[s].model = LoadModelFromMesh(mesh);
        shards_[s].loaded = true;
        shards_[s].restCentroid = centroid;
        shards_[s].offset = {0, 0, 0};
        shards_[s].vel = {0, 0, 0};
        shards_[s].rot = {0, 0, 0};
        shards_[s].spin = {0, 0, 0};
        shards_[s].radius = 32.f;
        shards_[s].hp = 450;
        shards_[s].active = true;
        shards_[s].isRemnantRing = false;
    }
}

// 6 rock shapes: noisy icosphere with a few flat cleavage faces (broken rock), faceted normals
void MoonLevel::buildRockVariants() {
    std::vector<Vector3> V0; std::vector<int> T;
    makeIcosphere(3, V0, T);   // 1280 triangles
    for (int v = 0; v < kNumRockVariants; v++) {
        BakeRng rng(0x7000u + v * 977u);
        std::vector<Vector3> V = V0;
        const Vector3 off = {rng.range(0, 50), rng.range(0, 50), rng.range(0, 50)};
        const Vector3 stretch = {rng.range(0.75f, 1.15f), rng.range(0.6f, 0.95f), rng.range(0.8f, 1.2f)};
        Vector3 cutN[4]; float cutD[4];
        const int cuts = 2 + v % 3;
        for (int k = 0; k < cuts; k++) { cutN[k] = randomUnit(rng); cutD[k] = rng.range(0.55f, 0.8f); }
        for (auto& p : V) {
            float n = 0.f, amp = 0.5f, f = 1.6f;
            for (int o = 0; o < 4; o++) { n += amp * (vnoise3(Vector3Add(Vector3Scale(p, f), off)) - 0.5f); amp *= 0.5f; f *= 2.1f; }
            Vector3 q = Vector3Scale(p, 1.f + 0.55f * n);
            q = {q.x * stretch.x, q.y * stretch.y, q.z * stretch.z};
            for (int k = 0; k < cuts; k++) {   // cleavage planes: clamp onto the plane
                const float d = Vector3DotProduct(q, cutN[k]);
                if (d > cutD[k]) q = Vector3Subtract(q, Vector3Scale(cutN[k], d - cutD[k]));
            }
            p = q;
        }
        float maxD = 0.001f;
        for (auto& p : V) maxD = std::max(maxD, Vector3Length(p));
        for (auto& p : V) p = Vector3Scale(p, 1.f / maxD);
        const int nT = (int)T.size() / 3, nV = (int)V.size();
        std::vector<Vector3> N(nV, Vector3{0, 0, 0});
        for (int t = 0; t < nT; t++) {   // area-weighted smooth normals (cleavage faces stay flat-looking)
            const Vector3 a = V[T[t * 3]], b = V[T[t * 3 + 1]], c = V[T[t * 3 + 2]];
            const Vector3 fn = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
            for (int k = 0; k < 3; k++) N[T[t * 3 + k]] = Vector3Add(N[T[t * 3 + k]], fn);
        }
        Mesh mesh{};
        mesh.vertexCount = nV;
        mesh.triangleCount = nT;
        mesh.vertices = (float*)MemAlloc(nV * 3 * sizeof(float));
        mesh.normals = (float*)MemAlloc(nV * 3 * sizeof(float));
        mesh.texcoords = (float*)MemAlloc(nV * 2 * sizeof(float));
        mesh.indices = (unsigned short*)MemAlloc(nT * 3 * sizeof(unsigned short));
        for (int k = 0; k < nV; k++) {
            const Vector3 n = Vector3Normalize(N[k]);
            mesh.vertices[k * 3] = V[k].x; mesh.vertices[k * 3 + 1] = V[k].y; mesh.vertices[k * 3 + 2] = V[k].z;
            mesh.normals[k * 3] = n.x; mesh.normals[k * 3 + 1] = n.y; mesh.normals[k * 3 + 2] = n.z;
            mesh.texcoords[k * 2] = 0.f; mesh.texcoords[k * 2 + 1] = 0.f;
        }
        for (int k = 0; k < nT * 3; k++) mesh.indices[k] = (unsigned short)T[k];
        UploadMesh(&mesh, false);
        rockModels_[v] = LoadModelFromMesh(mesh);
    }
    rocksLoaded_ = true;
}

void MoonLevel::shutdown() {
    if (!initialized_) return;
    trench_.reset();
    if (matsReady_) {
        // materials only borrow textures / shaders: free the map arrays, not the textures
        MemFree(moonMat_.maps); moonMat_.maps = nullptr;
        MemFree(rockMat_.maps); rockMat_.maps = nullptr;
        matsReady_ = false;
    }
    if (wholeMoonLoaded_) {
        UnloadModel(wholeMoonModel_);   // also frees Ryan's textures it owns (moonTexture_)
        wholeMoonLoaded_ = false;
    }
    if (!texturesLoaded_ && moonTexture_.id > 0) UnloadTexture(moonTexture_);   // our grey fallback only
    moonTexture_ = {};
    texturesLoaded_ = false;
    if (bakedLoaded_) {
        UnloadTexture(craterTex_); craterTex_ = {};
        UnloadTexture(crackTex_); crackTex_ = {};
        bakedLoaded_ = false;
    }
    if (softDotLoaded_) {
        UnloadTexture(softDotTexture_);
        softDotTexture_ = {};
        softDotLoaded_ = false;
    }
    for (auto& s : shards_) {
        if (s.loaded) {
            UnloadModel(s.model);
            s.loaded = false;
        }
    }
    if (rocksLoaded_) {
        for (int i = 0; i < kNumRockVariants; i++) UnloadModel(rockModels_[i]);
        rocksLoaded_ = false;
    }
    if (coreLoaded_) { UnloadModel(coreModel_); coreLoaded_ = false; }
    if (fireballShaderLoaded_) { UnloadShader(fireballShader_); fireballShaderLoaded_ = false; }
    if (moonShaderLoaded_) { UnloadShader(moonShader_); moonShaderLoaded_ = false; }
    if (rockShaderLoaded_) { UnloadShader(rockShader_); rockShaderLoaded_ = false; }
    if (dustShaderLoaded_) { UnloadShader(dustShader_); dustShaderLoaded_ = false; }
    initialized_ = false;
}

void MoonLevel::emitDust(Vector3 pos, Vector3 vel, float size, float grow, float life, float alpha, Color tint, float drag) {
    for (auto& d : dust_) {
        if (d.alive) continue;
        d = MoonParticle{true, pos, vel, size, grow, drag, life, life, alpha, tint};
        return;
    }
}

void MoonLevel::emitSpray(Vector3 pos, Vector3 vel, float size, float life, Color tint) {
    for (auto& d : spray_) {
        if (d.alive) continue;
        d = MoonParticle{true, pos, vel, size, 0.f, 0.05f, life, life, 1.f, tint};
        return;
    }
}

void MoonLevel::emitEmber(Vector3 pos, Vector3 vel, float size, float life) {
    for (auto& e : embers_) {
        if (e.alive) continue;
        e = MoonParticle{true, pos, vel, size, 0.f, 0.15f, life, life, 1.f, WHITE};
        return;
    }
}

void MoonLevel::emitRock(Vector3 pos, Vector3 vel, float r, float albedo, float hot) {
    for (auto& k : rocks_) {
        if (k.alive) continue;
        k.alive = true; k.pos = pos; k.vel = vel; k.r = r; k.albedo = albedo; k.hot = hot;
        k.rot = {frange(0.f, 6.28f), frange(0.f, 6.28f), frange(0.f, 6.28f)};
        k.spin = {frange(-1.5f, 1.5f), frange(-1.5f, 1.5f), frange(-1.5f, 1.5f)};
        k.mesh = rand() % kNumRockVariants;
        return;
    }
}

// Asteroid strike: everything in moon space (radii), the impact normal is impactCam_.
void MoonLevel::triggerImpact(Audio& audio, float& shake) {
    impacted_ = true;
    astAlive_ = false;
    shake = std::max(shake, 0.55f);
    flashAlpha_ = 0.32f;          // warm, capped: never a white-out, never black
    coreGlow_ = 1.0f;
    audio.play(Sfx::BigExplosion);
    const Vector3 N = impactCam_;
    const Vector3 site = Vector3Scale(N, 1.0f);
    // downrange: the asteroid's travel direction projected on the surface (oblique impact)
    Vector3 travel = Vector3Negate(astInDir_);
    Vector3 down = Vector3Subtract(travel, Vector3Scale(N, Vector3DotProduct(travel, N)));
    if (Vector3Length(down) > 1e-4f) down = Vector3Normalize(down);
    const Vector3 plumeAxis = Vector3Normalize(Vector3Add(N, Vector3Scale(down, 0.35f)));
    const Color dustCols[3] = {{176, 162, 142, 255}, {150, 138, 122, 255}, {190, 178, 160, 255}};
    for (int i = 0; i < 260; i++) {   // ejecta cone: grey-brown dust
        const Vector3 dir = coneDir(plumeAxis, 0.62f);
        const float spd = frange(0.08f, 0.80f);
        emitDust(Vector3Add(site, Vector3Scale(dir, frange(0.0f, 0.05f))), Vector3Scale(dir, spd),
                 frange(0.02f, 0.055f), frange(0.01f, 0.035f), frange(4.f, 8.f), frange(0.20f, 0.34f), dustCols[i % 3], 0.25f);
    }
    for (int i = 0; i < 210; i++) {   // rock spray: small irregular bits, fast, wider cone
        const Vector3 dir = coneDir(plumeAxis, 0.80f);
        const float spd = frange(0.30f, 1.5f);
        const unsigned char g = (unsigned char)frange(95.f, 150.f);
        emitSpray(site, Vector3Scale(dir, spd), frange(0.004f, 0.014f), frange(5.f, 12.f), Color{g, (unsigned char)(g - 5), (unsigned char)(g - 12), 255});
    }
    for (int i = 0; i < 110; i++) {   // molten droplets
        const Vector3 dir = coneDir(plumeAxis, 0.55f);
        emitEmber(site, Vector3Scale(dir, frange(0.2f, 1.0f)), frange(0.008f, 0.022f), frange(0.8f, 2.2f));
    }
    // the asteroid itself becomes debris: dark fragments thrown downrange / up
    for (int i = 0; i < 7; i++) {
        const Vector3 dir = coneDir(Vector3Normalize(Vector3Add(N, Vector3Scale(down, 0.8f))), 0.5f);
        emitRock(site, Vector3Scale(dir, frange(0.25f, 0.6f)), frange(0.012f, 0.026f), 0.18f, 0.5f);
    }
    for (int i = 0; i < 7; i++) {     // moon boulders
        const Vector3 dir = coneDir(plumeAxis, 0.7f);
        emitRock(site, Vector3Scale(dir, frange(0.15f, 0.5f)), frange(0.012f, 0.03f), 0.50f, 0.3f);
    }
}

void MoonLevel::triggerBreakup(Audio& audio, float& shake) {
    detonated_ = true;
    shake = std::max(shake, 0.85f);
    flashAlpha_ = 0.30f;
    interiorGlow_ = 1.0f;
    audio.play(Sfx::BigExplosion);
    const Vector3 N = impactCam_;
    const Vector3 camBack = viewBack_;   // unit vector from the scene toward the camera
    for (int i = 0; i < kNumShards; i++) {
        MoonShard& sh = shards_[i];
        // rest centroid in world orientation at breakup
        const Vector3 cw = Vector3Normalize(Vector3Transform(sh.restCentroid, moonRotation(kBreakupTime)));
        const float near = std::max(0.f, Vector3DotProduct(cw, N));
        Vector3 v = Vector3Scale(cw, frange(0.010f, 0.024f) + 0.012f * near * near);
        v = Vector3Add(v, Vector3Scale(Vector3Negate(N), 0.005f));   // asteroid momentum carries the body
        // keep the plates far behind the playfield: damp drift toward the camera
        const float vc = Vector3DotProduct(v, camBack);
        if (vc > 0.f) v = Vector3Subtract(v, Vector3Scale(camBack, vc * 0.75f));
        sh.vel = v;
        sh.spin = {frange(-0.03f, 0.03f), frange(-0.03f, 0.03f), frange(-0.03f, 0.03f)};
        sh.offset = {0, 0, 0};
    }
    const Color dustCols[2] = {{140, 130, 118, 255}, {120, 112, 102, 255}};
    for (int i = 0; i < 130; i++) {
        const Vector3 dir = randomUnitF();
        emitDust(Vector3Scale(dir, frange(0.85f, 1.0f)), Vector3Scale(dir, frange(0.04f, 0.22f)),
                 frange(0.06f, 0.14f), frange(0.01f, 0.04f), frange(8.f, 16.f), frange(0.14f, 0.26f), dustCols[i % 2], 0.08f);
    }
    for (int i = 0; i < 120; i++) {
        const Vector3 dir = randomUnitF();
        emitEmber(Vector3Scale(dir, frange(0.7f, 1.0f)), Vector3Scale(dir, frange(0.1f, 0.6f)), frange(0.012f, 0.03f), frange(2.f, 5.f));
    }
    for (int i = 0; i < 80; i++) {
        const Vector3 dir = randomUnitF();
        const unsigned char g = (unsigned char)frange(80.f, 130.f);
        emitSpray(Vector3Scale(dir, 1.0f), Vector3Scale(dir, frange(0.1f, 0.5f)), frange(0.01f, 0.03f), frange(8.f, 16.f), Color{g, g, (unsigned char)(g - 8), 255});
    }
}

void MoonLevel::spawnChunk(Vector2 pos, Vector2 vel, float depth, float velZ,
                           ChunkSize size, int meshIndex, float radius, int hp) {
    for (auto& c : chunks_) {
        if (!c.alive) {
            c.alive = true;
            c.size = size;
            c.pos = pos;
            c.vel = vel;
            c.hiddenAxis = depth;
            c.velZ = velZ;
            c.rot = {frange(0.f, 2.f * PI), frange(0.f, 2.f * PI), frange(0.f, 2.f * PI)};
            c.spin = {frange(-2.5f, 2.5f), frange(-2.5f, 2.5f), frange(-2.5f, 2.5f)};
            c.radius = radius;
            c.hp = hp;
            c.maxHp = hp;
            c.meshIndex = meshIndex % kNumRockVariants;
            c.mass = (size == ChunkSize::Large) ? 4.5f : (size == ChunkSize::Medium ? 1.6f : 0.6f);
            c.age = (cloudSpawn_ && pos.x < fwd_) ? 0.f : 99.f;   // t247: flies out of the cloud first
            c.hot = (curLevelTime_ < 75.0f) ? Clamp((75.0f - curLevelTime_) / 35.0f, 0.0f, 0.85f) : 0.0f;
            return;
        }
    }
}

void MoonLevel::destroyChunk(int index, Particles& fx, Audio& audio, int& score, const View& view, float s) {
    if (index < 0 || index >= kMaxChunks) return;
    RockChunk& c = chunks_[index];
    if (!c.alive) return;
    c.alive = false;

    float H = view.lateral();
    if (c.size == ChunkSize::Large) {
        score += 150;
        audio.play(Sfx::EnemyExplosion, 0.7f);
        fx.explosion(view.toScreen(c.pos), 1.5f * s, {255, 170, 70, 255});
        // Break into 2 medium rocks
        spawnChunk(Vector2Add(c.pos, {0.f, 15.f * s}), Vector2Add(c.vel, {frange(-30.f, 30.f) * s, 45.f * s}),
                   c.hiddenAxis, c.velZ + 0.15f, ChunkSize::Medium, (c.meshIndex + 1) % kNumRockVariants,
                   H * 0.042f, 260);
        spawnChunk(Vector2Add(c.pos, {0.f, -15.f * s}), Vector2Add(c.vel, {frange(-30.f, 30.f) * s, -45.f * s}),
                   c.hiddenAxis, c.velZ - 0.15f, ChunkSize::Medium, (c.meshIndex + 2) % kNumRockVariants,
                   H * 0.042f, 260);
    } else if (c.size == ChunkSize::Medium) {
        score += 75;
        audio.play(Sfx::Explosion, 0.6f);
        fx.explosion(view.toScreen(c.pos), 1.0f * s, {255, 190, 90, 255});
        // Break into 2 small rocks
        spawnChunk(Vector2Add(c.pos, {0.f, 8.f * s}), Vector2Add(c.vel, {frange(-40.f, 40.f) * s, 60.f * s}),
                   c.hiddenAxis, c.velZ + 0.25f, ChunkSize::Small, (c.meshIndex + 3) % kNumRockVariants,
                   H * 0.018f, 80);
        spawnChunk(Vector2Add(c.pos, {0.f, -8.f * s}), Vector2Add(c.vel, {frange(-40.f, 40.f) * s, -60.f * s}),
                   c.hiddenAxis, c.velZ - 0.25f, ChunkSize::Small, (c.meshIndex + 4) % kNumRockVariants,
                   H * 0.018f, 80);
    } else {
        score += 35;
        audio.play(Sfx::Explosion, 0.45f);
        fx.explosion(view.toScreen(c.pos), 0.6f * s, {255, 210, 110, 255});
        fx.sparks(view.toScreen(c.pos), {0, 0}, 12, {255, 180, 80, 255}, 180.f * s);
    }
}

void MoonLevel::spawnDebrisFromCloud(int waveType, const View& view, float s, float intensity) {
    // Chunks drift OUT OF THE DEBRIS CLOUD toward and past the player at various depths
    Vector2 cloudOrigin = moonPosLocal_;
    float H = view.lateral();
    float fwd = view.forward();

    // Scale radii so near rocks are clearly visible
    float rLarge = H * 0.085f;  // ~17% of screen height
    float rMed = H * 0.042f;    // ~8.4% of screen height
    float rSmall = H * 0.018f;  // ~3.6% of screen height

    // Perspective & depth layers
    float farDepth = frange(-3.2f, -1.8f);
    float playDepth = frange(-0.6f, 0.6f);
    float nearDepth = frange(1.6f, 2.8f);

    int countMul = (int)(1.0f + intensity * 2.5f);
    cloudSpawn_ = true;   // t247: chunks that start inside the view fly out of the cloud (no pop-in)

    switch (waveType % 4) {
        case 0: {
            // Pattern A: Upper Lane Open (player can fly in top 35% safely)
            spawnChunk(Vector2Add(cloudOrigin, {frange(20.f, 80.f) * s, H * 0.22f}), {-150.f * s, 10.f * s}, farDepth, -0.05f, ChunkSize::Large, 0, rLarge, 700);
            spawnChunk(Vector2Add(cloudOrigin, {frange(0.f, 60.f) * s, H * 0.40f}), {-180.f * s, -12.f * s}, playDepth, 0.0f, ChunkSize::Large, 1, rLarge, 550);
            spawnChunk({fwd + 40.f * s, H * 0.72f}, {-210.f * s, -15.f * s}, playDepth, 0.05f, ChunkSize::Medium, 2, rMed, 260);
            spawnChunk({fwd + 80.f * s, H * 0.88f}, {-290.f * s, 0.f}, nearDepth, 0.1f, ChunkSize::Small, 3, rSmall, 90);
            if (countMul > 1) {
                spawnChunk({fwd + 120.f * s, H * 0.55f}, {-200.f * s, 8.f * s}, playDepth, -0.02f, ChunkSize::Medium, 4, rMed, 240);
                spawnChunk({fwd + 160.f * s, H * 0.80f}, {-320.f * s, 0.f}, nearDepth, 0.08f, ChunkSize::Medium, 5, rMed, 240);
            }
            break;
        }
        case 1: {
            // Pattern B: Lower Lane Open (player can fly in bottom 35% safely)
            spawnChunk(Vector2Add(cloudOrigin, {frange(30.f, 90.f) * s, -H * 0.15f}), {-160.f * s, -8.f * s}, farDepth, 0.05f, ChunkSize::Large, 2, rLarge, 650);
            spawnChunk(Vector2Add(cloudOrigin, {frange(0.f, 50.f) * s, -H * 0.28f}), {-190.f * s, 10.f * s}, playDepth, 0.0f, ChunkSize::Large, 3, rLarge, 550);
            spawnChunk({fwd + 50.f * s, H * 0.22f}, {-220.f * s, 14.f * s}, playDepth, -0.04f, ChunkSize::Medium, 4, rMed, 250);
            spawnChunk({fwd + 90.f * s, H * 0.12f}, {-300.f * s, 0.f}, nearDepth, -0.1f, ChunkSize::Small, 5, rSmall, 90);
            if (countMul > 1) {
                spawnChunk({fwd + 130.f * s, H * 0.38f}, {-210.f * s, -10.f * s}, playDepth, 0.03f, ChunkSize::Medium, 0, rMed, 230);
                spawnChunk({fwd + 170.f * s, H * 0.20f}, {-330.f * s, 0.f}, nearDepth, -0.08f, ChunkSize::Medium, 1, rMed, 230);
            }
            break;
        }
        case 2: {
            // Pattern C: Center Corridor Open (top and bottom blocked)
            spawnChunk({fwd + 20.f * s, H * 0.15f}, {-170.f * s, 8.f * s}, playDepth, 0.0f, ChunkSize::Large, 4, rLarge, 580);
            spawnChunk({fwd + 30.f * s, H * 0.85f}, {-170.f * s, -8.f * s}, playDepth, 0.0f, ChunkSize::Large, 5, rLarge, 580);
            spawnChunk({fwd + 70.f * s, H * 0.25f}, {-310.f * s, 0.f}, nearDepth, -0.08f, ChunkSize::Medium, 1, rMed, 240);
            spawnChunk({fwd + 70.f * s, H * 0.75f}, {-210.f * s, 0.f}, farDepth, 0.08f, ChunkSize::Medium, 2, rMed, 240);
            spawnChunk({fwd + 110.f * s, H * 0.10f}, {-240.f * s, 0.f}, playDepth, 0.0f, ChunkSize::Small, 3, rSmall, 80);
            spawnChunk({fwd + 110.f * s, H * 0.90f}, {-240.f * s, 0.f}, playDepth, 0.0f, ChunkSize::Small, 0, rSmall, 80);
            if (countMul > 1) {
                spawnChunk({fwd + 150.f * s, H * 0.82f}, {-340.f * s, 0.f}, nearDepth, 0.05f, ChunkSize::Large, 2, rLarge, 600);
            }
            break;
        }
        case 3: {
            // Pattern D: Staggered Slalom (alternating gates)
            spawnChunk({fwd + 20.f * s, H * 0.30f}, {-190.f * s, 15.f * s}, playDepth, 0.0f, ChunkSize::Medium, 1, rMed, 280);
            spawnChunk({fwd + 70.f * s, H * 0.70f}, {-190.f * s, -15.f * s}, playDepth, 0.0f, ChunkSize::Medium, 3, rMed, 280);
            spawnChunk({fwd + 120.f * s, H * 0.35f}, {-200.f * s, 10.f * s}, farDepth, 0.05f, ChunkSize::Large, 0, rLarge, 500);
            spawnChunk({fwd + 50.f * s, H * 0.85f}, {-320.f * s, 0.f}, nearDepth, 0.0f, ChunkSize::Small, 2, rSmall, 80);
            spawnChunk({fwd + 100.f * s, H * 0.15f}, {-320.f * s, 0.f}, nearDepth, 0.0f, ChunkSize::Small, 4, rSmall, 80);
            if (countMul > 1) {
                spawnChunk({fwd + 140.f * s, H * 0.50f}, {-210.f * s, -5.f * s}, playDepth, 0.0f, ChunkSize::Large, 5, rLarge, 550);
                spawnChunk({fwd + 180.f * s, H * 0.28f}, {-350.f * s, 0.f}, nearDepth, 0.04f, ChunkSize::Medium, 3, rMed, 240);
            }
            break;
        }
    }
    cloudSpawn_ = false;
}

void MoonLevel::updateDebrisStorm(float dt, float levelTime, const View& view, float s,
                                  const Player& player, float& shake) {
    (void)dt;
    if (levelTime < 40.0f || levelTime > 170.0f) return;

    // Intensity curve: light at 40-60s, building to peak storm at 120-150s, easing off at 170s
    float stormIntensity = 0.0f;
    if (levelTime < 120.0f) {
        stormIntensity = ss(40.0f, 120.0f, levelTime) * 0.75f;
    } else if (levelTime <= 150.0f) {
        stormIntensity = 0.75f + ss(120.0f, 135.0f, levelTime) * 0.25f; // Peak storm
    } else {
        stormIntensity = 1.0f - ss(150.0f, 170.0f, levelTime) * 0.85f;  // Easing off
    }

    if (levelTime >= nextWaveTime_) {
        spawnDebrisFromCloud(waveCount_++, view, s, stormIntensity);
        // Interval tightens during the peak storm (down to 1.6s for 140s dense storm)
        float interval = Lerp(4.5f, 1.6f, stormIntensity);
        nextWaveTime_ = levelTime + interval;
    }

    // Camera proximity rumble when huge rocks pass close to the ship
    for (const auto& c : chunks_) {
        if (!c.alive || c.size != ChunkSize::Large) continue;
        float d = Vector2Distance(player.pos, c.pos);
        float triggerDist = (c.radius + 50.f) * s;
        if (d < triggerDist) {
            float rumble = (1.0f - d / triggerDist) * 0.10f;
            shake = std::max(shake, rumble);
        }
    }
}

void MoonLevel::checkScreenshots(float levelTime) {
    static const char* dir = getenv("MOON_SHOTS");   // capture runs only
    if (dir) {
        for (auto& cap : s_captures) {
            if (!cap.taken && levelTime >= cap.time) {
                cap.taken = true;
                rlDrawRenderBatchActive();
                Image screenImg = LoadImageFromScreen();
                const int targetW = screenImg.height * 16 / 9;   // ultrawide desktop: keep the 16:9 game frame
                if (targetW < screenImg.width) ImageCrop(&screenImg, Rectangle{(float)((screenImg.width - targetW) / 2), 0.f, (float)targetW, (float)screenImg.height});
                ExportImage(screenImg, TextFormat("%s/%s", dir, cap.filename));
                UnloadImage(screenImg);
                TraceLog(LOG_INFO, "MoonLevel: Screenshot %s captured at %.1fs", cap.filename, levelTime);
            }
        }
        if (s_captures[sizeof(s_captures) / sizeof(s_captures[0]) - 1].taken && getenv("MOON_AUTOEXIT")) {
            TraceLog(LOG_INFO, "MoonLevel: all screenshots captured. Auto-exiting.");
            CloseWindow();
            exit(0);
        }
    }
    if (const char* e = getenv("MOON_SHOT_AT")) {   // single test frame: MOON_SHOT_AT=<t> (needs MOON_SHOTS)
        static bool done = false;
        if (!done && dir && levelTime >= (float)atof(e)) {
            done = true;
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            const int targetW = img.height * 16 / 9;
            if (targetW < img.width) ImageCrop(&img, Rectangle{(float)((img.width - targetW) / 2), 0.f, (float)targetW, (float)img.height});
            ExportImage(img, TextFormat("%s/shot_%s.png", dir, e));
            UnloadImage(img);
            if (getenv("MOON_AUTOEXIT")) { CloseWindow(); exit(0); }
        }
    }
    if (const char* e = getenv("MOON_EXIT_AFTER")) {
        if (levelTime >= (float)atof(e)) {
            TraceLog(LOG_INFO, "MoonLevel: MOON_EXIT_AFTER reached. Auto-exiting.");
            CloseWindow();
            exit(0);
        }
    }
}

void MoonLevel::update(float dt, float levelTime, Player& player, Weapons& weapons,
                        Particles& fx, Vfx& vfx, Audio& audio, Render3D& r3d, const View& view, float s,
                        std::function<void(int dmg, Vector2 at)> damagePlayerCb,
                        int& score, float& shake) {
    (void)vfx;
    if (!initialized_) init(view);
    realLT_ = levelTime;
    if (trench_ && levelTime < kTrenchStart) {
        // Smooth approach: ease player ship toward the trench entry station
        const float F = view.forward(), Lt = view.lateral();
        const float kx = fminf(1.f, 2.5f * dt);
        player.pos.x = Lerp(player.pos.x, 0.45f * F, kx);
        player.pos.y = Lerp(player.pos.y, 0.55f * Lt, kx);
        if (levelTime >= kTrenchStart - 0.4f) {
            weapons.lasers.clear();
            weapons.missiles.clear();
        }
        // Swell approach engine drone smoothly into the trench handoff
        const float fade = ss(std::max(0.f, kTrenchStart - 3.0f), kTrenchStart, levelTime);
        g_trenchAudio.stamp = GetTime();
        g_trenchAudio.drone = 0.35f * fade;
        g_trenchAudio.roar = 0.15f * fade;
        g_trenchAudio.roarRate = 0.7f;
        g_trenchAudio.musicDuck = 0.15f * fade;
        g_trenchAudio.engineDuck = 0.f;
    }
    if (trench_ && levelTime >= kTrenchStart) {   // t252
        const float tt = levelTime - kTrenchStart;
        if (!(trench_->finaleStarted() && tt >= trench_->finaleTime()))
            trench_->update(dt, tt, player, weapons, audio, view, s, damagePlayerCb, score, shake);
        if (!(trench_->finaleStarted() && tt >= trench_->finaleTime())) { curLevelTime_ = storyTime(levelTime); return; }
        if (finaleLT_ < 0.f) { finaleLT_ = levelTime; fprintf(stderr, "TRENCH finale: moon explosion at lt=%.1f\n", levelTime); }
    }
    levelTime = storyTime(levelTime);
    curLevelTime_ = levelTime;
    if (finaleLT_ >= 0.f) updateFinale(dt, realLT_ - finaleLT_, audio, view);   // t267

    static const bool fpsLog = getenv("MOON_FPS_LOG") != nullptr;
    if (fpsLog) {
        static float acc = 0.f; static int frames = 0; static float worst = 0.f;
        acc += GetFrameTime(); frames++; worst = std::max(worst, GetFrameTime());
        if (acc >= 1.0f) {
            fprintf(stderr, "MOONFPS t=%.1f fps=%.1f worst_ms=%.1f\n", levelTime, frames / acc, worst * 1000.f);
            acc = 0.f; frames = 0; worst = 0.f;
        }
    }

    // --- asteroid fall ---
    const float astR = 1.0f / 8.0f;   // radius in moon radii (diameter = 1/8 of the moon's)
    if (!impacted_ && levelTime >= kImpactTime - kAsteroidLead) {
        if (!astAlive_) {
            astAlive_ = true;
            astSpin_ = {frange(-0.9f, 0.9f), frange(-0.9f, 0.9f), frange(-0.9f, 0.9f)};
        }
        const float left = std::max(0.f, kImpactTime - levelTime);
        const float dist = left * 0.80f;   // moon radii (enters the frame from the upper right)
        astPos_ = Vector3Add(Vector3Scale(impactCam_, 1.0f + astR * 0.6f), Vector3Scale(astInDir_, dist));
        astRot_ = Vector3Add(astRot_, Vector3Scale(astSpin_, dt));
    }
    if (!impacted_ && levelTime >= kImpactTime) triggerImpact(audio, shake);

    // --- after the impact: plume keeps venting while the cracks run ---
    if (impacted_ && !detonated_) {
        const float since = levelTime - kImpactTime;
        const float rate = 30.f * Clamp(1.f - since / 13.f, 0.15f, 1.f);
        ventAcc_ += rate * dt;
        const Vector3 N = impactCam_;
        const Color dustCols[3] = {{176, 162, 142, 255}, {150, 138, 122, 255}, {190, 178, 160, 255}};
        while (ventAcc_ >= 1.f) {
            ventAcc_ -= 1.f;
            const Vector3 dir = coneDir(N, 0.55f);
            emitDust(Vector3Add(N, Vector3Scale(dir, 0.02f)), Vector3Scale(dir, frange(0.08f, 0.45f)),
                     frange(0.02f, 0.05f), frange(0.01f, 0.03f), frange(4.f, 8.f), frange(0.18f, 0.30f), dustCols[rand() % 3], 0.22f);
            if (frand01() < 0.9f) {
                const unsigned char g = (unsigned char)frange(80.f, 125.f);
                emitSpray(N, Vector3Scale(coneDir(N, 0.7f), frange(0.25f, 0.9f)), frange(0.004f, 0.012f), frange(4.f, 9.f), Color{g, (unsigned char)(g - 4), (unsigned char)(g - 9), 255});
            }
            if (frand01() < 0.15f) emitEmber(N, Vector3Scale(coneDir(N, 0.5f), frange(0.15f, 0.6f)), frange(0.006f, 0.016f), frange(0.7f, 1.6f));
        }
        // seismic tremor builds before the breakup
        if (levelTime >= kBreakupTime - 3.f) shake = std::max(shake, 0.15f * (levelTime - (kBreakupTime - 3.f)) / 3.f);
    }
    if (!detonated_ && levelTime >= kBreakupTime) {
        if (!impacted_) triggerImpact(audio, shake);
        triggerBreakup(audio, shake);
    }

    if (flashAlpha_ > 0.001f) flashAlpha_ = std::max(0.0f, flashAlpha_ - dt / 0.35f);
    if (coreGlow_ > 0.001f) coreGlow_ = std::max(0.0f, coreGlow_ - dt / 2.8f);
    if (interiorGlow_ > 0.001f) interiorGlow_ = std::max(0.0f, interiorGlow_ - dt / 14.f);

    if (detonated_) {
        for (auto& sh : shards_) {
            if (!sh.active) continue;
            sh.offset = Vector3Add(sh.offset, Vector3Scale(sh.vel, dt));
            sh.rot = Vector3Add(sh.rot, Vector3Scale(sh.spin, dt));
        }
    }

    // --- moon-space particles: drag, a little of the moon's gravity, growth ---
    auto stepP = [&](MoonParticle& p, float gK) {
        if (!p.alive) return;
        const float r2 = std::max(1.0f, Vector3DotProduct(p.pos, p.pos));
        const Vector3 g = Vector3Scale(Vector3Normalize(p.pos), -gK / r2);
        p.vel = Vector3Add(Vector3Scale(p.vel, std::max(0.f, 1.f - p.drag * dt)), Vector3Scale(g, dt));
        p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
        p.size += p.grow * dt;
        p.life -= dt;
        if (p.life <= 0.f) p.alive = false;
        // fell back onto the intact moon: gone
        if (!detonated_ && !finTorn_ && Vector3DotProduct(p.pos, p.pos) < 0.97f) p.alive = false;
    };
    for (auto& p : dust_) stepP(p, 0.010f);
    for (auto& p : spray_) stepP(p, 0.030f);
    for (auto& p : embers_) stepP(p, 0.030f);
    for (auto& k : rocks_) {
        if (!k.alive) continue;
        k.pos = Vector3Add(k.pos, Vector3Scale(k.vel, dt));
        k.rot = Vector3Add(k.rot, Vector3Scale(k.spin, dt));
        k.hot = std::max(0.f, k.hot - dt * 0.12f);
        if (Vector3Length(k.pos) > 6.f) k.alive = false;
    }

    // Update debris storm & spawning
    if (!trench_) updateDebrisStorm(dt, levelTime, view, s, player, shake);   // t252: no rock storm after the trench run

    // Update gameplay rock chunks physics & collisions
    const float playerColR = player.shield > 0 ? (38.f * s) : (20.f * s);
    const float depthScale = view.pf.height / CameraSystem::kViewHeight;

    for (int i = 0; i < kMaxChunks; i++) {
        RockChunk& c = chunks_[i];
        if (!c.alive) continue;

        c.pos = Vector2Add(c.pos, Vector2Scale(c.vel, dt * player.edgeScroll));
        c.hiddenAxis += c.velZ * dt;
        c.rot = Vector3Add(c.rot, Vector3Scale(c.spin, dt));
        c.age += dt;

        if (c.pos.x < -140.f * s || c.pos.x > view.forward() + 300.f * s ||
            c.pos.y < -140.f * s || c.pos.y > view.lateral() + 140.f * s) {
            c.alive = false;
            continue;
        }

        // Hull collision (only collides if near player's gameplay plane)
        float depthDiff = fabsf(player.hiddenAxis - c.hiddenAxis) * depthScale;
        if (c.age >= kApproachTime * 0.8f && depthDiff < 30.f * s) {   // t247: not while still far out in the cloud
            float dx = player.pos.x - c.pos.x;
            float dy = player.pos.y - c.pos.y;
            float dist2D = sqrtf(dx * dx + dy * dy);
            float rockR = c.radius * s;

            if (dist2D < playerColR + rockR) {
                Vector2 normal = {dx, dy};
                float len = Vector2Length(normal);
                if (len < 0.001f) normal = {-1.0f, 0.0f};
                else normal = Vector2Scale(normal, 1.0f / len);

                float pen = (playerColR + rockR) - dist2D;

                // REAL HULL COLLISION: NO CLIPPING (pushes ship out outside chunk)
                player.pos = Vector2Add(player.pos, Vector2Scale(normal, pen + 0.6f * s));
                player.pos.x = Clamp(player.pos.x, 45.f * s, view.forward() * 0.90f);
                player.pos.y = Clamp(player.pos.y, 35.f * s, view.lateral() - 35.f * s);

                // Big chunks deadly (80 dmg), small only chip (12 dmg)
                int dmg = (c.size == ChunkSize::Large) ? 80 : (c.size == ChunkSize::Medium ? 32 : 12);
                Vector2 contact = Vector2Add(player.pos, Vector2Scale(Vector2Negate(normal), playerColR));

                // Drains shield first
                damagePlayerCb(dmg, contact);

                if (player.shield > 0) {
                    r3d.shieldImpact(player.pos, contact, player.hiddenAxis);
                    audio.shieldDeflect();
                }

                fx.sparks(view.toScreen(contact), view.dirToScreen(normal), 18,
                          c.size == ChunkSize::Large ? Color{255, 170, 50, 255} : SKYBLUE, 340.f * s);

                c.vel = Vector2Add(c.vel, Vector2Scale(Vector2Negate(normal), 60.f * s / c.mass));
                if (c.size == ChunkSize::Small) {
                    destroyChunk(i, fx, audio, score, view, s);
                }
            }
        }
    }


    // Weapon interactions: lasers stop at first rock, damage and break
    weapons.lasers.each([&](Laser& l) {
        if (!l.alive) return;
        for (int i = 0; i < kMaxChunks; i++) {
            RockChunk& c = chunks_[i];
            if (!c.alive || c.age < kApproachTime * 0.8f) continue;
            float ldx = l.pos.x - c.pos.x;
            float ldy = l.pos.y - c.pos.y;
            float rockR = c.radius * s;
            if (ldx * ldx + ldy * ldy < rockR * rockR) {
                l.alive = false;
                c.hp -= l.damage;
                fx.sparks(view.toScreen(l.pos), view.dirToScreen(l.vel), 8, {255, 210, 110, 255}, 260.f * s);
                if (c.hp <= 0) {
                    destroyChunk(i, fx, audio, score, view, s);
                }
                break;
            }
        }
    });

    // Weapon interactions: missiles hit rocks and explode
    weapons.missiles.each([&](Missile& m) {
        if (!m.alive) return;
        for (int i = 0; i < kMaxChunks; i++) {
            RockChunk& c = chunks_[i];
            if (!c.alive || c.age < kApproachTime * 0.8f) continue;
            float mdx = m.pos.x - c.pos.x;
            float mdy = m.pos.y - c.pos.y;
            float rockR = c.radius * s;
            if (mdx * mdx + mdy * mdy < (rockR + 10.f * s) * (rockR + 10.f * s)) {
                m.alive = false;
                c.hp -= 260;
                fx.explosion(view.toScreen(m.pos), 1.3f * s, ORANGE);
                if (c.hp <= 0) {
                    destroyChunk(i, fx, audio, score, view, s);
                }
                break;
            }
        }
    });

    // Weapon interactions: plasma beam intersects closest rock
    if (weapons.beamOn) {
        Vector2 o = weapons.beamFrom;
        Vector2 d = weapons.beamDir;
        float bestT = weapons.beamMax;
        int hitIndex = -1;
        Vector2 hitContact{};

        for (int i = 0; i < kMaxChunks; i++) {
            RockChunk& c = chunks_[i];
            if (!c.alive || c.age < kApproachTime * 0.8f) continue;
            float rockR = c.radius * s;
            Vector2 toC = Vector2Subtract(c.pos, o);
            float proj = Vector2DotProduct(toC, d);
            if (proj < 0.f || proj > bestT + rockR) continue;
            Vector2 closest = Vector2Add(o, Vector2Scale(d, proj));
            float d2 = Vector2DistanceSqr(closest, c.pos);
            if (d2 < rockR * rockR) {
                float halfChord = sqrtf(rockR * rockR - d2);
                float tHit = proj - halfChord;
                if (tHit >= 0.f && tHit < bestT) {
                    bestT = tHit;
                    hitIndex = i;
                    hitContact = Vector2Add(o, Vector2Scale(d, tHit));
                }
            }
        }

        if (hitIndex >= 0) {
            weapons.beamLen = std::min(weapons.beamLen, bestT);
            weapons.beamHit = true;
            weapons.beamHitPos = hitContact;
            weapons.beamHitT = 0.15f;
            weapons.beamAcc += dt * Weapons::kPlasmaDps;
            int dmg = (int)weapons.beamAcc;
            weapons.beamAcc -= dmg;
            if (dmg > 0) {
                chunks_[hitIndex].hp -= dmg;
                fx.embers(view.toScreen(hitContact), 2, 2.2f * s, 80.f * s);
                if (chunks_[hitIndex].hp <= 0) {
                    destroyChunk(hitIndex, fx, audio, score, view, s);
                }
            }
        }
    }

    // Weapon interactions: flamethrower rays stop at rocks
    if (weapons.flameOn) {
        Vector2 o = weapons.flameFrom;
        Vector2 baseDir = weapons.flameDir;
        float baseAngle = atan2f(baseDir.y, baseDir.x);

        for (int r = 0; r < Weapons::kFlameRayCount; r++) {
            float offset = -Weapons::kFlameHalfAngle + (2.f * Weapons::kFlameHalfAngle) * r / (Weapons::kFlameRayCount - 1.f);
            Vector2 d{cosf(baseAngle + offset), sinf(baseAngle + offset)};
            float bestT = weapons.flameRange;
            int hitIndex = -1;
            Vector2 hitContact{};

            for (int i = 0; i < kMaxChunks; i++) {
                RockChunk& c = chunks_[i];
                if (!c.alive || c.age < kApproachTime * 0.8f) continue;
                float rockR = c.radius * s;
                Vector2 toC = Vector2Subtract(c.pos, o);
                float proj = Vector2DotProduct(toC, d);
                if (proj < 0.f || proj > bestT + rockR) continue;
                Vector2 closest = Vector2Add(o, Vector2Scale(d, proj));
                float d2 = Vector2DistanceSqr(closest, c.pos);
                if (d2 < rockR * rockR) {
                    float halfChord = sqrtf(rockR * rockR - d2);
                    float tHit = proj - halfChord;
                    if (tHit >= 0.f && tHit < bestT) {
                        bestT = tHit;
                        hitIndex = i;
                        hitContact = Vector2Add(o, Vector2Scale(d, tHit));
                    }
                }
            }

            if (hitIndex >= 0) {
                weapons.flameStops[r] = std::min(weapons.flameStops[r], bestT);
                chunks_[hitIndex].hp -= 2;
                fx.embers(view.toScreen(hitContact), 1, 1.8f * s, 50.f * s);
                if (chunks_[hitIndex].hp <= 0) {
                    destroyChunk(hitIndex, fx, audio, score, view, s);
                }
            }
        }
    }

}

void MoonLevel::drawSky(Render3D& r3d, const View& view, float levelTime, const Texture2D* glow, float renderScale) const {
    levelTime = storyTime(levelTime);   // t252
    if (!r3d.ok()) return;
    if (hasEntry_) {   // the deep-space floor eases from the Warden's (14,4,6) to this level's (6,4,16)
        const float k = smoother(0.1f, 0.8f, turnE(levelTime));
        if (k < 1.f) DrawRectangle(0, 0, (int)std::ceil((view.pf.width + view.pf.x * 2.f) * renderScale), (int)std::ceil(view.pf.height * renderScale),
                                   Color{(unsigned char)Lerp(14, 6, k), 4, (unsigned char)Lerp(6, 16, k), 255});
    }
    Camera3D c = CameraSystem::makeCamera(view, 0.f);
    const float e = turnE(levelTime);
    Render3D::ApproachScene sc;
    // world frame: final camera C1, the sun S (sunDirection) and the planet are fixed in it; the
    // view turns from the Warden's sun (e = 0) to the moon (e = 1). Sky vectors go through skyMap
    // so the opening frame is exactly the Warden's sky (nebula, stars, sun).
    const Vector3 f1 = Vector3Normalize(Vector3Subtract(c.target, c.position));
    const Vector3 r1 = Vector3Normalize(Vector3CrossProduct(f1, c.up));
    const Vector3 u1 = Vector3CrossProduct(r1, f1);
    const Vector3 S = sunDirection();
    const Vector3 P = Vector3Normalize(Vector3Add(Vector3Scale(f1, 0.82f), Vector3Add(Vector3Scale(r1, -0.38f), Vector3Scale(u1, -0.32f))));
    const Vector3 ft = hasEntry_ ? camToWorld(levelTime, {0, 0, 1}) : f1;
    const Vector3 ut = hasEntry_ ? camToWorld(levelTime, {0, 1, 0}) : c.up;
    Vector3 ftj = ft;
    if (finaleLT_ >= 0.f) { const Vector3 rj = Vector3Normalize(Vector3CrossProduct(ft, ut)); ftj = Vector3Normalize(Vector3Add(ft, Vector3Add(Vector3Scale(rj, finJit_.x), Vector3Scale(ut, finJit_.y)))); }   // t267 shake
    sc.f = skyMap(ftj);
    sc.u = skyMap(ut);
    sc.sun = skyMap(S);
    sc.pdir = skyMap(P);
    sc.fovy = c.fovy;
    // flying away from the star: the disc shrinks from the Warden's size
    sc.sunR = hasEntry_ ? Lerp(entry_.sunR, 3.2f * DEG2RAD, smoother(0.3f, kTurnEnd, levelTime)) : 3.2f * DEG2RAD;
    sc.planet = true;             // Big purple planet lower left in the background
    sc.dist = 2.45f;
    sc.atmoK = 0.65f;
    sc.tint = {210, 150, 240, 255};
    sc.stars = 1.0f;
    const float nk = hasEntry_ ? smoother(0.1f, 0.8f, e) : 1.f;
    sc.nebula = Lerp(0.5f, 0.35f, nk);
    const Color n0 = hasEntry_ ? entry_.nebulaTint : Color{55, 35, 100, 255};
    sc.nebulaTint = {(unsigned char)Lerp(n0.r, 55, nk), (unsigned char)Lerp(n0.g, 35, nk), (unsigned char)Lerp(n0.b, 100, nk), 255};
    sc.starSpeed = 0.002f;
    sc.separateStarFrame = true;
    if (hasEntry_) { sc.starF = starMap(ft); sc.starU = starMap(ut); }
    sc.time = levelTime;
    sc.spin = 0.012f * levelTime;
    if (hasEntry_) {
        sc.time = entry_.time + levelTime;   // the star's surface keeps animating from the Warden's frame
        sc.starSpeed = 0.f;
        if (levelTime < kSkySwitch) {        // the Warden's sun scene exactly (both off-screen at the switch)
            sc.pdir = Vector3Negate(sc.sun);
            sc.dist = 30.f;
            sc.planet = false;
        }
    }
    Render3D::ApproachOut out;
    r3d.drawApproachScenery(sc, out);
    // Sole light source: ONLY the sun illuminates the scene (ships too, no fill / rim). Ships use
    // the fixed side camera, so the light is the sun as seen from the current (turning) view.
    const Vector3 Ls = hasEntry_ ? basisToWorld(r1, u1, f1, worldToCam(levelTime, S)) : S;
    r3d.setSceneLight(true, Vector3Negate(Vector3Normalize(Ls)), Lerp(0.34f, 0.16f, hasEntry_ ? e : 1.f), Lerp(0.16f, 0.12f, hasEntry_ ? e : 1.f));
    if (glow && glow->id) {
        const float W = view.pf.width * renderScale, H = view.pf.height * renderScale, s = view.s;
        const Rectangle gs{0, 0, (float)glow->width, (float)glow->height};
        BeginBlendMode(BLEND_ADDITIVE);
        float discK = 0.f;
        if (out.sunOn) {   // the Warden's soft outer bloom around the disc
            const Vector2 Pp = out.sunDiscPx; const float R = fmaxf(2.f, out.sunRadPx);
            const float g = fminf(R * 9.f, R * 2.6f + 900.f * s);
            discK = Clamp(out.sunVis, 0.f, 1.f);
            DrawTexturePro(*glow, gs, {Pp.x, Pp.y, g, g}, {g / 2, g / 2}, 0, Fade({255, 120, 40, 255}, 0.20f * discK));
        }
        // the star off-frame: its glare still washes in from that edge
        const Vector3 l = Vector3Normalize(worldToCam(levelTime, S));
        Vector2 d{l.x, -l.y};
        const float dl = sqrtf(d.x * d.x + d.y * d.y);
        if (dl > 1e-3f) {
            d = {d.x / dl, d.y / dl};
            const float a = (1.f - discK) * (0.60f + 0.35f * smoother(-0.8f, 0.35f, l.z));
            const float reach = 0.47f * sqrtf(W * W + H * H);
            const Vector2 Pp{W * 0.5f + d.x * reach, H * 0.5f + d.y * reach};
            const float g = 2.6f * H;
            DrawTexturePro(*glow, gs, {Pp.x, Pp.y, g, g}, {g / 2, g / 2}, 0, Fade({255, 170, 110, 255}, a));
            DrawTexturePro(*glow, gs, {Pp.x, Pp.y, g * 0.45f, g * 0.45f}, {g * 0.225f, g * 0.225f}, 0, Fade({255, 236, 210, 255}, a * 0.8f));
        }
        EndBlendMode();
    }
}

void MoonLevel::draw3D(Render3D& r3d, const View& view, float levelTime) {
    levelTime = storyTime(levelTime);   // t252
    (void)r3d;
    if (!initialized_ || !matsReady_) return;
    const Camera3D cb = sceneCamera(view, levelTime);          // placement (moon layout, playfield)
    const Camera3D c = turnCamera(cb, levelTime);              // what we look through (t248 opening turn)
    const Vector3 camF = Vector3Normalize(Vector3Subtract(c.target, c.position));
    const Vector3 camR = Vector3Normalize(Vector3CrossProduct(camF, c.up));
    const Vector3 camU = Vector3CrossProduct(camR, camF);
    const Vector3 L = sunDirection();
    const Vector3 lightDir = Vector3Negate(L);
    const Vector3 lightCam = {Vector3DotProduct(L, camR), Vector3DotProduct(L, camU), -Vector3DotProduct(L, camF)};

    Vector3 centre; float R;
    moonFrame(view, cb, levelTime, centre, R);
    const float clearFade = (levelTime >= 170.0f) ? Clamp(1.0f - (levelTime - 170.0f) / 10.0f, 0.0f, 1.0f) : 1.0f;
    const float s = view.s;

    BeginMode3D(c);

    // ---- moon uniforms ----
    if (moonShaderLoaded_) {
        const float impactAge = impacted_ ? levelTime - kImpactTime : -1.f;
        float crackProg = 0.f, widen = 0.35f, heat = 1.f;
        if (impacted_) {
            const float p = Clamp((levelTime - kImpactTime) / (kBreakupTime - kImpactTime), 0.f, 1.f);
            crackProg = 0.06f + 0.98f * (1.f - powf(1.f - p, 1.7f));
            widen = 0.40f + 0.60f * p;
        }
        if (detonated_) {
            const float since = levelTime - kBreakupTime;
            crackProg = 1.1f;
            widen = 1.0f + 0.5f * ss(0.f, 8.f, since);
            heat = std::max(0.22f, 1.f - since / 45.f);
        }
        float wobble = (!detonated_ && levelTime >= kBreakupTime - 3.f) ? (levelTime - (kBreakupTime - 3.f)) / 3.f : 0.f;
        float impactAgeF = impactAge;
        if (finaleLT_ >= 0.f) {   // t267: the finale's cracks, quake and heat (no crater)
            impactAgeF = -1.f; crackProg = finCrack_; widen = 0.35f + 1.05f * finCrack_ / 1.1f; heat = finTorn_ ? finHeat_ : 1.f; wobble = finWobble_;
        }
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "lightDir"), &lightDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "camPos"), &c.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "impactDir"), &impactObj_, SHADER_UNIFORM_VEC3);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "impactAge"), &impactAgeF, SHADER_UNIFORM_FLOAT);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "crackProgress"), &crackProg, SHADER_UNIFORM_FLOAT);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "widen"), &widen, SHADER_UNIFORM_FLOAT);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "heat"), &heat, SHADER_UNIFORM_FLOAT);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "time"), &levelTime, SHADER_UNIFORM_FLOAT);
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "wobble"), &wobble, SHADER_UNIFORM_FLOAT);
        static const float dbgV = getenv("MOON_DBG") ? (float)atof(getenv("MOON_DBG")) : 0.f;
        SetShaderValue(moonShader_, GetShaderLocation(moonShader_, "dbg"), &dbgV, SHADER_UNIFORM_FLOAT);
    }
    if (rockShaderLoaded_) {
        SetShaderValue(rockShader_, GetShaderLocation(rockShader_, "lightDir"), &lightDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(rockShader_, GetShaderLocation(rockShader_, "camPos"), &c.position, SHADER_UNIFORM_VEC3);
    }
    const Matrix moonRot = moonRotation(levelTime);
    const int locForce = GetShaderLocation(moonShader_, "forceOuter");

    // 1. Whole moon until the breakup
    if (!detonated_ && !finTorn_ && wholeMoonLoaded_) {   // t267: gone at the tear
        const Matrix M = MatrixMultiply(MatrixMultiply(moonRot, MatrixScale(R, R, R)), MatrixTranslate(centre.x, centre.y, centre.z));
        const float one = 1.f;
        if (moonShaderLoaded_) SetShaderValue(moonShader_, locForce, &one, SHADER_UNIFORM_FLOAT);
        moonMat_.maps[MATERIAL_MAP_DIFFUSE].color = Fade(WHITE, clearFade);
        for (int i = 0; i < wholeMoonModel_.meshCount; i++) DrawMesh(wholeMoonModel_.meshes[i], moonMat_, M);
    }

    // 2. Breakup: plates drift apart (they stay far behind the playfield)
    if (detonated_) {
        const float zero = 0.f;
        if (moonShaderLoaded_) SetShaderValue(moonShader_, locForce, &zero, SHADER_UNIFORM_FLOAT);
        for (int i = 0; i < kNumShards; i++) {
            MoonShard& sh = shards_[i];
            if (!sh.loaded || !sh.active) continue;
            const Vector3 pos = Vector3Add(centre, Vector3Scale(sh.offset, R));
            const float camDist = Vector3DotProduct(Vector3Subtract(pos, c.position), camF);
            const float nearFade = Clamp((camDist - 32.f) / 12.f, 0.f, 1.f);
            if (nearFade <= 0.f) continue;
            const Vector3 pivot = Vector3Scale(sh.restCentroid, 0.85f);
            Matrix M = MatrixTranslate(-pivot.x, -pivot.y, -pivot.z);
            M = MatrixMultiply(M, MatrixRotateXYZ(sh.rot));
            M = MatrixMultiply(M, MatrixTranslate(pivot.x, pivot.y, pivot.z));
            M = MatrixMultiply(M, moonRot);
            M = MatrixMultiply(M, MatrixScale(R, R, R));
            M = MatrixMultiply(M, MatrixTranslate(pos.x, pos.y, pos.z));
            moonMat_.maps[MATERIAL_MAP_DIFFUSE].color = Fade(WHITE, clearFade * nearFade);
            DrawMesh(sh.model.meshes[0], moonMat_, M);
        }
        // the hot interior, shrinking as the field disperses
        const float since = levelTime - kBreakupTime;
        const float ck = 1.f - 0.75f * ss(0.f, 60.f, since);
        if (coreLoaded_ && ck > 0.05f) {
            const float k = R * ck;
            const Matrix M = MatrixMultiply(MatrixMultiply(moonRot, MatrixScale(k, k, k)), MatrixTranslate(centre.x, centre.y, centre.z));
            moonMat_.maps[MATERIAL_MAP_DIFFUSE].color = Fade(WHITE, clearFade);
            DrawMesh(coreModel_.meshes[0], moonMat_, M);
        }
    }

    // t267: the finale: the crust plates fly apart tumbling, burning and crumbling to nothing
    if (finTorn_) {
        const float zero = 0.f;
        if (moonShaderLoaded_) SetShaderValue(moonShader_, locForce, &zero, SHADER_UNIFORM_FLOAT);
        for (int i = 0; i < kNumShards; i++) {
            MoonShard& sh = shards_[i];
            const float k = finShardK_[i];
            if (!sh.loaded || k < 0.02f) continue;
            const Vector3 pos = Vector3Add(centre, Vector3Scale(sh.offset, R));
            const float camDist = Vector3DotProduct(Vector3Subtract(pos, c.position), camF);
            const float nearFade = Clamp((camDist - 32.f) / 12.f, 0.f, 1.f);
            if (nearFade <= 0.f) continue;
            const Vector3 pv = sh.restCentroid;
            Matrix M = MatrixTranslate(-pv.x, -pv.y, -pv.z);
            M = MatrixMultiply(M, MatrixScale(k, k, k));
            M = MatrixMultiply(M, MatrixRotateXYZ(sh.rot));
            M = MatrixMultiply(M, MatrixTranslate(pv.x, pv.y, pv.z));
            M = MatrixMultiply(M, moonRot);
            M = MatrixMultiply(M, MatrixScale(R, R, R));
            M = MatrixMultiply(M, MatrixTranslate(pos.x, pos.y, pos.z));
            moonMat_.maps[MATERIAL_MAP_DIFFUSE].color = Fade(WHITE, nearFade);
            DrawMesh(sh.model.meshes[0], moonMat_, M);
        }
    }

    // 3. Asteroid, asteroid fragments and boulders (dark / grey sun-lit rocks)
    const int locAlb = GetShaderLocation(rockShader_, "albedoK");
    const int locHeat = GetShaderLocation(rockShader_, "rockHeat");
    auto drawRock = [&](int mesh, Vector3 pos, float r, Vector3 rot, float albedo, float hot, float alpha) {
        if (!rocksLoaded_) return;
        const Matrix M = MatrixMultiply(MatrixMultiply(MatrixRotateXYZ(rot), MatrixScale(r, r, r)), MatrixTranslate(pos.x, pos.y, pos.z));
        if (rockShaderLoaded_) {
            SetShaderValue(rockShader_, locAlb, &albedo, SHADER_UNIFORM_FLOAT);
            SetShaderValue(rockShader_, locHeat, &hot, SHADER_UNIFORM_FLOAT);
        }
        rockMat_.maps[MATERIAL_MAP_DIFFUSE].color = Fade(WHITE, alpha);
        DrawMesh(rockModels_[mesh % kNumRockVariants].meshes[0], rockMat_, M);
    };
    if (astAlive_) {
        // irregular: squash the rock mesh a little
        const Vector3 p = Vector3Add(centre, Vector3Scale(astPos_, R));
        drawRock(4, p, R / 8.f, astRot_, 0.16f, 0.f, 1.f);
    }
    for (const auto& k : rocks_) {
        if (!k.alive) continue;
        drawRock(k.mesh, Vector3Add(centre, Vector3Scale(k.pos, R)), k.r * R, k.rot, k.albedo, k.hot, clearFade);
    }

    // 4. Gameplay debris chunks (sun-lit 3D rocks). Fresh ones fly out of the cloud from far behind.
    if (rocksLoaded_) {
        const float wpl = CameraSystem::worldPerLocal(view);
        for (const auto& chunk : chunks_) {
            if (!chunk.alive) continue;
            const float worldR = chunk.radius * s * wpl * 1.06f;
            const float out = ss(0.f, kApproachTime, chunk.age);
            const float depthW = chunk.hiddenAxis * wpl - (1.f - out) * kMoonBehind * 0.85f;
            const Vector3 worldPos = deepWorld(view, cb, chunk.pos, depthW);
            drawRock(chunk.meshIndex, worldPos, worldR, chunk.rot, 0.30f, chunk.hot, clearFade);
        }
    }

    // 5. Interior glow seen through the gaps right after the breakup (soft additive, depth tested)
    if (fireballShaderLoaded_ && softDotLoaded_ && (interiorGlow_ > 0.01f || coreGlow_ > 0.01f)) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        BeginShaderMode(fireballShader_);
        SetShaderValue(fireballShader_, GetShaderLocation(fireballShader_, "time"), &levelTime, SHADER_UNIFORM_FLOAT);
        if (interiorGlow_ > 0.01f)
            DrawBillboard(c, softDotTexture_, centre, R * 2.2f, Fade(Color{255, 120, 30, 255}, interiorGlow_ * 0.9f * clearFade));
        if (coreGlow_ > 0.01f) {
            // soft additive burst at the crater (round, no square pixels)
            const Vector3 site = Vector3Add(centre, Vector3Scale(impactCam_, R * 1.03f));
            const float g = coreGlow_;
            DrawBillboard(c, softDotTexture_, site, R * (0.45f + 0.75f * (1.f - g)), Fade(Color{255, 170, 70, 255}, g * g));
        }
        EndShaderMode();
        EndBlendMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }

    // 6. Dust plume (soft, sun-lit) then rock spray (hard round bits)
    if (dustShaderLoaded_ && softDotLoaded_) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginShaderMode(dustShader_);
        SetShaderValue(dustShader_, GetShaderLocation(dustShader_, "lightCam"), &lightCam, SHADER_UNIFORM_VEC3);
        float hard = 0.f;
        SetShaderValue(dustShader_, GetShaderLocation(dustShader_, "hard"), &hard, SHADER_UNIFORM_FLOAT);
        for (const auto& d : dust_) {
            if (!d.alive) continue;
            const float age = d.maxLife - d.life;
            const float a = d.alpha * Clamp(age / 0.35f, 0.f, 1.f) * Clamp(d.life / (d.maxLife * 0.5f), 0.f, 1.f) * clearFade;
            Color col = d.tint; col.a = (unsigned char)(Clamp(a, 0.f, 1.f) * 255.f);
            DrawBillboard(c, softDotTexture_, Vector3Add(centre, Vector3Scale(d.pos, R)), d.size * R * 2.f, col);
        }
        rlDrawRenderBatchActive();
        hard = 1.f;
        SetShaderValue(dustShader_, GetShaderLocation(dustShader_, "hard"), &hard, SHADER_UNIFORM_FLOAT);
        for (const auto& d : spray_) {
            if (!d.alive) continue;
            const float a = Clamp(d.life / 1.5f, 0.f, 1.f) * clearFade;
            Color col = d.tint; col.a = (unsigned char)(a * 255.f);
            DrawBillboard(c, softDotTexture_, Vector3Add(centre, Vector3Scale(d.pos, R)), d.size * R * 2.f, col);
        }
        EndShaderMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }

    // 7. Molten droplets: additive, cooling yellow -> orange -> dark red
    if (softDotLoaded_) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        for (const auto& e : embers_) {
            if (!e.alive) continue;
            const float prog = 1.0f - (e.life / e.maxLife);
            Color col;
            if (prog < 0.35f) col = Color{255, (unsigned char)(225 - (int)(90.f * prog / 0.35f)), 90, 255};
            else if (prog < 0.70f) col = Color{255, (unsigned char)(135 - (int)(100.f * (prog - 0.35f) / 0.35f)), 25, 255};
            else col = Color{(unsigned char)(255 - (int)(120.f * (prog - 0.70f) / 0.30f)), 30, 10, (unsigned char)(255 * (1.0f - (prog - 0.70f) / 0.30f))};
            col.a = (unsigned char)(col.a * clearFade);
            DrawBillboard(c, softDotTexture_, Vector3Add(centre, Vector3Scale(e.pos, R)), e.size * R * 2.f, col);
        }
        EndBlendMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }
    if (finaleLT_ >= 0.f) drawFinale3D(c, centre, R, realLT_ - finaleLT_);   // t267

    EndMode3D();
}

void MoonLevel::draw2D(const View& view, float s) const {
    (void)view; (void)s;
    // Warm flash: capped, fades in ~0.35 s (never white-out, never black)
    if (flashAlpha_ > 0.001f) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(Color{255, 185, 110, 255}, flashAlpha_));
        EndBlendMode();
    }
    const_cast<MoonLevel*>(this)->checkScreenshots(curLevelTime_);
}

// ------------------------------------------------------------------ t267: the moon is destroyed completely
// Build (rumble, quakes, cracks running and glowing, vents, the core breaching) .. a held breath .. then it tears
// apart: the crust plates fly outward tumbling and burning and crumble to rocks, rocks / glowing fragments / fire
// and dust expand past the camera, shockwave rings. It ends in empty space with drifting debris. Warm bounded
// flashes only (no white-out). Sound: a synthesized deep rumble and sub-bass boom (+ the game's boss boom).
namespace {
float finSs(float a, float b, float x) { float t = Clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }
float finRand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 100000) / 100000.f; }
Vector3 finUnit() { for (;;) { Vector3 v{finRand(-1, 1), finRand(-1, 1), finRand(-1, 1)}; const float l = Vector3Length(v); if (l > 0.05f && l <= 1.f) return Vector3Scale(v, 1.f / l); } }
Sound finSynth(int n, const std::function<float(int, float)>& f) {
    Wave w{}; w.frameCount = (unsigned)n; w.sampleRate = 44100; w.sampleSize = 16; w.channels = 1;
    std::vector<float> buf(n); float peak = 1e-4f;
    for (int i = 0; i < n; i++) { buf[i] = f(i, i / 44100.f); peak = std::max(peak, fabsf(buf[i])); }
    short* d = (short*)RL_MALLOC(n * sizeof(short));
    for (int i = 0; i < n; i++) d[i] = (short)(Clamp(buf[i] / peak * 0.95f, -1.f, 1.f) * 32000.f);
    w.data = d;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}
}  // namespace

void MoonLevel::emitFinFire(Vector3 pos, Vector3 vel, float size, float grow, float life, bool core) {
    for (auto& f : finFire_) if (!f.alive) { f = FinFire{true, pos, vel, size, grow, life, life, core}; return; }
}
void MoonLevel::emitFinRock(Vector3 pos, Vector3 vel, float r, float hot) {
    for (auto& k : finRocks_) if (!k.alive) {
        k = FinRock{}; k.alive = true; k.pos = pos; k.vel = vel; k.r = r; k.hot = hot; k.albedo = finRand(0.34f, 0.52f);
        k.rot = {finRand(0, 6.28f), finRand(0, 6.28f), finRand(0, 6.28f)};
        k.spin = {finRand(-2.2f, 2.2f), finRand(-2.2f, 2.2f), finRand(-2.2f, 2.2f)};
        k.mesh = GetRandomValue(0, kNumRockVariants - 1);
        return;
    }
}

bool MoonLevel::takeTrenchBlastDeath() {
    if (!trench_ || !trench_->blastDeathReady()) return false;
    trench_->takeBlastDeath();
    return true;
}

void MoonLevel::updateFinale(float dt, float since, Audio& audio, const View& view) {
    const Camera3D cb = sceneCamera(view, kFinaleStory);
    const Camera3D c = turnCamera(cb, kFinaleStory);
    Vector3 centre; float R;
    moonFrame(view, cb, kFinaleStory, centre, R);
    const Vector3 camBack = Vector3Normalize(Vector3Subtract(c.position, centre));
    const float dc = Vector3Distance(c.position, centre) / std::max(R, 1e-3f);   // camera distance in moon radii
    if (!finInit_) {
        finInit_ = true;
        finShardK_.fill(1.f);
        if (IsAudioDeviceReady()) {
            // deep rumble (4.2 s): brown noise + 31 / 46 Hz, swelling, quaking
            float b1 = 0.f, b2 = 0.f; unsigned rs = 12345u;
            finRumble_ = finSynth((int)(44100 * 4.2f), [&](int, float t) {
                rs = rs * 1664525u + 1013904223u; const float wn = ((rs >> 9) & 0xFFFF) / 32768.f - 1.f;
                b1 = 0.985f * b1 + 0.015f * wn; b2 += (b1 - b2) * 0.02f;
                const float env = powf(Clamp(t / 4.1f, 0.f, 1.f), 1.3f) * (0.72f + 0.28f * sinf(6.2831853f * 5.3f * t)) * (1.f - finSs(4.12f, 4.2f, t));
                return env * (b2 * 9.f + 0.55f * sinf(6.2831853f * 31.f * t) + 0.3f * sinf(6.2831853f * 46.f * t + 1.f));
            });
            // sub-bass boom (7.5 s): a falling 80 -> 26 Hz punch, a low crack, a long rolling tail
            float ph = 0.f, c1 = 0.f, c2 = 0.f, tl1 = 0.f, tl2 = 0.f; unsigned rs2 = 987654u;
            finBoom_ = finSynth((int)(44100 * 7.5f), [&](int, float t) {
                rs2 = rs2 * 1664525u + 1013904223u; const float wn = ((rs2 >> 9) & 0xFFFF) / 32768.f - 1.f;
                const float f = 26.f + 55.f * expf(-t * 2.2f);
                ph += 6.2831853f * f / 44100.f;
                const float att = Clamp(t / 0.015f, 0.f, 1.f);
                const float sub = sinf(ph) * expf(-t * 0.42f);
                const float k = 0.02f + 0.25f * expf(-t * 3.f);
                c1 += (wn - c1) * k; c2 += (c1 - c2) * k;
                const float crack = c2 * 3.5f * expf(-t * 1.6f);
                tl1 = 0.99f * tl1 + 0.01f * wn; tl2 += (tl1 - tl2) * 0.02f;
                const float tail = tl2 * 20.f * expf(-t * 0.35f) * Clamp(t / 0.3f, 0.f, 1.f);
                return att * tanhf(1.6f * (sub + crack + tail));
            });
            finSnd_ = finRumble_.frameCount > 0 && finBoom_.frameCount > 0;
        }
        fprintf(stderr, "FINALE start: build %.1f s, tear at %.1f s, moon R %.1f, camera %.1f R away, sound %d\n", kFinPause, kFinTear, R, dc, finSnd_ ? 1 : 0);
    }
    const float b = finSs(0.2f, kFinPause, since);           // the build
    const float x = since - kFinTear;                           // since the tear
    // cracks run and widen, the crust quakes, the breach glows
    finCrack_ = 1.1f * finSs(0.3f, 3.3f, since);
    finWobble_ = finTorn_ ? 0.f : (since < kFinPause ? finSs(1.2f, kFinPause, since) : 0.25f);
    finGlow_ = since < kFinPause ? finSs(0.8f, kFinPause, since) : 1.f;
    if (finTorn_) finHeat_ = std::max(0.22f, 1.6f * expf(-x * 0.22f));
    // camera shake (angular): rising through the build, a held breath, the blast
    float amp = 0.f;
    if (since < kFinPause) amp = 0.12f + 0.75f * b;
    else if (since < kFinTear) amp = 0.06f;
    else amp = 1.7f * expf(-x * 0.42f) + 0.08f;
    finShake_ = amp;
    finJit_ = {amp * 0.0055f * (sinf(since * 37.f) + 0.6f * sinf(since * 59.f + 1.f)), amp * 0.0055f * (cosf(since * 41.f) + 0.5f * sinf(since * 67.f + 0.4f))};
    // sound + the music dipping under it
    if (finSnd_ && !finRumbling_ && since > 0.1f && since < kFinPause) { SetSoundVolume(finRumble_, 0.9f); PlaySound(finRumble_); finRumbling_ = true; }
    if (finSnd_ && finRumbling_ && since >= kFinPause) { StopSound(finRumble_); finRumbling_ = false; }   // the held breath: silence
    g_trenchAudio.stamp = GetTime();
    g_trenchAudio.drone = 0.f;
    g_trenchAudio.roar = since < kFinPause ? 0.4f * b : (x > 0.f ? 0.55f * expf(-x * 0.3f) : 0.f);   // a low engine-room roar under the rumble
    g_trenchAudio.roarRate = 0.45f;
    g_trenchAudio.musicDuck = since < kFinPause ? 0.4f + 0.6f * b : (x < 4.f ? 1.f : std::max(0.3f, 1.f - (x - 4.f) / 6.f));
    g_trenchAudio.engineDuck = x > 0.f ? 0.8f * expf(-x * 0.3f) : 0.3f * b;
    // vents: fire and molten spray out of the cracks, faster as it builds
    if (!finTorn_) {
        finVentAcc_ += dt * (since < kFinPause ? 6.f + 80.f * b : 4.f);
        while (finVentAcc_ >= 1.f) {
            finVentAcc_ -= 1.f;
            Vector3 d = finUnit(); if (Vector3DotProduct(d, camBack) < -0.1f) d = Vector3Negate(d);
            emitFinFire(d, Vector3Scale(d, finRand(0.05f, 0.25f) * (0.5f + b)), finRand(0.03f, 0.07f), finRand(0.03f, 0.08f), finRand(0.7f, 1.2f));
            if (GetRandomValue(0, 9) < 4) emitEmber(d, Vector3Scale(d, finRand(0.1f, 0.45f)), finRand(0.006f, 0.016f), finRand(0.8f, 1.8f));
        }
    }
    // the tear
    if (!finTorn_ && since >= kFinTear) {
        finTorn_ = true;
        flashAlpha_ = 0.42f;   // warm, capped: never a white-out
        const Matrix rot = moonRotation(kFinaleStory);
        for (int i = 0; i < kNumShards; i++) {
            MoonShard& sh = shards_[i];
            const Vector3 cw = Vector3Normalize(Vector3Transform(sh.restCentroid, rot));
            Vector3 v = Vector3Add(Vector3Scale(cw, finRand(0.35f, 0.95f)), Vector3Scale(finUnit(), 0.08f));
            const float vc = Vector3DotProduct(v, camBack);
            if (vc > 0.f) v = Vector3Subtract(v, Vector3Scale(camBack, vc * 0.5f));   // the plates stay out of the lens
            sh.vel = v; sh.offset = {0, 0, 0}; sh.rot = {0, 0, 0}; sh.active = true;
            sh.spin = {finRand(-0.9f, 0.9f), finRand(-0.9f, 0.9f), finRand(-0.9f, 0.9f)};
        }
        for (int i = 0; i < 6; i++) emitFinFire(Vector3Scale(finUnit(), 0.15f), Vector3Scale(finUnit(), 0.05f), finRand(0.8f, 1.3f), finRand(0.55f, 0.9f), finRand(2.4f, 3.2f), true);
        for (int i = 0; i < 150; i++) { const Vector3 d = finUnit(); emitFinFire(Vector3Scale(d, finRand(0.3f, 0.9f)), Vector3Scale(d, finRand(0.4f, 1.4f)), finRand(0.12f, 0.3f), finRand(0.15f, 0.35f), finRand(1.5f, 3.2f)); }
        for (int i = 0; i < 260; i++) { const Vector3 d = finUnit(); emitEmber(Vector3Scale(d, finRand(0.3f, 1.f)), Vector3Scale(d, finRand(0.5f, 2.2f)), finRand(0.012f, 0.035f), finRand(2.f, 6.f)); }
        for (int i = 0; i < 220; i++) { const Vector3 d = finUnit(); const unsigned char g = (unsigned char)finRand(80.f, 140.f);
                                        emitSpray(Vector3Scale(d, finRand(0.6f, 1.f)), Vector3Scale(d, finRand(0.6f, 2.4f)), finRand(0.01f, 0.03f), finRand(6.f, 12.f), Color{g, g, (unsigned char)(g - 8), 255}); }
        const Color dustCols[2] = {{140, 130, 118, 255}, {118, 110, 100, 255}};
        for (int i = 0; i < 200; i++) { const Vector3 d = finUnit();
                                        emitDust(Vector3Scale(d, finRand(0.5f, 1.f)), Vector3Scale(d, finRand(0.2f, 1.1f)), finRand(0.08f, 0.2f), finRand(0.05f, 0.15f), finRand(8.f, 14.f), finRand(0.16f, 0.3f), dustCols[i % 2], 0.05f); }
        for (int i = 0; i < 30; i++) {   // the dust cloud rolls out past the camera
            const Vector3 d = Vector3Normalize(Vector3Add(camBack, Vector3Scale(finUnit(), 0.35f)));
            emitDust(Vector3Scale(d, 0.9f), Vector3Scale(d, dc / finRand(3.f, 6.f)), 0.25f, 0.35f, 9.f, 0.12f, dustCols[i % 2], 0.f);
        }
        for (int i = 0; i < 110; i++) { const Vector3 d = finUnit(); emitFinRock(Vector3Scale(d, finRand(0.4f, 0.95f)), Vector3Scale(d, finRand(0.4f, 1.6f)), finRand(0.025f, 0.09f), 1.f); }
        for (int i = 0; i < 12; i++) {   // rocks that come straight at you and fly past
            const Vector3 d = Vector3Normalize(Vector3Add(camBack, Vector3Scale(finUnit(), 0.12f)));
            emitFinRock(Vector3Scale(d, 0.9f), Vector3Scale(d, dc / finRand(2.5f, 5.5f)), finRand(0.05f, 0.12f), 1.f);
        }
        if (finSnd_) { SetSoundVolume(finBoom_, 1.f); PlaySound(finBoom_); }
        audio.play(Sfx::BossBoom, 1.f, 0.f);
        audio.play(Sfx::BigExplosion, 0.9f, 0.f);
        fprintf(stderr, "FINALE tear: moon destroyed at %.2f s (shards %d, rocks / fire / embers launched)\n", since, kNumShards);
    }
    if (finTorn_) {
        int left = 0;
        for (int i = 0; i < kNumShards; i++) {
            MoonShard& sh = shards_[i];
            sh.offset = Vector3Add(sh.offset, Vector3Scale(sh.vel, dt));
            sh.rot = Vector3Add(sh.rot, Vector3Scale(sh.spin, dt));
            const float k0 = finShardK_[i];
            finShardK_[i] = 1.f - finSs(kFinTear + 2.5f + 0.45f * (i % 7), kFinTear + 7.5f + 0.6f * (i % 5), since);   // crumbling away
            if (finShardK_[i] > 0.02f) left++;
            const Vector3 cw = Vector3Normalize(Vector3Transform(sh.restCentroid, moonRotation(kFinaleStory)));
            const Vector3 p = Vector3Add(sh.offset, Vector3Scale(cw, 0.85f));
            if (finShardK_[i] < k0 && finShardK_[i] > 0.02f && GetRandomValue(0, 99) < 22) {   // it breaks into rocks and dust
                emitFinRock(Vector3Add(p, Vector3Scale(finUnit(), 0.12f)), Vector3Add(sh.vel, Vector3Scale(finUnit(), 0.25f)), finRand(0.02f, 0.06f), finHeat_ * 0.6f);
                emitDust(p, Vector3Add(Vector3Scale(sh.vel, 0.8f), Vector3Scale(finUnit(), 0.08f)), finRand(0.06f, 0.12f), 0.06f, finRand(4.f, 8.f), 0.18f, Color{130, 122, 110, 255}, 0.05f);
            }
            if (finShardK_[i] > 0.05f && x < 6.f && GetRandomValue(0, 99) < 9) emitFinFire(p, Vector3Scale(sh.vel, 0.7f), finRand(0.08f, 0.16f) * finShardK_[i], 0.12f, finRand(0.8f, 1.4f));   // burning trail
        }
        if (since > finLogT_) {
            finLogT_ = since + 1.f;
            int rocks = 0, fire = 0, emb = 0; for (auto& k : finRocks_) rocks += k.alive; for (auto& f : finFire_) fire += f.alive; for (auto& e : embers_) emb += e.alive;
            fprintf(stderr, "FINALE t=%.1f moonWhole=0 shardsLeft=%d rocks=%d fire=%d embers=%d shake=%.2f\n", since, left, rocks, fire, emb, finShake_);
        }
    } else if (since > finLogT_) {
        finLogT_ = since + 1.f;
        fprintf(stderr, "FINALE t=%.1f build=%.2f cracks=%.2f glow=%.2f shake=%.2f moonWhole=1\n", since, b, finCrack_, finGlow_, finShake_);
    }
    for (auto& f : finFire_) {
        if (!f.alive) continue;
        f.life -= dt; if (f.life <= 0.f) { f.alive = false; continue; }
        f.vel = Vector3Scale(f.vel, std::max(0.f, 1.f - 0.15f * dt));
        f.pos = Vector3Add(f.pos, Vector3Scale(f.vel, dt)); f.size += f.grow * dt;
    }
    for (auto& k : finRocks_) {
        if (!k.alive) continue;
        k.pos = Vector3Add(k.pos, Vector3Scale(k.vel, dt)); k.rot = Vector3Add(k.rot, Vector3Scale(k.spin, dt));
        k.hot = std::max(0.f, k.hot - dt * 0.15f);
        if (since > kFinTear + 8.5f) { k.r *= expf(-1.3f * dt); if (k.r < 0.003f) { k.alive = false; continue; } }   // t267: the last dust thins out to nothing
        const Vector3 toCam = Vector3Subtract(Vector3Scale(camBack, dc), k.pos);
        if (Vector3Length(k.pos) > 40.f || (Vector3DotProduct(toCam, camBack) < -2.f)) k.alive = false;   // gone far away / behind the camera
    }
}

void MoonLevel::drawRing(Vector3 centre, Vector3 e1, Vector3 e2, float r0, float w, float a, Color mid) const {
    const int N = 128;
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < N; i++) {
        const float a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
        const Vector3 d0 = Vector3Add(Vector3Scale(e1, cosf(a0)), Vector3Scale(e2, sinf(a0)));
        const Vector3 d1 = Vector3Add(Vector3Scale(e1, cosf(a1)), Vector3Scale(e2, sinf(a1)));
        const float rr[3] = {r0 - w, r0, r0 + w * 0.6f};
        const float al[3] = {0.f, a, 0.f};
        for (int b = 0; b < 2; b++) {
            const Vector3 p00 = Vector3Add(centre, Vector3Scale(d0, rr[b])), p01 = Vector3Add(centre, Vector3Scale(d1, rr[b]));
            const Vector3 p10 = Vector3Add(centre, Vector3Scale(d0, rr[b + 1])), p11 = Vector3Add(centre, Vector3Scale(d1, rr[b + 1]));
            const unsigned char A0 = (unsigned char)(255 * Clamp(al[b], 0.f, 1.f)), A1 = (unsigned char)(255 * Clamp(al[b + 1], 0.f, 1.f));
            auto V = [&](Vector3 p, unsigned char A) { rlColor4ub((unsigned char)(mid.r * A / 255), (unsigned char)(mid.g * A / 255), (unsigned char)(mid.b * A / 255), 255); rlVertex3f(p.x, p.y, p.z); };
            V(p00, A0); V(p10, A1); V(p11, A1);
            V(p00, A0); V(p11, A1); V(p01, A0);
            V(p00, A0); V(p11, A1); V(p10, A1);   // both faces
            V(p00, A0); V(p01, A0); V(p11, A1);
        }
    }
    rlEnd();
}

void MoonLevel::drawFinale3D(const Camera3D& c, Vector3 centre, float R, float since) {
    const float x = since - kFinTear;
    const Vector3 camF = Vector3Normalize(Vector3Subtract(c.target, c.position));
    // rocks (sun-lit, the fresh ones glowing hot)
    const int locAlb = GetShaderLocation(rockShader_, "albedoK"), locHeat = GetShaderLocation(rockShader_, "rockHeat");
    if (rocksLoaded_) for (const auto& k : finRocks_) {
        if (!k.alive) continue;
        const Vector3 p = Vector3Add(centre, Vector3Scale(k.pos, R));
        const float r = k.r * R;
        const Matrix M = MatrixMultiply(MatrixMultiply(MatrixRotateXYZ(k.rot), MatrixScale(r, r, r)), MatrixTranslate(p.x, p.y, p.z));
        const float hot = k.hot * 3.f;
        if (rockShaderLoaded_) { SetShaderValue(rockShader_, locAlb, &k.albedo, SHADER_UNIFORM_FLOAT); SetShaderValue(rockShader_, locHeat, &hot, SHADER_UNIFORM_FLOAT); }
        rockMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        DrawMesh(rockModels_[k.mesh % kNumRockVariants].meshes[0], rockMat_, M);
    }
    if (!softDotLoaded_) return;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    if (fireballShaderLoaded_) { BeginShaderMode(fireballShader_); SetShaderValue(fireballShader_, GetShaderLocation(fireballShader_, "time"), &since, SHADER_UNIFORM_FLOAT); }
    if (!finTorn_) {   // the core breaching: the glow behind the rim, the hot spot at the breach growing, pinching in the held breath
        const float g = finGlow_;
        const bool pause = since >= kFinPause;
        DrawBillboard(c, softDotTexture_, centre, R * 2.35f, Fade(Color{255, 110, 30, 255}, (pause ? 0.85f : 0.55f) * g));
        const Vector3 site = Vector3Add(centre, Vector3Scale(impactCam_, R * 1.02f));
        const float sz = pause ? R * (0.55f - 0.35f * finSs(kFinPause, kFinTear, since)) : R * (0.15f + 0.9f * g);
        DrawBillboard(c, softDotTexture_, site, sz, Fade(Color{255, 190, 110, 255}, std::min(1.f, 0.35f + 0.65f * g)));
        if (pause) DrawBillboard(c, softDotTexture_, site, sz * 0.45f, Fade(Color{255, 240, 210, 255}, 0.9f));
    } else {
        if (x < 2.5f) {   // the blast core: bright but bounded to the moon's own neighbourhood
            DrawBillboard(c, softDotTexture_, centre, R * (1.6f + 2.4f * finSs(0.f, 1.5f, x)), Fade(Color{255, 225, 170, 255}, 0.85f * expf(-x * 1.8f)));
            DrawBillboard(c, softDotTexture_, centre, R * (0.9f + 1.0f * x), Fade(Color{255, 250, 235, 255}, 0.7f * expf(-x * 3.f)));
        }
        if (x < 10.f) DrawBillboard(c, softDotTexture_, centre, R * (2.4f + 1.4f * x), Fade(Color{255, 120, 40, 255}, 0.35f * expf(-x * 0.35f) * (1.f - finSs(6.f, 10.f, x))));
    }
    for (const auto& f : finFire_) {   // fire: yellow-white young, orange, then smoky red
        if (!f.alive) continue;
        const float age = 1.f - f.life / f.maxLife;
        Color col = age < 0.25f ? Color{255, 215, 150, 255} : age < 0.6f ? Color{255, 135, 45, 255} : Color{150, 45, 18, 255};
        const float a = Clamp(f.life / (f.maxLife * 0.4f), 0.f, 1.f) * (f.core ? 0.6f : 0.75f) * Clamp(age / 0.06f, 0.f, 1.f);
        const Vector3 p = Vector3Add(centre, Vector3Scale(f.pos, R));
        if (Vector3DotProduct(Vector3Subtract(p, c.position), camF) < 0.5f) continue;
        DrawBillboard(c, softDotTexture_, p, f.size * R * 2.f, Fade(col, a));
    }
    if (fireballShaderLoaded_) EndShaderMode();
    for (const auto& k : finRocks_) if (k.alive && k.hot > 0.25f) {   // glow around the hot fragments
        const Vector3 p = Vector3Add(centre, Vector3Scale(k.pos, R));
        if (Vector3DotProduct(Vector3Subtract(p, c.position), camF) < 0.5f) continue;
        DrawBillboard(c, softDotTexture_, p, k.r * R * 3.2f, Fade(Color{255, 120, 40, 255}, 0.5f * k.hot));
    }
    if (finTorn_) {   // shockwave rings: a fast bright one, a slow thick fiery one, one in another plane
        const Vector3 n = Vector3Normalize(Vector3Add(c.up, Vector3Scale(camF, 0.42f)));
        Vector3 e1 = Vector3Normalize(Vector3CrossProduct(n, camF)); if (Vector3Length(e1) < 0.1f) e1 = {1, 0, 0};
        const Vector3 e2 = Vector3CrossProduct(n, e1);
        const Vector3 n2 = Vector3Normalize(Vector3Add(Vector3Scale(n, 0.5f), Vector3Scale(e1, 0.85f)));
        Vector3 f1 = Vector3Normalize(Vector3CrossProduct(n2, camF)); if (Vector3Length(f1) < 0.1f) f1 = e2;
        const Vector3 f2 = Vector3CrossProduct(n2, f1);
        if (x < 6.f) drawRing(centre, e1, e2, R * (1.f + x * 3.4f), R * (0.05f + x * 0.07f), 0.9f * (1.f - x / 6.f), Color{255, 230, 190, 255});
        const float a2 = x - 0.25f; if (a2 > 0.f && a2 < 9.f) drawRing(centre, e1, e2, R * (1.f + a2 * 1.9f), R * (0.14f + a2 * 0.12f), 0.75f * (1.f - a2 / 9.f), Color{255, 140, 60, 255});
        const float a3 = x - 0.5f; if (a3 > 0.f && a3 < 7.f) drawRing(centre, f1, f2, R * (1.f + a3 * 2.6f), R * (0.08f + a3 * 0.1f), 0.5f * (1.f - a3 / 7.f), Color{255, 180, 110, 255});
    }
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

// ------------------------------------------------------------------ t252 trench run hooks
void MoonLevel::drawOwnedFrame(Render3D& r3d, const View& view, float lt, const Texture2D* glow, float renderScale) {
    if (!trench_) return;
    if (finaleLT_ < 0.f) { trench_->draw(r3d, view, lt - kTrenchStart, renderScale); return; }
    // t267: the moon is destroyed completely (drawFinale3D: build, breach, tear, plates, rocks, fire, rings) + the escaping ship
    ClearBackground(Color{6, 4, 16, 255});
    drawSky(r3d, view, lt, glow, renderScale);
    rlEnableDepthTest();
    draw3D(r3d, view, lt);
    const float st = storyTime(lt), since = lt - finaleLT_;
    const Camera3D cb = sceneCamera(view, st);
    const Camera3D c = turnCamera(cb, st);
    Vector3 centre; float R;
    moonFrame(view, cb, st, centre, R);
    BeginMode3D(c);
    trench_->drawEscapeShip(c, centre, R, since * 0.72f);
    EndMode3D();
    rlDrawRenderBatchActive();
    rlDisableDepthTest();   // the HUD is drawn next (2D)
    draw2D(view, view.s);
    if (since < 0.6f) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(Color{255, 150, 70, 255}, 0.45f * (1.f - since / 0.6f)));   // cut in from the trench blast (warm, never white)
}

void MoonLevel::endOfFrame() {
    if (!trench_ || realLT_ < kTrenchStart) return;
    if (getenv("TRENCH_SHOTS") || getenv("TRENCH_FRAMES") || getenv("TRENCH_SNAP_AT")) rlDrawRenderBatchActive();   // t267: captures include the game HUD (still in the batch)
    trench_->captures(realLT_ - kTrenchStart);
    const char* dir = getenv("TRENCH_SHOTS");
    if (!dir || finaleLT_ < 0.f) return;
    static bool done[3]{};
    // t267: build, breach, the tear, debris, crumbling, empty space (19 is the last: the runs stop on it)
    static bool done2[9]{};
    (void)done;
    const float since = realLT_ - finaleLT_, at[9] = {1.5f, 3.3f, 3.8f, 4.25f, 4.9f, 6.5f, 9.0f, 12.0f, 15.2f};
    const char* names[9] = {"17a_build", "17b_breach", "17c_pause", "18a_tear", "18b_explosion", "18c_debris", "18d_crumbling", "18e_drifting", "19_escape_away"};
    for (int i = 0; i < 9; i++) if (!done2[i] && since > at[i]) {
        bool* doneI = &done2[i];
        *doneI = true;
        Image img = LoadImageFromScreen();
        if (img.width > 1280) ImageResize(&img, 1280, img.height * 1280 / img.width);
        ExportImage(img, TextFormat("%s/%s.png", dir, names[i])); UnloadImage(img);
        fprintf(stderr, "TRENCH shot %s t=%.2f\n", names[i], since);
    }
}
