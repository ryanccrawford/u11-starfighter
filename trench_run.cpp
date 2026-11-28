// t252: Moon Base trench run (see trench_run.hpp).
#include "trench_run.hpp"
#include "player.hpp"
#include "weapons.hpp"
#include "audio.hpp"
#include "view.hpp"
#include "render3d.hpp"
#include "config.hpp"
#include "ui_theme.hpp"
#include "rlgl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <type_traits>
#include <climits>

namespace {
constexpr float W2 = TrenchRun::kHalfW, DD = TrenchRun::kDepth, SEG = TrenchRun::kSeg;
constexpr float kSMin = -1600.f, kSLen = 42000.f;
#if defined(__aarch64__)
constexpr float kSegDrawBack = 40.f, kSegDrawFwd = 500.f, kSegLodDist = 200.f, kTileReachBase = 400.f;
constexpr bool kTrenchTablet = true;
#else
constexpr float kSegDrawBack = 40.f, kSegDrawFwd = 1500.f, kSegLodDist = 330.f, kTileReachBase = 900.f;
constexpr bool kTrenchTablet = false;
#endif
constexpr float kULim = 8.2f, kYMin = 4.0f, kYMax = DD - 2.6f, kShipR = 2.4f;
constexpr float kTrenchSpeed = 128.f, kSkimSpeed = 150.f;
constexpr float kDiveAlt = 430.f;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9E3779B9u) { if (!s) s = 1; }
    uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (u() & 0xFFFFFF) / 16777216.f; }
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int a, int b) { return a + (int)(f() * (b - a + 1)); }
};
uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
float frnd(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 100000) / 100000.f; }
float sstep(float a, float b, float x) { float t = Clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); }
Color mixc(Color a, Color b, float t) { t = Clamp(t, 0.f, 1.f); return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)}; }
Matrix basisM(Vector3 x, Vector3 y, Vector3 z, Vector3 p) {
    Matrix m{x.x, y.x, z.x, p.x, x.y, y.y, z.y, p.y, x.z, y.z, z.z, p.z, 0, 0, 0, 1};
    return m;
}
Vector3 rotAxis(Vector3 v, Vector3 k, float a) { return Vector3RotateByAxisAngle(v, k, a); }

// ------------------------------------------------------------------ mesh builder
struct MB {
    std::vector<float> v, n, t, t2; std::vector<unsigned char> c;
    void vert(Vector3 p, Vector3 nn, Vector2 uv, float q, float em, Color col) {
        v.insert(v.end(), {p.x, p.y, p.z}); n.insert(n.end(), {nn.x, nn.y, nn.z});
        t.insert(t.end(), {uv.x, uv.y}); t2.insert(t2.end(), {q, em}); c.insert(c.end(), {col.r, col.g, col.b, col.a});
    }
    void tri(Vector3 a, Vector3 b, Vector3 cc, Vector3 N, Vector2 ua, Vector2 ub, Vector2 uc, float q, float em, Color col) {
        const Vector3 g = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(cc, a));
        if (Vector3DotProduct(g, N) < 0) { std::swap(b, cc); std::swap(ub, uc); }
        vert(a, N, ua, q, em, col); vert(b, N, ub, q, em, col); vert(cc, N, uc, q, em, col);
    }
    void quad(Vector3 a, Vector3 b, Vector3 cc, Vector3 d, Vector3 N, Vector2 ua, Vector2 ub, Vector2 uc, Vector2 ud, float q, float em, Color col) {
        tri(a, b, cc, N, ua, ub, uc, q, em, col); tri(a, cc, d, N, ua, uc, ud, q, em, col);
    }
    // axis-aligned box; planar UVs per face (world units / ts); skip: faces mask (1 -x,2 +x,4 -y,8 +y,16 -z,32 +z)
    void box(Vector3 mn, Vector3 mx, float q, Color col, float ts = 8.f, int skip = 4, float em = 0.f) {
        auto uv = [&](float a, float b) { return Vector2{a / ts, b / ts}; };
        const float x0 = mn.x, x1 = mx.x, y0 = mn.y, y1 = mx.y, z0 = mn.z, z1 = mx.z;
        if (!(skip & 2)) quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0}, uv(z0, y0), uv(z0, y1), uv(z1, y1), uv(z1, y0), q, em, col);
        if (!(skip & 1)) quad({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {-1, 0, 0}, uv(z0, y0), uv(z0, y1), uv(z1, y1), uv(z1, y0), q, em, col);
        if (!(skip & 8)) quad({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, {0, 1, 0}, uv(x0, z0), uv(x1, z0), uv(x1, z1), uv(x0, z1), q, em, col);
        if (!(skip & 4)) quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0}, uv(x0, z0), uv(x1, z0), uv(x1, z1), uv(x0, z1), q, em, col);
        if (!(skip & 32)) quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1}, uv(x0, y0), uv(x1, y0), uv(x1, y1), uv(x0, y1), q, em, col);
        if (!(skip & 16)) quad({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1}, uv(x0, y0), uv(x1, y0), uv(x1, y1), uv(x0, y1), q, em, col);
    }
    // cylinder from a to b, radius r (r2: radius at b)
    void cyl(Vector3 a, Vector3 b, float r, int sides, float q, Color col, bool caps = true, float em = 0.f, float r2 = -1.f) {
        if (r2 < 0) r2 = r;
        const Vector3 ax = Vector3Normalize(Vector3Subtract(b, a));
        Vector3 p = fabsf(ax.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        const Vector3 e1 = Vector3Normalize(Vector3CrossProduct(ax, p)), e2 = Vector3CrossProduct(ax, e1);
        const float L = Vector3Distance(a, b);
        for (int i = 0; i < sides; i++) {
            const float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
            const Vector3 d0 = Vector3Add(Vector3Scale(e1, cosf(a0)), Vector3Scale(e2, sinf(a0)));
            const Vector3 d1 = Vector3Add(Vector3Scale(e1, cosf(a1)), Vector3Scale(e2, sinf(a1)));
            const Vector3 nm = Vector3Normalize(Vector3Add(d0, d1));
            const Vector3 p0 = Vector3Add(a, Vector3Scale(d0, r)), p1 = Vector3Add(a, Vector3Scale(d1, r));
            const Vector3 p2 = Vector3Add(b, Vector3Scale(d1, r2)), p3 = Vector3Add(b, Vector3Scale(d0, r2));
            const float u0 = (float)i / sides * 2.f, u1 = (float)(i + 1) / sides * 2.f;
            quad(p0, p1, p2, p3, nm, {u0, 0}, {u1, 0}, {u1, L / 6.f}, {u0, L / 6.f}, q, em, col);
            if (caps) {
                const Vector2 c0{0.5f, 0.5f}, e0{0.5f + 0.5f * cosf(a0), 0.5f + 0.5f * sinf(a0)}, f0{0.5f + 0.5f * cosf(a1), 0.5f + 0.5f * sinf(a1)};
                tri(b, p3, p2, ax, c0, e0, f0, q, em, col);
                tri(a, p0, p1, Vector3Negate(ax), c0, e0, f0, q, em, col);
            }
        }
    }
    template <typename S, typename D> static void cp(const std::vector<S>& src, D*& dst) {
        dst = (D*)RL_MALLOC(src.size() * sizeof(D)); memcpy(dst, src.data(), src.size() * sizeof(D));
    }
    Mesh upload() {
        Mesh m{};
        m.vertexCount = (int)(v.size() / 3); m.triangleCount = m.vertexCount / 3;
        if (m.vertexCount == 0) return m;
        cp(v, m.vertices); cp(n, m.normals); cp(t, m.texcoords); cp(t2, m.texcoords2); cp(c, m.colors);
        UploadMesh(&m, false);
        return m;
    }
};
Color grey(int g, int a = 255) { return Color{(unsigned char)g, (unsigned char)g, (unsigned char)std::min(255, g + 3), (unsigned char)a}; }

const char* kVS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec2 vertexTexCoord2; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 trenchInv;
out vec3 wPos; out vec3 wN; out vec2 uv; out vec2 uv2; out vec4 col; out vec3 lPos;
void main() {
    vec4 w = matModel * vec4(vertexPosition, 1.0);
    wPos = w.xyz; wN = normalize(mat3(matModel) * vertexNormal);
    uv = vertexTexCoord; uv2 = vertexTexCoord2; col = vertexColor;
    lPos = (trenchInv * w).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";
const char* kFS = R"(#version 330
in vec3 wPos; in vec3 wN; in vec2 uv; in vec2 uv2; in vec4 col; in vec3 lPos;
out vec4 fragColor;
uniform sampler2D texture0; uniform sampler2D texture2; uniform vec4 colDiffuse;
uniform mat4 trenchInv;
uniform vec3 sunDir, camPos, fogCol, tintK; uniform float time, fogK, ambK, specK, halfW, depthD, glowK, flashK; uniform int useAtlas;
uniform vec3 flashPos;
uniform vec4 dmgSpot[8]; uniform float dmgHeat[8]; uniform int dmgN; uniform vec4 dmgHole[2]; uniform int holeN;   // t267 hull damage
uniform vec4 clipA; uniform vec2 clipW;   // t275 surface tiles beside the trench: path point (x, z), heading, curvature; cut / strip half-widths
float hh3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
vec3 perturb(vec3 N, vec3 p, vec2 t, vec3 m) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 du1 = dFdx(t), du2 = dFdy(t);
    vec3 a = cross(dp2, N), b = cross(N, dp1);
    vec3 T = a * du1.x + b * du2.x, B = a * du1.y + b * du2.y;
    float k = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
    return normalize(mat3(T * k, B * k, N) * m);
}
void main() {
    if (clipW.x > 0.0) {   // t275: a tile next to the trench: open over the trench, only its flat base under the rim strip (fills any gap)
        vec2 d = wPos.xz - clipA.xy;
        float a = dot(d, vec2(sin(clipA.z), -cos(clipA.z)));
        float u = abs(dot(d, vec2(cos(clipA.z), sin(clipA.z))) - 0.5 * clipA.w * a * a);
        if (u < clipW.x || (u < clipW.y && wPos.y > -0.34)) discard;
    }
    vec3 N = normalize(wN);
    if (!gl_FrontFacing) N = -N;
    vec3 alb; float ao = 1.0, em = 0.0;
    vec3 dmgE = vec3(0.0);
    if (useAtlas == 1) {
        float q = floor(uv2.x + 0.5);
        vec2 qo = vec2(mod(q, 2.0), floor(q / 2.0)) * 0.5;
        vec2 a = qo + 0.004 + fract(uv) * 0.492;
        vec2 gx = dFdx(uv) * 0.492, gy = dFdy(uv) * 0.492;
        alb = textureGrad(texture0, a, gx, gy).rgb * col.rgb;
        vec3 nm = textureGrad(texture2, a, gx, gy).xyz * 2.0 - 1.0;
        N = perturb(N, wPos, uv, nm);
        ao = col.a; em = uv2.y;
    } else {
        vec4 tx = texture(texture0, uv);
        alb = tx.rgb * colDiffuse.rgb * col.rgb;
        // t267: visible hull damage (player ship only): missing chunks, scorched panels, hot cores
        for (int i = 0; i < 2; i++) {
            if (i >= holeN) break;
            float d = length(wPos - dmgHole[i].xyz), rr = dmgHole[i].w * (0.75 + 0.4 * hh3(floor(wPos * 4.0)));
            if (d < rr) discard;
            float edge = 1.0 - smoothstep(rr, rr * 1.7, d);
            alb *= 1.0 - 0.88 * edge;
            dmgE += vec3(1.0, 0.30, 0.05) * edge * edge * (0.55 + 0.25 * sin(time * 9.0 + wPos.x * 3.0));
        }
        for (int i = 0; i < 8; i++) {
            if (i >= dmgN) break;
            float d = length(wPos - dmgSpot[i].xyz), r = dmgSpot[i].w;
            float k = 1.0 - smoothstep(r * 0.25, r, d * (0.75 + 0.5 * hh3(floor(wPos * 7.0))));
            alb *= 1.0 - 0.86 * k;
            float core = max(0.0, 1.0 - d / (r * 0.42));
            dmgE += vec3(1.0, 0.32, 0.06) * dmgHeat[i] * core * core * (1.1 + 0.3 * sin(time * 13.0 + float(i)));
        }
    }
    if (em > 0.01) {   // lamps / windows / coils: self-lit
        float d = length(wPos - camPos);
        vec3 e = col.rgb * (1.0 + 0.6 * em) * (0.85 + 0.15 * sin(time * 3.0 + wPos.x * 0.7 + wPos.z * 0.3));
        fragColor = vec4(mix(e, fogCol, (1.0 - exp(-d * fogK)) * 0.5), 1.0);
        return;
    }
    vec3 L = normalize(sunDir);
    float sh = 1.0;
    vec3 Ll = mat3(trenchInv) * L;
    vec3 Nl = mat3(trenchInv) * N;
    bool inside = halfW > 0.0 && abs(lPos.x) < halfW + 0.05 && lPos.y < 0.02 && lPos.y > -depthD - 2.0;
    if (inside) {   // the trench walls shadow the floor / far wall: exact for a straight rectangular cut
        if (Ll.y <= 0.02) sh = 0.0;
        else {
            float ur = lPos.x + Ll.x * (-lPos.y) / Ll.y;
            sh = 1.0 - smoothstep(halfW - 0.25, halfW + 0.25, abs(ur));
        }
    }
    float ndl = max(dot(N, L), 0.0);
    vec3 V = normalize(camPos - wPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * specK * ndl;
    vec3 sunC = vec3(1.28, 1.24, 1.16);
    float depthK = inside ? mix(0.45, 1.0, clamp((lPos.y + depthD) / depthD, 0.0, 1.0)) : 1.0;
    float bounce = inside ? 0.16 * max(0.0, -Nl.x * sign(Ll.x)) * clamp(Ll.y * 2.0, 0.0, 1.0) + 0.05 * max(0.0, Nl.y) : 0.0;
    vec3 amb = vec3(0.55, 0.58, 0.68) * ambK * (0.55 + 0.45 * max(N.y, 0.0)) * depthK + vec3(1.0, 0.96, 0.9) * bounce;
    vec3 c = alb * (sunC * ndl * sh + amb) * ao + sunC * spec * sh;
    if (flashK > 0.001) {   // explosion light (bomb / fireball)
        vec3 fd = flashPos - wPos; float fl = length(fd);
        c += alb * vec3(1.0, 0.55, 0.22) * flashK * max(dot(N, fd / max(fl, 0.01)), 0.0) * 2600.0 / (fl * fl + 900.0);
    }
    c += tintK * glowK + dmgE;
    float d = length(wPos - camPos);
    c = mix(c, fogCol, 1.0 - exp(-d * fogK));
    fragColor = vec4(c, 1.0);
})";

// Tablet / Mali-G57: same sun-lit dark trench, but no textureGrad, cheaper spec, and a coarser wall shadow.
// Class-2 tile discard is not used on the tablet (those tiles are skipped in drawEnv).
const char* kFSTablet = R"(#version 330
in vec3 wPos; in vec3 wN; in vec2 uv; in vec2 uv2; in vec4 col; in vec3 lPos;
out vec4 fragColor;
uniform sampler2D texture0; uniform sampler2D texture2; uniform vec4 colDiffuse;
uniform mat4 trenchInv;
uniform vec3 sunDir, camPos, fogCol, tintK; uniform float time, fogK, ambK, specK, halfW, depthD, glowK, flashK; uniform int useAtlas;
uniform vec3 flashPos;
uniform vec4 dmgSpot[8]; uniform float dmgHeat[8]; uniform int dmgN; uniform vec4 dmgHole[2]; uniform int holeN;
uniform vec4 clipA; uniform vec2 clipW;
float hh3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
void main() {
    vec3 N = normalize(wN);
    if (!gl_FrontFacing) N = -N;
    vec3 alb; float ao = 1.0, em = 0.0;
    vec3 dmgE = vec3(0.0);
    if (useAtlas == 1) {
        float q = floor(uv2.x + 0.5);
        vec2 qo = vec2(mod(q, 2.0), floor(q / 2.0)) * 0.5;
        vec2 a = qo + 0.004 + fract(uv) * 0.492;
        alb = texture(texture0, a).rgb * col.rgb;
        vec3 nm = texture(texture2, a).xyz * 2.0 - 1.0;
        N = normalize(N + 0.35 * vec3(nm.x, 0.0, nm.y));
        ao = col.a; em = uv2.y;
    } else {
        vec4 tx = texture(texture0, uv);
        alb = tx.rgb * colDiffuse.rgb * col.rgb;
        for (int i = 0; i < 2; i++) {
            if (i >= holeN) break;
            float d = length(wPos - dmgHole[i].xyz), rr = dmgHole[i].w * (0.75 + 0.4 * hh3(floor(wPos * 4.0)));
            if (d < rr) continue;
            float edge = 1.0 - smoothstep(rr, rr * 1.7, d);
            alb *= 1.0 - 0.88 * edge;
            dmgE += vec3(1.0, 0.30, 0.05) * edge * edge * 0.55;
        }
        for (int i = 0; i < 8; i++) {
            if (i >= dmgN) break;
            float d = length(wPos - dmgSpot[i].xyz), r = dmgSpot[i].w;
            float k = 1.0 - smoothstep(r * 0.25, r, d);
            alb *= 1.0 - 0.86 * k;
            float core = max(0.0, 1.0 - d / (r * 0.42));
            dmgE += vec3(1.0, 0.32, 0.06) * dmgHeat[i] * core * core;
        }
    }
    if (em > 0.01) {
        float d = length(wPos - camPos);
        vec3 e = col.rgb * (1.0 + 0.6 * em);
        fragColor = vec4(mix(e, fogCol, (1.0 - exp(-d * fogK)) * 0.5), 1.0);
        return;
    }
    vec3 L = normalize(sunDir);
    float sh = 1.0;
    vec3 Ll = mat3(trenchInv) * L;
    bool inside = halfW > 0.0 && abs(lPos.x) < halfW + 0.05 && lPos.y < 0.02 && lPos.y > -depthD - 2.0;
    if (inside) sh = Ll.y <= 0.02 ? 0.18 : mix(0.40, 1.0, clamp(Ll.y * 2.0, 0.0, 1.0));
    float ndl = max(dot(N, L), 0.0);
    vec3 V = normalize(camPos - wPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 16.0) * specK * ndl;
    vec3 sunC = vec3(1.28, 1.24, 1.16);
    float depthK = inside ? mix(0.45, 1.0, clamp((lPos.y + depthD) / depthD, 0.0, 1.0)) : 1.0;
    vec3 amb = vec3(0.55, 0.58, 0.68) * ambK * (0.55 + 0.45 * max(N.y, 0.0)) * depthK;
    vec3 c = alb * (sunC * ndl * sh + amb) * ao + sunC * spec * sh;
    if (flashK > 0.001) {
        vec3 fd = flashPos - wPos; float fl = length(fd);
        c += alb * vec3(1.0, 0.55, 0.22) * flashK * max(dot(N, fd / max(fl, 0.01)), 0.0) * 2600.0 / (fl * fl + 900.0);
    }
    c += tintK * glowK + dmgE;
    float d = length(wPos - camPos);
    c = mix(c, fogCol, 1.0 - exp(-d * fogK));
    fragColor = vec4(c, 1.0);
})";
}  // namespace

TrenchAudioCue g_trenchAudio;   // t267

// ------------------------------------------------------------------ path
void TrenchRun::Path::build(float len) {
    const int n = (int)(len / ds) + 2;
    h.assign(n, 0.f); x.assign(n, 0.f); z.assign(n, 0.f);
    struct Bend { float s0, s1, a; };
    std::vector<Bend> bends;
    Rng r(252);
    float sb = 2350.f; int sign = 1;
    auto forbidden = [](float a, float b) {
        for (int k = 0; k < 8; k++) { const float p = kPortS + 2400.f * k; if (b > p - 1350.f && a < p + 260.f) return true; }
        return false;
    };
    while (sb < len + kSMin) {
        const float L = r.r(260.f, 430.f);
        if (forbidden(sb, sb + L)) { sb += 120.f; continue; }
        float a = r.r(9.f, 30.f) * DEG2RAD;
        if (r.f() < 0.75f) sign = -sign;
        bends.push_back({sb, sb + L, a * sign});
        sb += L + r.r(260.f, 650.f);
    }
    float hx = 0, xx = 0, zz = 0;
    for (int i = 0; i < n; i++) {
        const float s = kSMin + i * ds;
        float hh = 0;
        for (const Bend& b : bends) {
            if (s <= b.s0) break;
            const float t = Clamp((s - b.s0) / (b.s1 - b.s0), 0.f, 1.f);
            hh += b.a * t * t * t * (t * (t * 6 - 15) + 10);
        }
        h[i] = hh;
        if (i > 0) { xx += sinf(0.5f * (hx + hh)) * ds; zz -= cosf(0.5f * (hx + hh)) * ds; }
        x[i] = xx; z[i] = zz; hx = hh;
    }
    const int i0 = (int)(-kSMin / ds);   // s = 0 at the origin
    const float ox = x[i0], oz = z[i0];
    for (int i = 0; i < n; i++) { x[i] -= ox; z[i] -= oz; }
}
float TrenchRun::Path::heading(float s) const {
    const float f = Clamp((s - kSMin) / ds, 0.f, (float)h.size() - 2.001f); const int i = (int)f; const float k = f - i;
    return h[i] + (h[i + 1] - h[i]) * k;
}
Vector3 TrenchRun::Path::centre(float s) const {
    const float f = Clamp((s - kSMin) / ds, 0.f, (float)h.size() - 2.001f); const int i = (int)f; const float k = f - i;
    return {x[i] + (x[i + 1] - x[i]) * k, 0.f, z[i] + (z[i + 1] - z[i]) * k};
}
Vector3 TrenchRun::fwd(float s) const { const float h = path_.heading(s); return {sinf(h), 0, -cosf(h)}; }
Vector3 TrenchRun::right(float s) const { const float h = path_.heading(s); return {cosf(h), 0, sinf(h)}; }
Vector3 TrenchRun::world(float s, float u, float y) const {
    const Vector3 c = path_.centre(s), r = right(s);
    return {c.x + r.x * u, y - DD, c.z + r.z * u};
}
Matrix TrenchRun::frame(float s) const {
    const Vector3 c = path_.centre(s);
    return MatrixMultiply(MatrixRotateY(-path_.heading(s)), MatrixTranslate(c.x, 0.f, c.z));
}
float TrenchRun::project(Vector3 p, float s, float& u, float& y) const {
    for (int k = 0; k < 3; k++) s += Vector3DotProduct(Vector3Subtract(p, path_.centre(s)), fwd(s));
    u = Vector3DotProduct(Vector3Subtract(p, path_.centre(s)), right(s)); y = p.y + DD;
    return s;
}

// t275: the true nearest point of the path for any surface point (a coarse grid of path samples, then project())
void TrenchRun::buildPathGrid() {
    pathGrid_.clear();
    for (int i = 0; i < (int)path_.x.size(); i += 4) pathGrid_[gridKey((int)floorf(path_.x[i] / kGridCell), (int)floorf(path_.z[i] / kGridCell))].push_back(i);
}
float TrenchRun::nearestS(Vector3 p, float& u) const {
    const int cx = (int)floorf(p.x / kGridCell), cz = (int)floorf(p.z / kGridCell);
    float best = 1e30f; int bi = -1;
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        const auto it = pathGrid_.find(gridKey(cx + dx, cz + dz));
        if (it == pathGrid_.end()) continue;
        for (int i : it->second) { const float ex = path_.x[i] - p.x, ez = path_.z[i] - p.z, d2 = ex * ex + ez * ez; if (d2 < best) { best = d2; bi = i; } }
    }
    if (bi < 0) { u = 1e4f; return -1e9f; }   // farther than one grid cell from the path
    float y; return project(p, kSMin + bi * path_.ds, u, y);
}

// ------------------------------------------------------------------ kit
static void wallSide(MB& m, MB& lod, Rng& r, int sg, int variant, float z0, float z1, std::vector<TrenchRun::Lamp>& lamps) {
    // sg = -1 left wall (faces +x), +1 right wall (faces -x). y relative to the surface (floor at -DD).
    auto X = [&](float d) { return sg * d; };
    auto bx = [&](MB& b, float dIn, float dOut, float y0, float y1, float za, float zb, float q, Color c, float ts = 8.f, float em = 0.f) {
        const float xa = std::min(X(dIn), X(dOut)), xb = std::max(X(dIn), X(dOut));
        b.box({xa, y0, std::min(za, zb)}, {xb, y1, std::max(za, zb)}, q, c, ts, 4, em);
    };
    auto both = [&](float dIn, float dOut, float y0, float y1, float za, float zb, float q, Color c, float ts = 8.f) {
        bx(m, dIn, dOut, y0, y1, za, zb, q, c, ts); bx(lod, dIn, dOut, y0, y1, za, zb, q, c, ts);
    };
    const float F1 = W2 - 1.2f, F2 = W2 - 0.6f, F3 = W2, FL = W2 - 0.4f;
    const float h1 = -DD + 7.f, h2 = -DD + 19.5f;
    // stepped bands (continuous from segment to segment)
    both(F1, W2 + 3.f, -DD - 1.f, h1, z0, z1, 1, grey(150));                 // concrete footing
    both(W2 - 2.2f, W2 + 3.f, h1, h1 + 0.5f, z0, z1, 0, grey(176));          // ledge 1
    both(F2, W2 + 3.f, h1 + 0.5f, h2, z0, z1, 0, grey(160));                 // metal plating
    both(W2 - 1.6f, W2 + 3.f, h2, h2 + 0.5f, z0, z1, 0, grey(180));          // ledge 2
    both(F3, W2 + 3.f, h2 + 0.5f, -1.5f, z0, z1, 3, grey(168));              // ribbed upper wall
    both(FL, W2 + 3.f, -1.5f, 0.6f, z0, z1, 0, grey(184));                   // rim lip
    // continuous pipe runs + floor-corner conduits
    const float py[3] = {-DD + 10.5f, -DD + 12.0f, -DD + 22.5f}, pr[3] = {0.36f, 0.22f, 0.46f};
    for (int k = 0; k < 3; k++) {
        const float face = k < 2 ? F2 : F3, d = face - pr[k] - 0.06f;
        m.cyl({X(d), py[k], z0}, {X(d), py[k], z1}, pr[k], 10, 0, grey(k == 1 ? 120 : 150), false);
        if (k != 1) lod.cyl({X(d), py[k], z0}, {X(d), py[k], z1}, pr[k], 6, 0, grey(150), false);
    }
    m.cyl({X(F1 - 0.62f), -DD + 0.62f, z0}, {X(F1 - 0.62f), -DD + 0.62f, z1}, 0.6f, 12, 0, grey(140), false);
    m.cyl({X(F1 - 1.5f), -DD + 0.32f, z0}, {X(F1 - 1.5f), -DD + 0.32f, z1}, 0.3f, 8, 0, grey(120), false);
    lod.cyl({X(F1 - 0.62f), -DD + 0.62f, z0}, {X(F1 - 0.62f), -DD + 0.62f, z1}, 0.6f, 6, 0, grey(140), false);
    for (float zc = z0 - 2.f; zc > z1; zc -= 4.f)   // pipe clamps
        for (int k = 0; k < 3; k++) { const float face = k < 2 ? F2 : F3; bx(m, face - pr[k] * 2.f - 0.18f, face, py[k] - pr[k] - 0.12f, py[k] + pr[k] + 0.12f, zc - 0.18f, zc + 0.18f, 0, grey(110)); }
    // kitbash greebles on every band (dense, layered)
    struct Band { float face, y0, y1; };
    const Band bands[3] = {{F1, -DD + 0.9f, h1 - 0.2f}, {F2, h1 + 0.7f, h2 - 0.2f}, {F3, h2 + 0.7f, -1.7f}};
    const int dens = 9 + (variant % 3) * 3;
    for (int b = 0; b < 3; b++) {
        const Band& B = bands[b];
        for (int i = 0; i < dens; i++) {
            const float zw = r.r(0.6f, 4.2f), hh = r.r(0.5f, std::min(5.5f, B.y1 - B.y0)), dep = r.r(0.2f, 1.25f);
            const float zc = r.r(z1 + zw * 0.5f, z0 - zw * 0.5f), yc = r.r(B.y0 + hh * 0.5f, B.y1 - hh * 0.5f);
            bool hit = false; for (int k = 0; k < 3; k++) if (fabsf(yc - py[k]) < hh * 0.5f + pr[k] + 0.1f && (k < 2 ? b == 1 : b == 2)) hit = true;
            if (hit) continue;
            const int qsel = r.i(0, 9); const float q = qsel < 4 ? 0.f : qsel < 7 ? 2.f : 3.f;
            const Color c = grey(r.i(128, 196));
            bx(m, B.face - dep, B.face, yc - hh * 0.5f, yc + hh * 0.5f, zc - zw * 0.5f, zc + zw * 0.5f, q, c, 4.f);
            if (zw * hh > 6.f) bx(lod, B.face - dep, B.face, yc - hh * 0.5f, yc + hh * 0.5f, zc - zw * 0.5f, zc + zw * 0.5f, q, c, 4.f);
            if (r.f() < 0.4f) {   // stacked detail on top of it
                const float zw2 = zw * r.r(0.3f, 0.7f), hh2 = hh * r.r(0.3f, 0.7f), dep2 = std::min(0.35f, 1.3f - dep);
                if (dep2 > 0.05f) bx(m, B.face - dep - dep2, B.face - dep, yc - hh2 * 0.5f, yc + hh2 * 0.5f, zc - zw2 * 0.5f, zc + zw2 * 0.5f, 2, grey(r.i(110, 200)), 4.f);
            }
        }
    }
    const float ribStep = 3.2f + (variant % 2) * 1.6f;   // vertical ribs
    for (float zc = z0 - 1.f - (variant % 3) * 0.7f; zc > z1 + 0.3f; zc -= ribStep) {
        both(F3 - 0.55f, F3, h2 + 0.5f, -1.5f, zc - 0.18f, zc + 0.18f, 0, grey(190), 4.f);
        if (variant == 1 || variant == 4) bx(m, F2 - 0.45f, F2, h1 + 0.5f, h2, zc - 0.16f, zc + 0.16f, 0, grey(176), 4.f);
    }
    for (int i = 0; i < 2; i++) { const float zc = r.r(z1 + 1, z0 - 1); m.cyl({X(F2 - 0.2f), h1 + 0.5f, zc}, {X(F2 - 0.2f), h2, zc}, 0.14f, 6, 0, grey(130), false); }
    for (int i = 0; i < 2 + variant % 3; i++) {   // round discs / hatches
        const int b = r.i(0, 2); const Band& B = bands[b]; const float rr = r.r(0.6f, 1.4f);
        const float zc = r.r(z1 + rr, z0 - rr), yc = r.r(B.y0 + rr, B.y1 - rr);
        m.cyl({X(B.face), yc, zc}, {X(B.face - 0.28f), yc, zc}, rr, 14, 2, grey(r.i(150, 200)));
        m.cyl({X(B.face - 0.28f), yc, zc}, {X(B.face - 0.42f), yc, zc}, rr * 0.35f, 10, 0, grey(110));
    }
    if (variant % 2 == 0) {   // vent with red glowing coils (ref 5)
        const float zc = r.r(z1 + 2, z0 - 2), yc = r.r(h1 + 2.f, h1 + 6.f);
        bx(m, F2 - 0.3f, F2, yc - 0.9f, yc + 0.9f, zc - 1.6f, zc + 1.6f, 2, grey(90), 2.f);
        for (int k = -1; k <= 1; k++) {
            m.cyl({X(F2 - 0.3f), yc, zc + k * 0.95f}, {X(F2 - 0.85f), yc, zc + k * 0.95f}, 0.34f, 10, 0, Color{255, 60, 30, 255}, true, 1.f);
            lamps.push_back({{X(F2 - 0.9f), yc, zc + k * 0.95f}, 0.f, 1.2f, Color{255, 70, 40, 255}, 3});
        }
    }
    for (int w = 0; w < 1 + variant % 2; w++) {   // small lit windows
        const float yc = r.r(h1 + 1.5f, h2 - 1.5f), zc = r.r(z1 + 4, z0 - 4);
        for (int k = 0; k < 5; k++) bx(m, F2 - 0.12f, F2, yc - 0.12f, yc + 0.12f, zc + k * 0.7f - 1.4f, zc + k * 0.7f - 1.1f, 0, Color{255, 236, 190, 255}, 1.f, 0.6f);
    }
    for (int i = 0; i < 1 + variant % 2; i++) {   // ledge poles with red flashing lights (outside the flight envelope)
        const float zc = r.r(z1 + 1.f, z0 - 1.f); const bool upper = r.f() < 0.5f;
        const float base = upper ? h2 + 0.5f : h1 + 0.5f, d = upper ? W2 - 1.1f : W2 - 1.5f, hgt = r.r(1.0f, 2.2f);
        m.cyl({X(d), base, zc}, {X(d), base + hgt, zc}, 0.07f, 5, 0, grey(160), false);
        m.cyl({X(d), base + hgt, zc}, {X(d), base + hgt + 0.18f, zc}, 0.13f, 6, 0, Color{255, 40, 30, 255}, true, 1.f);
        lamps.push_back({{X(d), base + hgt + 0.1f, zc}, r.r(0.f, 6.28f), 0.8f, Color{255, 40, 28, 255}, 0});
    }
    for (int i = 0; i < 6; i++) {   // rim blocks
        const float zw = r.r(0.8f, 3.5f), hh = r.r(0.3f, 1.8f), d0 = r.r(W2 + 0.4f, W2 + 9.f), dw = r.r(0.8f, 3.5f), zc = r.r(z1 + zw * 0.5f, z0 - zw * 0.5f);
        bx(m, d0, d0 + dw, 0.f, hh, zc - zw * 0.5f, zc + zw * 0.5f, r.f() < 0.5f ? 0.f : 2.f, grey(r.i(150, 205)), 4.f);
    }
    for (int i = 0; i < 1 + (variant + 1) % 2; i++) {   // antenna poles on the rim, red lights on top
        const float zc = r.r(z1 + 1.f, z0 - 1.f), d = r.r(W2 + 1.2f, W2 + 6.f), hgt = r.r(3.5f, 7.5f);
        m.cyl({X(d), 0.f, zc}, {X(d), hgt, zc}, 0.12f, 6, 0, grey(170), false);
        m.cyl({X(d) - 0.6f, hgt * 0.7f, zc}, {X(d) + 0.6f, hgt * 0.7f, zc}, 0.05f, 4, 0, grey(170), false);
        lod.cyl({X(d), 0.f, zc}, {X(d), hgt, zc}, 0.14f, 4, 0, grey(170), false);
        lamps.push_back({{X(d), hgt + 0.15f, zc}, r.r(0.f, 6.28f), 1.0f, Color{255, 36, 26, 255}, 0});
    }
    // surface strip beyond the rim: plating + low greebles (towers only far out, on the tiles)
    both(W2 + 3.f, W2 + 96.f, -1.f, 0.f, z0, z1, 0, grey(168), 12.f);
    for (int i = 0; i < 26; i++) {
        const float zw = r.r(1.5f, 7.f), dw = r.r(1.5f, 7.f), d0 = r.r(W2 + 4.f, W2 + 90.f), zc = r.r(z1 + zw * 0.5f, z0 - zw * 0.5f);
        const float hh = d0 < 64.f ? r.r(0.3f, 4.2f) : r.r(0.5f, 9.f);
        const Color c = grey(r.i(140, 210));
        bx(m, d0, d0 + dw, 0.f, hh, zc - zw * 0.5f, zc + zw * 0.5f, r.f() < 0.6f ? 0.f : 2.f, c, 4.f);
        if (zw * dw > 18.f) bx(lod, d0, d0 + dw, 0.f, hh, zc - zw * 0.5f, zc + zw * 0.5f, 0, c, 4.f);
        if (r.f() < 0.3f) m.cyl({X(d0 + dw * 0.5f), hh, zc}, {X(d0 + dw * 0.5f), hh + 0.4f, zc}, std::min(zw, dw) * 0.35f, 12, 2, grey(185));
    }
}

void TrenchRun::buildKit() {
    seg_.resize(11);
    for (int v = 0; v < 11; v++) {
        MB m, lod; Rng r(1000 + v * 77);
        const float z0 = 0.45f, z1 = -SEG - 0.45f;
        m.box({-W2 - 0.5f, -DD - 1.f, z1}, {W2 + 0.5f, -DD, z0}, 1, grey(150), 8.f);   // floor
        lod.box({-W2 - 0.5f, -DD - 1.f, z1}, {W2 + 0.5f, -DD, z0}, 1, grey(150), 8.f);
        m.box({-W2 + 1.6f, -DD, z1}, {-1.6f, -DD + 0.14f, z0}, 0, grey(170), 8.f);
        m.box({1.6f, -DD, z1}, {W2 - 1.6f, -DD + 0.14f, z0}, 0, grey(170), 8.f);
        for (int i = 0; i < 10 + v % 4; i++) {
            const float w = r.r(0.6f, 2.4f), d = r.r(0.6f, 3.f), hh = r.r(0.25f, 1.3f);
            const float x = r.r(-W2 + 2.5f + w * 0.5f, W2 - 2.5f - w * 0.5f), z = r.r(z1 + d * 0.5f, z0 - d * 0.5f);
            m.box({x - w * 0.5f, -DD, z - d * 0.5f}, {x + w * 0.5f, -DD + hh, z + d * 0.5f}, r.f() < 0.5f ? 2.f : 0.f, grey(r.i(120, 190)), 4.f);
        }
        for (int i = 0; i < 2 + v % 2; i++) {   // round floor plates with holes (ref 6)
            const float rr = r.r(0.9f, 1.8f), x = r.r(-W2 + 3.f + rr, W2 - 3.f - rr), z = r.r(z1 + rr, z0 - rr);
            m.cyl({x, -DD, z}, {x, -DD + 0.22f, z}, rr, 16, 2, grey(176));
            m.cyl({x, -DD + 0.22f, z}, {x, -DD + 0.26f, z}, rr * 0.3f, 10, 0, grey(70));
        }
        if (v == 3 || v == 6) {   // glowing blue-white hexagonal floor lamp (ref 5)
            m.cyl({0, -DD + 0.02f, -SEG * 0.5f}, {0, -DD + 0.2f, -SEG * 0.5f}, 1.5f, 6, 2, grey(120));
            m.cyl({0, -DD + 0.2f, -SEG * 0.5f}, {0, -DD + 0.3f, -SEG * 0.5f}, 1.1f, 6, 0, Color{190, 225, 255, 255}, true, 1.f);
            seg_[v].lamps.push_back({{0, -DD + 0.5f, -SEG * 0.5f}, 0.f, 3.6f, Color{150, 200, 255, 255}, 2});
        }
        wallSide(m, lod, r, -1, v, z0, z1, seg_[v].lamps);
        wallSide(m, lod, r, +1, v, z0, z1, seg_[v].lamps);
        if (v == 8 || v == 9) {   // gantry across the trench: fly over or under it
            const float g0 = v == 8 ? -DD + 8.f : -DD + 18.f, g1 = g0 + 3.f, za = -6.f, zb = -9.f;
            m.box({-W2 - 0.2f, g0, zb}, {W2 + 0.2f, g1, za}, 0, grey(172), 4.f, 0);
            lod.box({-W2 - 0.2f, g0, zb}, {W2 + 0.2f, g1, za}, 0, grey(172), 4.f, 0);
            m.box({-W2, g0 - 0.5f, zb + 0.4f}, {W2, g0, za - 0.4f}, 2, grey(120), 2.f, 0);
            for (float x = -W2 + 1.f; x < W2; x += 2.2f) m.box({x - 0.15f, g1, zb}, {x + 0.15f, g1 + 0.45f, za}, 0, grey(190), 2.f);
            m.cyl({-W2, g0 - 0.7f, -7.5f}, {W2, g0 - 0.7f, -7.5f}, 0.35f, 10, 0, grey(140), false);
            for (float x = -W2 + 3.f; x < W2 - 2.f; x += 5.f) {
                m.cyl({x, g1, -7.5f}, {x, g1 + 0.6f, -7.5f}, 0.1f, 5, 0, grey(170), false);
                seg_[v].lamps.push_back({{x, g1 + 0.75f, -7.5f}, x * 0.7f, 0.9f, Color{255, 40, 28, 255}, 0});
            }
            seg_[v].gantry = v == 8 ? 1 : 2;
        }
        if (v == 10) {   // the exhaust port: hex well with a glowing core
            m.cyl({0, -DD, -SEG * 0.5f}, {0, -DD + 0.5f, -SEG * 0.5f}, 4.6f, 6, 0, grey(190));
            m.cyl({0, -DD + 0.5f, -SEG * 0.5f}, {0, -DD + 0.55f, -SEG * 0.5f}, 3.4f, 6, 2, Color{30, 30, 34, 255});
            m.cyl({0, -DD + 0.55f, -SEG * 0.5f}, {0, -DD + 0.62f, -SEG * 0.5f}, 1.9f, 6, 0, Color{255, 200, 120, 255}, true, 1.f);
            for (int k = 0; k < 6; k++) {
                const float a = k * PI / 3.f;
                m.box({cosf(a) * 5.2f - 0.4f, -DD, -SEG * 0.5f + sinf(a) * 5.2f - 0.4f}, {cosf(a) * 5.2f + 0.4f, -DD + 0.9f, -SEG * 0.5f + sinf(a) * 5.2f + 0.4f}, 0, grey(200), 2.f);
                seg_[v].lamps.push_back({{cosf(a) * 5.2f, -DD + 1.1f, -SEG * 0.5f + sinf(a) * 5.2f}, k * 1.05f, 0.9f, Color{255, 40, 28, 255}, 0});
            }
            seg_[v].lamps.push_back({{0, -DD + 1.2f, -SEG * 0.5f}, 0.f, 9.f, Color{255, 170, 90, 255}, 4});
        }
        seg_[v].full = m.upload(); seg_[v].lod = lod.upload(); seg_[v].ok = seg_[v].full.vertexCount > 0;
        fprintf(stderr, "TRENCH kit seg %d tris %d lod %d lamps %d\n", v, seg_[v].full.triangleCount, seg_[v].lod.triangleCount, (int)seg_[v].lamps.size());
    }
    tile_.resize(6);   // surface tiles (world grid): 0-2 low (near the trench), 3-5 with towers
    for (int v = 0; v < 6; v++) {
        MB m, lod; Rng r(5000 + v * 31);
        const float T = kTile;
        m.box({0, -1.35f, -T}, {T, -0.35f, 0}, 0, grey(160), 12.f);
        lod.box({0, -1.35f, -T}, {T, -0.35f, 0}, 0, grey(160), 12.f);
        for (int gx = 0; gx < 8; gx++) for (int gz = 0; gz < 8; gz++) {
            const int n = r.i(0, 2);
            for (int k = 0; k < n; k++) {
                const float w = r.r(1.5f, 7.f), d = r.r(1.5f, 7.f), hh = r.r(0.4f, 3.5f);
                const float x = gx * 8.f + r.r(w * 0.5f, 8.f - w * 0.5f), z = -(gz * 8.f + r.r(d * 0.5f, 8.f - d * 0.5f));
                const Color c = grey(r.i(135, 210));
                m.box({x - w * 0.5f, -0.35f, z - d * 0.5f}, {x + w * 0.5f, hh, z + d * 0.5f}, r.f() < 0.6f ? 0.f : 2.f, c, 4.f);
                if (w * d > 20.f) lod.box({x - w * 0.5f, -0.35f, z - d * 0.5f}, {x + w * 0.5f, hh, z + d * 0.5f}, 0, c, 4.f);
            }
        }
        if (v >= 3) for (int k = 0; k < 2 + v % 2; k++) {   // stepped towers, antenna, red light, lit windows
            const float x = r.r(10, T - 10), z = -r.r(10, T - 10), w = r.r(4, 9), hh = r.r(9, 24);
            m.box({x - w * 0.5f, -0.35f, z - w * 0.5f}, {x + w * 0.5f, hh, z + w * 0.5f}, 3, grey(r.i(150, 200)), 4.f);
            lod.box({x - w * 0.5f, -0.35f, z - w * 0.5f}, {x + w * 0.5f, hh, z + w * 0.5f}, 3, grey(170), 4.f);
            m.box({x - w * 0.35f, hh, z - w * 0.35f}, {x + w * 0.35f, hh + w * 0.4f, z + w * 0.35f}, 2, grey(180), 4.f);
            m.cyl({x, hh + w * 0.4f, z}, {x, hh + w * 0.4f + 5.f, z}, 0.14f, 5, 0, grey(170), false);
            tile_[v].lamps.push_back({{x, hh + w * 0.4f + 5.2f, z}, r.r(0, 6.28f), 1.3f, Color{255, 36, 26, 255}, 0});
            for (int q = 0; q < 6; q++) m.box({x - w * 0.5f - 0.05f, hh * 0.6f, z - w * 0.4f + q * w * 0.14f}, {x - w * 0.5f, hh * 0.6f + 0.3f, z - w * 0.4f + q * w * 0.14f + 0.35f}, 0, Color{255, 232, 180, 255}, 1.f, 4, 0.6f);
        }
        for (int k = 0; k < 3; k++) {   // domes / round discs
            const float x = r.r(6, T - 6), z = -r.r(6, T - 6), rr = r.r(1.5f, 4.f);
            m.cyl({x, -0.35f, z}, {x, 0.8f, z}, rr, 16, 2, grey(175), true, 0.f, rr * 0.7f);
        }
        for (int k = 0; k < 2; k++) {
            const float x = r.r(4, T - 4), z = -r.r(4, T - 4), hgt = r.r(3, 6);
            m.cyl({x, -0.35f, z}, {x, hgt, z}, 0.12f, 5, 0, grey(170), false);
            tile_[v].lamps.push_back({{x, hgt + 0.15f, z}, r.r(0, 6.28f), 0.9f, Color{255, 36, 26, 255}, 0});
        }
        tile_[v].full = m.upload(); tile_[v].lod = lod.upload(); tile_[v].ok = true;
    }
    {   // turret: round base + collar (no square base), drum head, twin barrels
        MB b, h, g;
        b.cyl({0, 0, 0}, {0, 1.25f, 0}, 1.1f, 18, 0, grey(170));
        b.cyl({0, 0, 0}, {0, 0.32f, 0}, 1.45f, 20, 2, grey(150));
        b.cyl({0, 1.25f, 0}, {0, 1.4f, 0}, 0.95f, 16, 0, grey(120));
        h.cyl({-0.95f, 0, 0}, {0.95f, 0, 0}, 0.82f, 16, 0, grey(180));
        h.cyl({-1.0f, 0, 0}, {-0.95f, 0, 0}, 0.5f, 12, 2, grey(140));
        h.cyl({0.95f, 0, 0}, {1.0f, 0, 0}, 0.5f, 12, 2, grey(140));
        h.box({-0.5f, -0.3f, -1.0f}, {0.5f, 0.35f, -0.6f}, 2, grey(150), 2.f, 0);
        for (int k = -1; k <= 1; k += 2) {
            g.cyl({k * 0.42f, 0, -0.6f}, {k * 0.42f, 0, -1.3f}, 0.24f, 10, 0, grey(140));
            g.cyl({k * 0.42f, 0, -1.3f}, {k * 0.42f, 0, -3.1f}, 0.12f, 8, 0, grey(110));
            g.cyl({k * 0.42f, 0, -3.1f}, {k * 0.42f, 0, -3.25f}, 0.17f, 8, 0, grey(90));
        }
        turretBase_ = b.upload(); turretHead_ = h.upload(); turretGun_ = g.upload();
        MB c; c.box({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, 0, grey(150), 1.f, 0); cube_ = c.upload();
        MB bm; bm.cyl({0, 0, 0.9f}, {0, 0, -0.9f}, 0.45f, 12, 0, grey(90)); bm.cyl({0, 0, -0.9f}, {0, 0, -1.3f}, 0.45f, 12, 0, grey(120), true, 0.f, 0.1f);
        bm.box({-0.75f, -0.05f, 0.5f}, {0.75f, 0.05f, 0.95f}, 0, grey(150), 1.f, 0); bm.box({-0.05f, -0.75f, 0.5f}, {0.05f, 0.75f, 0.95f}, 0, grey(150), 1.f, 0);
        bombMesh_ = bm.upload();
    }
}

void TrenchRun::loadShips() {
    auto prep = [&](Model& m, const char* path, bool player, float len, float& scale, Vector3& c, Vector3& wl, Vector3& wr) {
        if (!FileExists(path)) { fprintf(stderr, "TRENCH missing %s\n", path); return false; }
        m = LoadModel(path);
        if (m.meshCount == 0) return false;
        for (int i = 0; i < m.materialCount; i++) m.materials[i].shader = sh_;
        const BoundingBox bb = GetModelBoundingBox(m);
        c = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f);
        const float L = player ? bb.max.x - bb.min.x : bb.max.z - bb.min.z;
        scale = len / std::max(0.01f, L);
        if (player) {
            // player_starfighter.glb basis: nose -X, up +Y, port +Z. Scale/center from the hull
            // (nose to engine outlets), not the bind-pose bbox which includes plumes / detached parts.
            constexpr float kHullNoseX = -5.73f, kHullEngineX = 4.27f, kHullHalfY = 1.05f, kWingTipZ = 4.4f;
            const float hullLen = kHullEngineX - kHullNoseX;
            scale = len / std::max(0.01f, hullLen);
            c = {(kHullNoseX + kHullEngineX) * 0.5f, 0.f, 0.f};
            wl = {1.0f, 0.13f, kWingTipZ};   // wing lamp housings (nav lights)
            wr = {1.0f, 0.13f, -kWingTipZ};
            pNozL_ = {kHullEngineX, 0.22f, -0.98f};
            pNozR_ = {kHullEngineX, 0.22f, 0.98f};
            pNoz_ = {kHullEngineX, 0.22f, 0.f};
            Render3D::hideExhaustConeMeshes(m);
            halfSpan_ = kWingTipZ * scale;
            halfThick_ = kHullHalfY * scale;
        } else { wl = {bb.max.x, c.y, c.z}; wr = {bb.min.x, c.y, c.z}; cNoz_ = {c.x, c.y, bb.min.z}; }
        fprintf(stderr, "TRENCH model %s bb (%.2f %.2f %.2f)-(%.2f %.2f %.2f) scale %.3f meshes %d\n", path, bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z, scale, m.meshCount);
        return true;
    };
    const char* playerPath = "assets/models/player_starfighter.glb";
#if defined(__aarch64__)
    if (FileExists("assets/models/player_starfighter_tablet.glb"))
        playerPath = "assets/models/player_starfighter_tablet.glb";
#endif
    playerOk_ = prep(player_, playerPath, true, 7.0f, playerScale_, playerC_, pWingL_, pWingR_);
    if (playerOk_) {   // t267: hull points for the damage marks (model space)
        int total = 0; for (int i = 0; i < player_.meshCount; i++) total += player_.meshes[i].vertexCount;
        const int step = std::max(1, total / 1500);
        int k = 0;
        for (int i = 0; i < player_.meshCount; i++) {
            const Mesh& m = player_.meshes[i];
            if (!m.vertices) continue;
            for (int v = 0; v < m.vertexCount; v++, k++) if (k % step == 0) hullVerts_.push_back({m.vertices[v * 3], m.vertices[v * 3 + 1], m.vertices[v * 3 + 2]});
        }
        fprintf(stderr, "TRENCH hull points %d (of %d)\n", (int)hullVerts_.size(), total);
    }
    const char* ce = getenv("TRENCH_CHASER");
    const std::string cp = std::string("assets/models/") + (ce ? ce : "enemy_grunt.glb");
    chaserOk_ = prep(chaser_, cp.c_str(), false, 6.2f, chaserScale_, chaserC_, cWingL_, cWingR_);
}

void TrenchRun::loadSounds() {
    if (!IsAudioDeviceReady()) return;
    auto ld = [](const char* n) { Sound s{}; const std::string p = std::string("assets/trench/") + n; if (FileExists(p.c_str())) s = LoadSound(p.c_str()); return s; };
    sRoar_ = ld("trench_roar.wav"); sWhoosh_ = ld("whoosh.wav"); sTurret_ = ld("turret_shot.wav"); sChaser_ = ld("chaser_shot.wav");
    sDrop_ = ld("bomb_drop.wav"); sBlast_ = ld("bomb_blast.wav"); sCrash_ = ld("crash.wav"); sScrape_ = ld("scrape.wav"); sBeep_ = ld("target_beep.wav");
    sndOk_ = sRoar_.frameCount > 0;
}

TrenchRun::TrenchRun() {}
TrenchRun::~TrenchRun() {
    for (auto* vs : {&seg_, &tile_}) for (auto& v : *vs) if (v.ok) { UnloadMesh(v.full); if (v.lod.vertexCount) UnloadMesh(v.lod); }
    for (Mesh* m : {&turretBase_, &turretHead_, &turretGun_, &cube_, &bombMesh_}) if (m->vertexCount) UnloadMesh(*m);
    if (texOk_) { UnloadTexture(albedo_); UnloadTexture(normal_); }
    if (soft_.id) UnloadTexture(soft_);
    if (fire_.id) UnloadTexture(fire_);
    const Shader def{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
    if (playerOk_) { for (int i = 0; i < player_.materialCount; i++) player_.materials[i].shader = def; UnloadModel(player_); }
    if (chaserOk_) { for (int i = 0; i < chaser_.materialCount; i++) chaser_.materials[i].shader = def; UnloadModel(chaser_); }
    matAtlas_.shader = def; matPlain_.shader = def;   // (the atlas textures are owned above)
    if (shOk_) UnloadShader(sh_);
    if (sndOk_) { StopSound(sRoar_); StopSound(sScrape_); }
    for (Sound* s : {&sRoar_, &sWhoosh_, &sTurret_, &sChaser_, &sDrop_, &sBlast_, &sCrash_, &sScrape_, &sBeep_}) if (s->frameCount) UnloadSound(*s);
    if (getenv("AUTOPLAY")) g_autopilotHold = {-1.f, -1.f};
}

bool TrenchRun::init() {
    const double t0 = GetTime();
    path_.build(kSLen);
    buildPathGrid();
    sun_ = Vector3Normalize({0.50f, 0.78f, 0.36f});
#if defined(__aarch64__)
    sh_ = LoadShaderFromMemory(kVS, kFSTablet);
#else
    sh_ = LoadShaderFromMemory(kVS, kFS);
#endif
    shOk_ = sh_.id > 0 && sh_.id != rlGetShaderIdDefault();
    if (!shOk_) { fprintf(stderr, "TRENCH shader failed\n"); return false; }
    sh_.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(sh_, "texture2");
    sh_.locs[SHADER_LOC_VERTEX_TEXCOORD02] = GetShaderLocationAttrib(sh_, "vertexTexCoord2");
    sh_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(sh_, "matModel");
    locTrenchInv_ = GetShaderLocation(sh_, "trenchInv"); locSun_ = GetShaderLocation(sh_, "sunDir"); locCam_ = GetShaderLocation(sh_, "camPos");
    locTime_ = GetShaderLocation(sh_, "time"); locUseAtlas_ = GetShaderLocation(sh_, "useAtlas"); locFog_ = GetShaderLocation(sh_, "fogCol");
    locFogK_ = GetShaderLocation(sh_, "fogK"); locAmb_ = GetShaderLocation(sh_, "ambK"); locSpec_ = GetShaderLocation(sh_, "specK");
    locClipA_ = GetShaderLocation(sh_, "clipA"); locClipW_ = GetShaderLocation(sh_, "clipW");
    { const Vector2 off{-1.f, -1.f}; SetShaderValue(sh_, locClipW_, &off, SHADER_UNIFORM_VEC2); }
    locHalfW_ = GetShaderLocation(sh_, "halfW"); locDepth_ = GetShaderLocation(sh_, "depthD"); locTint_ = GetShaderLocation(sh_, "tintK");
    locGlow_ = GetShaderLocation(sh_, "glowK"); locFlash_ = GetShaderLocation(sh_, "flashK"); locFlashPos_ = GetShaderLocation(sh_, "flashPos");
    locDmgSpot_ = GetShaderLocation(sh_, "dmgSpot"); locDmgHeat_ = GetShaderLocation(sh_, "dmgHeat"); locDmgN_ = GetShaderLocation(sh_, "dmgN");
    locHole_ = GetShaderLocation(sh_, "dmgHole"); locHoleN_ = GetShaderLocation(sh_, "holeN");
    { const int z0 = 0; SetShaderValue(sh_, locDmgN_, &z0, SHADER_UNIFORM_INT); SetShaderValue(sh_, locHoleN_, &z0, SHADER_UNIFORM_INT); }
    fprintf(stderr, "TRENCH damage uniforms spot %d heat %d n %d hole %d holeN %d\n", locDmgSpot_, locDmgHeat_, locDmgN_, locHole_, locHoleN_);
    albedo_ = LoadTexture("assets/trench/trench_albedo.png"); normal_ = LoadTexture("assets/trench/trench_normal.png");
    if (albedo_.id) { GenTextureMipmaps(&albedo_); SetTextureFilter(albedo_, TEXTURE_FILTER_TRILINEAR); }
    if (normal_.id) { GenTextureMipmaps(&normal_); SetTextureFilter(normal_, TEXTURE_FILTER_TRILINEAR); }
    {   // soft round dot + a noisy fire puff
        Image a = GenImageGradientRadial(64, 64, 0.f, WHITE, BLANK); soft_ = LoadTextureFromImage(a); UnloadImage(a);
        Image f = GenImageColor(128, 128, BLANK);
        Color* px = (Color*)f.data; Rng r(77);
        std::vector<float> nz(32 * 32); for (float& q : nz) q = r.f();
        for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
            const float dx = (x - 63.5f) / 63.5f, dy = (y - 63.5f) / 63.5f, d = sqrtf(dx * dx + dy * dy);
            const float gx = x / 4.f, gy = y / 4.f; const int ix = (int)gx & 31, iy = (int)gy & 31; const float fx = gx - (int)gx, fy = gy - (int)gy;
            const float n00 = nz[iy * 32 + ix], n10 = nz[iy * 32 + ((ix + 1) & 31)], n01 = nz[((iy + 1) & 31) * 32 + ix], n11 = nz[((iy + 1) & 31) * 32 + ((ix + 1) & 31)];
            const float n = (n00 * (1 - fx) + n10 * fx) * (1 - fy) + (n01 * (1 - fx) + n11 * fx) * fy;
            const float a2 = Clamp((1.f - d) * 1.6f, 0.f, 1.f) * (0.55f + 0.45f * n) * Clamp(1.f - d * d, 0.f, 1.f);
            px[y * 128 + x] = {255, 255, 255, (unsigned char)(255 * Clamp(a2, 0.f, 1.f))};
        }
        fire_ = LoadTextureFromImage(f); UnloadImage(f);
        SetTextureFilter(soft_, TEXTURE_FILTER_BILINEAR); SetTextureFilter(fire_, TEXTURE_FILTER_BILINEAR);
    }
    texOk_ = albedo_.id && normal_.id;
    matAtlas_ = LoadMaterialDefault(); matAtlas_.shader = sh_;
    matAtlas_.maps[MATERIAL_MAP_DIFFUSE].texture = albedo_; matAtlas_.maps[MATERIAL_MAP_NORMAL].texture = normal_;
    matPlain_ = LoadMaterialDefault(); matPlain_.shader = sh_;
    buildKit();
    loadShips();
    loadSounds();
    if (const char* c = getenv("TRENCH_CAM")) forcedCam_ = atoi(c);
    fprintf(stderr, "TRENCH init %.0f ms (tex %d ships %d/%d sound %d)\n", (GetTime() - t0) * 1000.0, texOk_, playerOk_, chaserOk_, sndOk_);
    return true;
}

int TrenchRun::segVariant(long i) const {
    const float s0 = i * SEG;
    if (sPort_ >= s0 && sPort_ < s0 + SEG) return 10;
    const uint32_t h = hash32((uint32_t)(i + 100000));
    static const bool noGantry = getenv("TRENCH_TEST_NOGANTRY") != nullptr;   // verification runs: clean U-bank logs
    if (!noGantry && s0 > 2700.f && s0 < sPort_ - 700.f && (h % 19) == 0 && (hash32((uint32_t)(i + 99999)) % 19) != 0 && (hash32((uint32_t)(i + 100001)) % 19) != 0) return (h >> 8) & 1 ? 8 : 9;
    return (int)((h >> 4) % 8);
}

void TrenchRun::message(const char* m, float secs, Color c) { msg_ = m; msgT_ = secs; msgCol_ = c; fprintf(stderr, "TRENCH msg '%s' t=%.1f\n", m, lastT_); }

void TrenchRun::spawnFire(Vector3 p, Vector3 v, float size, float grow, float life, int kind, Color c, float drag, float rise) {
    Part& q = parts_[partNext_]; partNext_ = (partNext_ + 1) % (int)parts_.size();
    q = Part{true, p, v, size, grow, life, life, drag, rise, kind, c};
}
void TrenchRun::sparks(Vector3 p, Vector3 n, int count, float speed) {
    for (int i = 0; i < count; i++) {
        const Vector3 d = Vector3Normalize(Vector3Add(n, {frnd(-0.9f, 0.9f), frnd(-0.6f, 0.9f), frnd(-0.9f, 0.9f)}));
        spawnFire(p, Vector3Add(Vector3Scale(d, speed * frnd(0.3f, 1.f)), Vector3Scale(fwd(pl_.s), pl_.v * 0.6f)), frnd(0.08f, 0.16f), 0, frnd(0.2f, 0.5f), 2, Color{255, 210, 140, 255}, 1.5f);
    }
}
void TrenchRun::explode(Vector3 p, float k, Vector3 vel) {
    spawnFire(p, vel, 3.f * k, 26.f * k, 0.35f, 3, Color{255, 235, 200, 255}, 2.f);
    for (int i = 0; i < (int)(26 * k) + 8; i++) {
        const Vector3 d = Vector3Normalize({frnd(-1, 1), frnd(-1, 1), frnd(-1, 1)});
        spawnFire(Vector3Add(p, Vector3Scale(d, frnd(0, 1.2f * k))), Vector3Add(vel, Vector3Scale(d, frnd(4, 22) * k)), frnd(1.2f, 2.8f) * k, frnd(2, 6) * k, frnd(0.5f, 1.2f), 0, Color{255, 200, 120, 255}, 2.2f, 2.f);
    }
    for (int i = 0; i < (int)(8 * k) + 3; i++) {
        const Vector3 d = Vector3Normalize({frnd(-1, 1), frnd(-0.2f, 1), frnd(-1, 1)});
        spawnFire(Vector3Add(p, Vector3Scale(d, frnd(0, k))), Vector3Add(Vector3Scale(vel, 0.6f), Vector3Scale(d, frnd(2, 8) * k)), frnd(1.5f, 3.f) * k, frnd(2, 5) * k, frnd(1.5f, 3.f), 1, Color{70, 64, 60, 255}, 1.2f, 1.5f);
    }
    sparks(p, {0, 1, 0}, (int)(20 * k), 40.f * k);
    for (int i = 0; i < (int)(6 * k) + 2; i++) for (auto& d : debris_) if (!d.alive) {
        const Vector3 dir = Vector3Normalize({frnd(-1, 1), frnd(-0.3f, 1), frnd(-1, 1)});
        d = Debris{true, p, Vector3Add(vel, Vector3Scale(dir, frnd(8, 30) * k)), {frnd(0, 6), frnd(0, 6), frnd(0, 6)}, {frnd(-9, 9), frnd(-9, 9), frnd(-9, 9)}, frnd(0.25f, 0.8f) * k, frnd(1.5f, 3.f), 1.f};
        break;
    }
}

// ------------------------------------------------------------------ update
void TrenchRun::spawnWave(int kind, float t) {
    // kind 0: three line up behind and fire; 1: two blast past close; 2: one slides in alongside; 3: two ahead (shootable)
    const int n = kind == 0 ? 3 : kind == 1 ? 2 : kind == 2 ? 1 : 2;
    int made = 0;
    for (auto& c : ch_) {
        if (c.alive) continue;
        c = Chaser{};
        c.alive = true; c.mode = kind == 3 ? 3 : kind; c.t = 0; c.hp = 4; c.fireT = 1.2f + made * 0.4f;
        c.side = (made % 2) ? 1.f : -1.f;
        if (kind == 0) { c.s = pl_.s - 70.f - made * 14.f; c.u = (made - 1) * 3.2f; c.y = 15.f + made * 1.5f; c.vs = pl_.v + 22.f; c.wu = (made - 1) * 3.2f; c.wy = 13.f + (made % 2) * 6.f; }
        if (kind == 1) { c.s = pl_.s - 90.f - made * 25.f; c.u = c.side * 3.5f; c.y = pl_.y + 3.f; c.vs = pl_.v + 70.f; c.wu = c.side * 2.6f; c.wy = std::min(kYMax - 1, pl_.y + 3.5f); }
        if (kind == 2) { c.s = pl_.s - 60.f; c.u = -Clamp(pl_.u, -1.f, 1.f) * 5.f + 0.1f; c.side = pl_.u > 0 ? -1.f : 1.f; c.y = pl_.y; c.vs = pl_.v + 30.f; c.wu = c.side * 5.f; c.wy = pl_.y; }
        if (kind == 3) { c.s = pl_.s + 260.f + made * 40.f; c.u = c.side * 4.f; c.y = 12.f + made * 5.f; c.vs = pl_.v - 30.f; c.wu = c.side * 3.f; c.wy = c.y; }
        if (++made >= n) break;
    }
    fprintf(stderr, "TRENCH wave kind %d n %d t=%.1f\n", kind, made, t);
}

void TrenchRun::updateTurrets(float dt, float t, Audio&) {
    // turrets live per trench segment around the player: generate on demand by hash
    const long seg0 = (long)floorf((pl_.s - 60.f) / SEG), seg1 = (long)floorf((pl_.s + 560.f) / SEG);
    for (auto& tr : turrets_) if (tr.alive && (tr.seg < seg0 - 2)) tr.alive = false;
    const bool skim = t < kEntryEnd;
    for (long sg = seg0; sg <= seg1; sg++) {
        if (sg * SEG > sPort_ + 600.f) break;
        if (seenTurretSeg_.count(sg)) continue;
        seenTurretSeg_.insert(sg);
        const uint32_t h = hash32((uint32_t)(sg * 7 + 31337));
        const int v = segVariant(sg);
        std::vector<int> slots;
        if (sg * SEG < 2350.f) { if (h % 100 < 14) slots.push_back(4 + (h >> 9) % 2); }
        else if (v < 8 && sg * SEG < sPort_ - 260.f) {
            const int q = h % 100;
            if (q < 38) slots.push_back((h >> 8) % 4);
            else if (q < 50) { slots.push_back((h >> 8) % 2); slots.push_back(2 + (h >> 12) % 2); }
            if (h % 7 == 0) slots.push_back(4 + (h >> 14) % 2);
        }
        (void)skim;
        for (int sl : slots) {
            Turret tr; tr.alive = true; tr.seg = sg; tr.slot = sl; tr.s = sg * SEG + SEG * (0.3f + 0.4f * ((h >> (sl + 3)) % 8) / 8.f);
            const float sgn = (sl % 2) ? 1.f : -1.f;
            if (sl < 2) { tr.u = sgn * (W2 + 3.2f); tr.y = DD; tr.rim = true; }
            else if (sl < 4) { tr.u = sgn * (W2 + 9.f); tr.y = DD; tr.rim = true; }   // t260: set back on the rim (no turrets inside the trench)
            else { tr.u = sgn * (W2 + 24.f + ((h >> 5) % 40)); tr.y = DD; tr.rim = true; }
            if (killedTurret_.count(sg * 8 + sl)) continue;
            tr.fireT = 0.6f + ((h >> 17) % 100) / 100.f * 1.2f;
            tr.yaw = sgn > 0 ? -PI * 0.5f : PI * 0.5f;
            bool dup = false; for (auto& o : turrets_) if (o.alive && o.seg == sg && o.slot == sl) dup = true;
            if (dup) continue;
            bool placed = false;
            for (auto& o : turrets_) if (!o.alive && o.deadT < 0) { o = tr; placed = true; break; }
            if (!placed) turrets_.push_back(tr);
        }
    }
    for (auto& tr : turrets_) {
        if (!tr.alive) { if (tr.deadT >= 0) tr.deadT += dt; continue; }
        const Vector3 base = world(tr.s, tr.u, tr.y);
        const Vector3 head = Vector3Add(base, {0, 1.95f, 0});
        const float ds = tr.s - pl_.s;
        Vector3 aimAt = Vector3Add(plPos_, Vector3Scale(fwd(pl_.s), pl_.v * Clamp(ds, 0.f, 500.f) / 230.f * 0.6f));
        Vector3 d = Vector3Subtract(aimAt, head);
        const float wantYaw = atan2f(-d.x, -d.z);
        const float hd = sqrtf(d.x * d.x + d.z * d.z);
        const float rawPitch = atan2f(d.y, hd);
        // t260: elevation clamp. Every turret head sits above the trench top line (world y = 0), so a barrel that never
        // points below horizontal can never send a shot into the trench volume: inside the trench is safe from turrets.
        constexpr float kMinPitch = 0.02f;
        const float wantPitch = std::max(kMinPitch, rawPitch);
        float dy = wantYaw - tr.yaw; while (dy > PI) dy -= 2 * PI; while (dy < -PI) dy += 2 * PI;
        tr.yaw += Clamp(dy, -3.f * dt, 3.f * dt);
        tr.pitch += Clamp(wantPitch - tr.pitch, -2.f * dt, 2.f * dt);
        tr.recoil = std::max(0.f, tr.recoil - dt * 4.f);
        if (ds > 25.f && ds < 480.f && t > kDiveEnd && !bombDropped_) {
            tr.fireT -= dt;
            if (tr.fireT < 0.55f) tr.tele = 1.f - tr.fireT / 0.55f;
            // only fire when the clamped barrel really bears on the player (he is above the trench top line)
            const Vector3 bar{-sinf(tr.yaw) * cosf(tr.pitch), sinf(tr.pitch), -cosf(tr.yaw) * cosf(tr.pitch)};
            const bool bears = Vector3DotProduct(bar, Vector3Normalize(d)) > cosf(0.16f) && rawPitch >= kMinPitch - 0.03f;
            if (!bears) { tr.fireT = std::max(tr.fireT, 0.6f); tr.tele = 0; if (tr.fireT <= 0.61f && tr.fireT > 0.6f - dt) turretHolds_++; }
            if (tr.fireT <= 0.f) {
                tr.fireT = 1.1f + frnd(0.f, 1.3f); tr.tele = 0; tr.recoil = 1.f; tr.barrel ^= 1;
                Vector3 dir = Vector3Normalize(Vector3Add(bar, {frnd(-0.012f, 0.012f), frnd(0.f, 0.012f), frnd(-0.012f, 0.012f)}));
                if (dir.y < 0.f) dir = Vector3Normalize({dir.x, 0.f, dir.z});
                turretShots_++;
                const Vector3 side = Vector3Normalize(Vector3CrossProduct(dir, {0, 1, 0}));
                const Vector3 mz = Vector3Add(Vector3Add(head, Vector3Scale(dir, 3.3f)), Vector3Scale(side, tr.barrel ? 0.42f : -0.42f));
                for (auto& b : bolts_) if (!b.alive) { b = Bolt{true, mz, Vector3Scale(dir, 230.f), 3.f, tr.s, 8, true, Color{90, 255, 110, 255}, 0.42f, 7.f}; b.turret = true; break; }
                if (sndOk_ && sTurret_.frameCount && ds < 320.f) { SetSoundVolume(sTurret_, 0.45f * (1.f - ds / 340.f)); SetSoundPitch(sTurret_, frnd(0.9f, 1.1f)); PlaySound(sTurret_); }
            }
        } else tr.tele = 0;
        if (ds < -80.f) tr.alive = false;
    }
}

void TrenchRun::updateChasers(float dt, float t, Audio&, int& score) {
    for (auto& c : ch_) {
        if (!c.alive) continue;
        c.t += dt;
        if (c.crashT >= 0) {   // spinning into the wall / floor, then a fireball
            c.crashT += dt;
            c.s += c.vs * dt; c.u += c.vu * dt; c.y += c.vy * dt; c.vy -= 18.f * dt; c.bank += 9.f * dt * c.side;
            if (fabsf(c.u) > W2 - 2.f || c.y < 1.5f || c.crashT > 1.6f) {
                c.u = Clamp(c.u, -W2 + 2.f, W2 - 2.f); c.y = std::max(c.y, 1.5f);
                explode(world(c.s, c.u, c.y), 2.2f, Vector3Scale(fwd(c.s), c.vs * 0.4f));
                sparks(world(c.s, c.u, c.y), {-c.u / W2, 0.4f, 0}, 40, 30.f);
                if (sndOk_ && sCrash_.frameCount) { SetSoundVolume(sCrash_, 0.9f); PlaySound(sCrash_); }
                c.alive = false; crashes_++; score += 600;
                fprintf(stderr, "TRENCH chaser crashed t=%.1f\n", t);
            }
            c.pos = world(c.s, c.u, c.y);
            continue;
        }
        const float rel = c.s - pl_.s;
        float wantVs = pl_.v;
        switch (c.mode) {
            case 0: wantVs = pl_.v + Clamp((-40.f - rel) * 0.8f, -40.f, 40.f); c.wu = Clamp(pl_.u * 0.6f + (c.side) * 2.f * sinf(c.t * 0.7f), -6.f, 6.f); break;
            case 1: wantVs = pl_.v + 70.f; if (rel > 160.f) c.mode = 4; break;
            case 2: wantVs = pl_.v + Clamp((4.f - rel) * 1.5f, -30.f, 30.f); c.wu = Clamp(pl_.u + c.side * 5.2f, -kULim, kULim); c.wy = pl_.y + sinf(c.t * 1.1f) * 0.8f; if (c.t > 9.f) c.mode = 4; break;
            case 3: wantVs = pl_.v - 22.f; c.wu = c.side * 3.f + sinf(c.t * 0.9f) * 3.f; if (rel < 30.f) c.mode = 1; break;
            case 4: wantVs = pl_.v + 90.f; c.wy = kYMax + 30.f; if (rel > 400.f) c.alive = false; break;
        }
        if (c.mode == 0 && c.t > 13.f) c.mode = 1;
        c.vs += Clamp(wantVs - c.vs, -60.f * dt, 60.f * dt);
        const float au = (c.wu - c.u) * 9.f - c.vu * 4.2f, ay = (c.wy - c.y) * 9.f - c.vy * 4.2f;
        c.vu += au * dt; c.vy += ay * dt;
        c.s += c.vs * dt; c.u += c.vu * dt; c.y += c.vy * dt;
        c.bank = Lerp(c.bank, Clamp(-c.vu * 0.06f, -0.9f, 0.9f), std::min(1.f, 6.f * dt));
        if (c.mode != 4) c.y = Clamp(c.y, 2.5f, kYMax - 0.5f);
        // the player can shove alongside chasers into the wall: contact pushes them sideways
        const float du = c.u - pl_.u, dyy = c.y - pl_.y;
        if (fabsf(rel) < 6.5f && fabsf(du) < 5.2f && fabsf(dyy) < 3.2f) {
            const float push = (du >= 0 ? 1.f : -1.f);
            c.vu += push * 16.f; c.u += push * (5.2f - fabsf(du)) * 0.5f;
            sparks(Vector3Lerp(plPos_, c.pos, 0.5f), {-push, 0.2f, 0}, 10, 18.f);
        }
        if (fabsf(c.u) > W2 - 3.8f && c.mode != 4) {
            if (fabsf(c.vu) > 6.f) { c.crashT = 0; c.side = c.u > 0 ? 1.f : -1.f; c.vu = c.side * 4.f; message("SMASHED INTO THE WALL!", 1.6f, Color{255, 200, 120, 255}); }
            else { c.u = Clamp(c.u, -W2 + 3.8f, W2 - 3.8f); c.vu = 0; }
        }
        // whoosh when it passes the camera
        if (!c.whooshed && rel > -3.f && c.vs > pl_.v + 5.f) {
            c.whooshed = true;
            if (sndOk_ && sWhoosh_.frameCount) { SetSoundVolume(sWhoosh_, 0.95f); SetSoundPitch(sWhoosh_, frnd(0.9f, 1.15f)); PlaySound(sWhoosh_); }
        }
        // fire forward (at the player when behind it)
        c.fireT -= dt;
        if (c.fireT <= 0 && c.mode != 4 && !bombDropped_) {
            c.fireT = 0.7f + frnd(0, 0.9f);
            c.pos = world(c.s, c.u, c.y);
            Vector3 dir = rel < -8.f ? Vector3Normalize(Vector3Subtract(Vector3Add(plPos_, {frnd(-3, 3), frnd(-2, 2), frnd(-3, 3)}), c.pos)) : fwd(c.s);
            for (int k = -1; k <= 1; k += 2) {
                const Vector3 mz = Vector3Add(c.pos, Vector3Scale(right(c.s), k * 1.2f));
                for (auto& b : bolts_) if (!b.alive) { b = Bolt{true, mz, Vector3Add(Vector3Scale(dir, 260.f), Vector3Scale(fwd(c.s), c.vs * 0.5f)), 2.2f, c.s, 6, true, Color{110, 255, 120, 255}, 0.3f, 6.f}; break; }
            }
            if (sndOk_ && sChaser_.frameCount && fabsf(rel) < 160.f) { SetSoundVolume(sChaser_, 0.5f); SetSoundPitch(sChaser_, frnd(0.92f, 1.1f)); PlaySound(sChaser_); }
        }
        c.pos = world(c.s, c.u, c.y);
    }
}

void TrenchRun::updateBolts(float dt, const std::function<void(int, Vector2)>& damage, Vector2 at, int& score, Audio&) {
    (void)score;   // t267: kills score through hitChaser / hitTurret (scoreP_)
    for (auto& b : bolts_) {
        if (!b.alive) continue;
        const Vector3 p0 = b.p;
        b.p = Vector3Add(b.p, Vector3Scale(b.v, dt));
        b.life -= dt;
        float u, y; b.sGuess = project(b.p, b.sGuess, u, y);
        if (b.turret && b.p.y < 0.35f && fabsf(u) < W2 + 0.5f) { turretBoltsCut_++; b.alive = false; continue; }   // t260: never into the trench
        const bool inTrench = b.p.y < 0.5f;
        if (b.life <= 0 || (inTrench && (fabsf(u) > W2 - 0.6f || y < 0.2f)) || (!inTrench && b.p.y < 0.4f && fabsf(u) > W2 + 1.f)) {
            if (b.life > 0) sparks(b.p, Vector3Normalize(Vector3Negate(b.v)), 6, 14.f);
            b.alive = false; continue;
        }
        if (b.enemy) {   // hit the player? (segment vs sphere)
            const Vector3 seg = Vector3Subtract(b.p, p0), w = Vector3Subtract(plPos_, p0);
            const float L2 = std::max(1e-4f, Vector3DotProduct(seg, seg)), k = Clamp(Vector3DotProduct(w, seg) / L2, 0.f, 1.f);
            if (deadT_ < 0.f && Vector3Distance(Vector3Add(p0, Vector3Scale(seg, k)), plPos_) < kShipR + 0.6f) {
                if (b.turret) { if (pl_.y > DD) turretHitsAbove_++; else turretHitsInside_++; if (enteredT_ < 0.f) turretHitsBeforeEntry_++; }
                b.alive = false; damage(b.dmg, at); hitFlash_ = 1.f; shake_ = std::max(shake_, 0.6f);
                sparks(b.p, Vector3Normalize(Vector3Negate(b.v)), 14, 20.f);
                hullHit(Vector3Add(p0, Vector3Scale(seg, k)), b.turret ? 0.10f : 0.07f);   // t267: pieces come off, the hit stays visible
            }
            continue;
        }
        bool used = false;
        for (auto& c : ch_) {
            if (!c.alive || c.crashT >= 0) continue;
            if (Vector3Distance(b.p, c.pos) < 3.6f || Vector3Distance(Vector3Lerp(p0, b.p, 0.5f), c.pos) < 3.6f) {
                used = true; sparks(b.p, Vector3Normalize(Vector3Negate(b.v)), 8, 16.f);
                hitChaser(c, (float)b.dmg, b.p, Vector3Normalize(b.v), 0);
                break;
            }
        }
        if (!used) for (auto& tr : turrets_) {
            if (!tr.alive) continue;
            const Vector3 head = Vector3Add(world(tr.s, tr.u, tr.y), {0, 1.3f, 0});
            if (Vector3Distance(b.p, head) < 2.4f || Vector3Distance(Vector3Lerp(p0, b.p, 0.5f), head) < 2.4f) {
                used = true; sparks(b.p, {0, 1, 0}, 8, 14.f);
                hitTurret(tr, (float)b.dmg, b.p, 0);
                break;
            }
        }
        if (used) b.alive = false;
    }
}

// ------------------------------------------------------------------ t267: weapons in 3D
static const char* kWeapName[4] = {"laser", "plasma", "flame", "missile"};

void TrenchRun::hitChaser(Chaser& c, float dmg, Vector3 at, Vector3 dir, int by) {
    (void)at; (void)dir;
    if (!c.alive || c.crashT >= 0) return;
    c.hp -= dmg; hitsBy_[by]++;
    if (c.hp <= 0) {
        kills_++; killsBy_[by]++; if (scoreP_) *scoreP_ += 500;
        if (GetRandomValue(0, 3) == 0) { c.crashT = 0; c.side = c.u > 0 ? 1.f : -1.f; c.vu = c.side * 14.f; }
        else { explode(c.pos, 2.f, Vector3Scale(fwd(c.s), c.vs * 0.7f)); c.alive = false;
               if (sndOk_ && sCrash_.frameCount) { SetSoundVolume(sCrash_, 0.8f); PlaySound(sCrash_); } }
        fprintf(stderr, "TRENCH chaser shot down by %s t=%.1f\n", kWeapName[by], lastT_);
    }
}

void TrenchRun::hitTurret(Turret& tr, float dmg, Vector3 at, int by) {
    (void)at;
    if (!tr.alive) return;
    tr.hp -= dmg; hitsBy_[by]++;
    if (tr.hp <= 0) {
        const Vector3 head = Vector3Add(world(tr.s, tr.u, tr.y), {0, 1.3f, 0});
        tr.alive = false; tr.deadT = 0; killedTurret_.insert(tr.seg * 8 + tr.slot); turretKills_++; killsBy_[by]++; if (scoreP_) *scoreP_ += 250;
        explode(head, 1.5f, {0, 0, 0});
        if (sndOk_ && sCrash_.frameCount) { SetSoundVolume(sCrash_, 0.6f); PlaySound(sCrash_); }
        fprintf(stderr, "TRENCH turret destroyed by %s t=%.1f\n", kWeapName[by], lastT_);
    }
}

Vector3 TrenchRun::aimAssist(Vector3 from, Vector3 dir, float cone, float range) const {
    // a gentle lead-corrected nudge toward the target nearest the line of fire (head-on 3D aim)
    float best = cone; Vector3 out = dir;
    const float shotV = 560.f + pl_.v;
    auto consider = [&](Vector3 p, Vector3 vel) {
        const Vector3 d0 = Vector3Subtract(p, from);
        const float L = Vector3Length(d0);
        if (L < 4.f || L > range) return;
        const Vector3 lead = Vector3Add(p, Vector3Scale(Vector3Subtract(vel, Vector3Scale(fwd(pl_.s), pl_.v)), L / shotV));
        const Vector3 d = Vector3Normalize(Vector3Subtract(lead, from));
        const float a = acosf(Clamp(Vector3DotProduct(d, dir), -1.f, 1.f));
        if (a < best) { best = a; out = d; }
    };
    for (const auto& c : ch_) if (c.alive && c.crashT < 0) consider(c.pos, Vector3Scale(fwd(c.s), c.vs));
    for (const auto& tr : turrets_) if (tr.alive) consider(Vector3Add(world(tr.s, tr.u, tr.y), {0, 1.3f, 0}), {0, 0, 0});
    return Vector3Normalize(Vector3Lerp(dir, out, 0.85f));
}

void TrenchRun::updateWeapons3(float dt, float t, Player& player, Weapons& weapons, Audio& audio, bool& dropReq) {
    const bool dead = deadT_ >= 0.f;
    const Vector3 F = fwd(pl_.s);
    const float range = sPort_ - pl_.s;
    const bool dropWindow = bombReady_ && !bombDropped_ && range <= 150.f && range > -20.f;
    // lasers: every bolt the game's weapon spawned (level 1 single / 2 double / 3 spread, rapid colour) becomes a 3D bolt
    weapons.lasers.each([&](Laser& l) {
        if (!l.alive) return;
        l.alive = false;
        if (dead) return;
        const Vector2 rel = Vector2Subtract(l.pos, player.pos);
        const float lat = rel.y / std::max(0.01f, uiS_);
        const float ang = atan2f(l.vel.y, l.vel.x);
        const float side = Clamp(lat / 9.f, -1.f, 1.f);
        const Vector3 mz = Vector3Add(Vector3Add(plPos_, Vector3Scale(plR_, side * 2.6f)), Vector3Scale(F, fabsf(side) < 0.1f ? 3.2f : 1.5f));
        Vector3 dir = rotAxis(F, plU_, -ang);
        dir = aimAssist(mz, dir, 0.07f, 650.f);
        for (auto& b : bolts_) if (!b.alive) { b = Bolt{true, mz, Vector3Scale(dir, 560.f + pl_.v), 1.2f, pl_.s, 1, false, l.col, 0.28f, 8.f}; break; }
        shotsBy_[0]++;
    });
    // plasma lance: a hitscan beam from the nose; damage over time on the first chaser / turret along it
    beamOn_ = false; beamHit_ = false;
    if (weapons.beamOn && !dead) {
        beamOn_ = true; shotsBy_[1]++;
        const Vector3 a = Vector3Add(Vector3Add(plPos_, Vector3Scale(F, 3.4f)), Vector3Scale(plU_, -0.2f));
        const Vector3 d = aimAssist(a, F, 0.05f, 650.f);
        float best = 650.f; Chaser* hc = nullptr; Turret* ht = nullptr;
        auto ray = [&](Vector3 c, float r) -> float {
            const Vector3 w = Vector3Subtract(c, a); const float tc = Vector3DotProduct(w, d);
            if (tc < 0.f) return -1.f;
            const float p2 = Vector3DotProduct(w, w) - tc * tc;
            if (p2 > r * r) return -1.f;
            return std::max(0.f, tc - sqrtf(r * r - p2));
        };
        for (auto& c : ch_) if (c.alive && c.crashT < 0) { const float h = ray(c.pos, 3.2f); if (h >= 0.f && h < best) { best = h; hc = &c; ht = nullptr; } }
        for (auto& tr : turrets_) if (tr.alive) { const float h = ray(Vector3Add(world(tr.s, tr.u, tr.y), {0, 1.3f, 0}), 2.4f); if (h >= 0.f && h < best) { best = h; ht = &tr; hc = nullptr; } }
        beamA_ = a; beamB_ = Vector3Add(a, Vector3Scale(d, best));
        if (hc || ht) {
            beamHit_ = true;
            beamAcc_ += 5.5f * dt;
            while (beamAcc_ >= 1.f) { beamAcc_ -= 1.f; if (hc) hitChaser(*hc, 1.f, beamB_, d, 1); else if (ht) hitTurret(*ht, 1.f, beamB_, 1); }
            for (int i = 0; i < 2; i++) spawnFire(beamB_, Vector3Add(Vector3Scale(F, pl_.v * 0.7f), {frnd(-14, 14), frnd(-6, 16), frnd(-14, 14)}), frnd(0.08f, 0.15f), 0, frnd(0.15f, 0.35f), 2, Color{190, 230, 255, 255}, 1.5f);
        }
    } else beamAcc_ = 0.f;
    audio.sizzle(beamHit_);
    // flamethrower: a cone of fire from the nose; damage ticks (16 / s) on everything inside it
    if (weapons.flameOn && !dead) {
        shotsBy_[2]++;
        const Vector3 a = Vector3Add(plPos_, Vector3Scale(F, 3.6f));
        flameDebt_ += 120.f * dt;
        while (flameDebt_ >= 1.f) {
            flameDebt_ -= 1.f;
            const Vector3 dir = Vector3Normalize(Vector3Add(F, Vector3Add(Vector3Scale(plR_, frnd(-0.24f, 0.24f)), Vector3Scale(plU_, frnd(-0.17f, 0.17f)))));
            spawnFire(Vector3Add(a, {frnd(-0.3f, 0.3f), frnd(-0.3f, 0.3f), frnd(-0.3f, 0.3f)}), Vector3Add(Vector3Scale(F, pl_.v), Vector3Scale(dir, frnd(210.f, 250.f))),
                      frnd(0.35f, 0.6f), frnd(7.f, 11.f), frnd(0.32f, 0.45f), 0, Color{255, 190, 90, 255}, 2.4f, 1.f);
        }
        flameTick_ += dt * 16.f;
        while (flameTick_ >= 1.f) {
            flameTick_ -= 1.f;
            auto inCone = [&](Vector3 p) { const Vector3 w = Vector3Subtract(p, a); const float L = Vector3Length(w);
                                           return L > 0.5f && L < 48.f && Vector3DotProduct(w, F) > L * cosf(0.34f); };
            for (auto& c : ch_) if (c.alive && c.crashT < 0 && inCone(c.pos)) { hitChaser(c, 0.5f, c.pos, F, 2); sparks(c.pos, Vector3Negate(F), 3, 10.f); }
            for (auto& tr : turrets_) if (tr.alive) { const Vector3 h = Vector3Add(world(tr.s, tr.u, tr.y), {0, 1.3f, 0}); if (inCone(h)) hitTurret(tr, 0.5f, h, 2); }
        }
    } else flameTick_ = 0.f;
    audio.flamethrower(weapons.flameOn && !dead);
    // missiles: the secondary launches homing missiles; inside the release window it drops the bomb instead
    int nm = 0;
    weapons.missiles.each([&](Missile& m) { if (m.alive) { nm++; m.alive = false; } });
    static const bool testMis = getenv("TRENCH_TEST_MISSILES") != nullptr;
    if (testMis && !dead && t > 3.f) {   // verification hook: a pair every 1.6 s (the same launch path as the button)
        if (t >= misTestT_) { misTestT_ = t + 1.6f; nm += 2; audio.missileLaunch(); }
    }
    if (nm > 0 && !dead) {
        if (dropWindow) dropReq = true;
        else {
            struct Cand { int c, tr; float score; };
            std::vector<Cand> cand;
            for (int i = 0; i < (int)ch_.size(); i++) if (ch_[i].alive && ch_[i].crashT < 0) {
                const Vector3 w = Vector3Subtract(ch_[i].pos, plPos_); const float L = Vector3Length(w);
                if (L > 5.f && L < 650.f && Vector3DotProduct(w, F) > 0.2f * L) cand.push_back({i, -1, L * 0.3f + acosf(Clamp(Vector3DotProduct(w, F) / L, -1.f, 1.f)) * 200.f});
            }
            for (int i = 0; i < (int)turrets_.size(); i++) if (turrets_[i].alive) {
                const Vector3 w = Vector3Subtract(Vector3Add(world(turrets_[i].s, turrets_[i].u, turrets_[i].y), {0, 1.3f, 0}), plPos_); const float L = Vector3Length(w);
                if (L > 20.f && L < 650.f && Vector3DotProduct(w, F) > 0.5f * L) cand.push_back({-1, i, 60.f + L * 0.3f + acosf(Clamp(Vector3DotProduct(w, F) / L, -1.f, 1.f)) * 200.f});
            }
            std::sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) { return x.score < y.score; });
            for (int k = 0; k < nm; k++) for (auto& m : mis_) if (!m.alive) {
                const float side = (k % 2) ? 1.f : -1.f;
                m = Missile3{};
                m.alive = true; m.life = 3.5f;
                m.p = Vector3Add(Vector3Add(plPos_, Vector3Scale(plR_, side * 2.0f)), Vector3Scale(plU_, -0.7f));
                m.v = Vector3Add(Vector3Add(Vector3Scale(F, pl_.v + 30.f), Vector3Scale(plR_, side * 18.f)), Vector3Scale(plU_, -6.f));
                if (!cand.empty()) { const Cand& c = cand[k % cand.size()]; m.tc = c.c; m.tt = c.tr; }
                shotsBy_[3]++;
                fprintf(stderr, "TRENCH missile launched t=%.1f target %s\n", t, m.tc >= 0 ? "chaser" : m.tt >= 0 ? "turret" : "none");
                break;
            }
        }
    }
    for (auto& m : mis_) {
        if (!m.alive) continue;
        m.life -= dt;
        Vector3 tgt{}; bool has = false;
        if (m.tc >= 0 && ch_[m.tc].alive && ch_[m.tc].crashT < 0) { tgt = ch_[m.tc].pos; has = true; }
        else if (m.tt >= 0 && m.tt < (int)turrets_.size() && turrets_[m.tt].alive) { tgt = Vector3Add(world(turrets_[m.tt].s, turrets_[m.tt].u, turrets_[m.tt].y), {0, 1.3f, 0}); has = true; }
        float sp = std::min(Vector3Length(m.v) + 420.f * dt, pl_.v + 340.f);
        Vector3 dir = Vector3Normalize(m.v);
        if (has) { const Vector3 want = Vector3Normalize(Vector3Subtract(tgt, m.p)); dir = Vector3Normalize(Vector3Lerp(dir, want, std::min(1.f, 5.5f * dt))); }
        m.v = Vector3Scale(dir, sp);
        const Vector3 p0 = m.p;
        m.p = Vector3Add(m.p, Vector3Scale(m.v, dt));
        m.trailT -= dt;
        if (m.trailT <= 0.f) { m.trailT = 0.012f; spawnFire(p0, Vector3Scale(F, pl_.v * 0.25f), 0.35f, 2.6f, 0.8f, 1, Color{150, 148, 145, 255}, 1.2f, 0.3f); }
        bool boom = false;
        auto proxHit = [&](Vector3 c, float r) { const Vector3 seg = Vector3Subtract(m.p, p0), w = Vector3Subtract(c, p0);
                                                 const float L2 = std::max(1e-4f, Vector3DotProduct(seg, seg)), k = Clamp(Vector3DotProduct(w, seg) / L2, 0.f, 1.f);
                                                 return Vector3Distance(Vector3Add(p0, Vector3Scale(seg, k)), c) < r; };
        for (auto& c : ch_) if (!boom && c.alive && c.crashT < 0 && proxHit(c.pos, 3.4f)) { hitChaser(c, 4.f, m.p, dir, 3); boom = true; }
        for (auto& tr : turrets_) if (!boom && tr.alive && proxHit(Vector3Add(world(tr.s, tr.u, tr.y), {0, 1.3f, 0}), 2.8f)) { hitTurret(tr, 3.f, m.p, 3); boom = true; }
        float u, y; project(m.p, pl_.s, u, y);
        if (!boom && m.p.y < 0.2f && (fabsf(u) > W2 - 0.4f || y < 0.3f)) boom = true;   // wall / floor
        if (!boom && m.p.y < 0.2f && fabsf(u) > W2 + 1.f) boom = true;                  // the surface
        if (boom || m.life <= 0.f) {
            explode(m.p, boom ? 0.9f : 0.5f, Vector3Scale(F, pl_.v * 0.4f));
            if (sndOk_ && sCrash_.frameCount) { SetSoundVolume(sCrash_, 0.45f); PlaySound(sCrash_); }
            m.alive = false;
        }
    }
}

// ------------------------------------------------------------------ t267: visible hull damage
void TrenchRun::hullHit(Vector3 at, float sev) {
    if (!playerOk_ || deadT_ >= 0.f || hullVerts_.empty()) return;
    hullHits_++;
    hullAcc_ = std::min(1.f, hullAcc_ + sev);
    const Matrix MW = MatrixMultiply(player_.transform, plM_);
    const Vector3 loc = Vector3Transform(at, MatrixInvert(MW));
    float bd = 1e30f; Vector3 best = loc;
    for (const Vector3& v : hullVerts_) { const float d = Vector3DistanceSqr(v, loc); if (d < bd) { bd = d; best = v; } }
    int idx = -1;
    for (int i = 0; i < nSpots_; i++) if (Vector3Distance(spots_[i].local, best) * playerScale_ < 0.9f) idx = i;
    if (idx < 0) {
        if (nSpots_ < (int)spots_.size()) { idx = nSpots_++; spots_[idx] = HullSpot{}; spots_[idx].local = best; spots_[idx].r = 0.55f; }
        else { idx = 0; for (int i = 1; i < nSpots_; i++) if (spots_[i].str < spots_[idx].str) idx = i; spots_[idx].r = std::min(1.35f, spots_[idx].r + 0.15f); }
    } else spots_[idx].r = std::min(1.35f, spots_[idx].r + 0.18f);
    HullSpot& sp = spots_[idx];
    sp.heat = 1.f; sp.str = std::min(1.f, sp.str + sev * 3.f);
    const Vector3 w = Vector3Transform(sp.local, MW);
    Vector3 n = Vector3Subtract(w, plPos_); n = Vector3Length(n) > 1e-3f ? Vector3Normalize(n) : plU_;
    const Vector3 F = fwd(pl_.s);
    const int np = sev > 0.15f ? 5 : 2 + GetRandomValue(0, 1);
    static const Color cols[3] = {{205, 205, 212, 255}, {185, 38, 30, 255}, {72, 74, 80, 255}};
    for (int k = 0; k < np; k++) for (auto& pl : plates_) if (!pl.alive) {
        pl = Plate{};
        pl.alive = true; pl.p = Vector3Add(w, {frnd(-0.3f, 0.3f), frnd(-0.3f, 0.3f), frnd(-0.3f, 0.3f)});
        pl.v = Vector3Add(Vector3Add(Vector3Scale(F, pl_.v * 0.92f), Vector3Scale(n, frnd(5.f, 14.f))), {frnd(-3, 3), frnd(-1, 4), frnd(-3, 3)});
        pl.rot = {frnd(0, 6.28f), frnd(0, 6.28f), frnd(0, 6.28f)}; pl.spin = {frnd(-9, 9), frnd(-9, 9), frnd(-9, 9)};
        pl.size = {frnd(0.35f, 0.8f), frnd(0.04f, 0.08f), frnd(0.25f, 0.55f)};
        pl.life = frnd(2.f, 3.2f); pl.c = cols[GetRandomValue(0, 2)]; pl.hot = 1.f;
        platesSpawned_++;
        break;
    }
    for (int i = 0; i < 18; i++) {
        const Vector3 d = Vector3Normalize(Vector3Add(n, {frnd(-0.9f, 0.9f), frnd(-0.9f, 0.9f), frnd(-0.9f, 0.9f)}));
        spawnFire(w, Vector3Add(Vector3Scale(F, pl_.v * 0.9f), Vector3Scale(d, frnd(8.f, 26.f))), frnd(0.08f, 0.16f), 0, frnd(0.3f, 0.6f), 2, Color{255, 210, 140, 255}, 1.5f);
    }
    for (int i = 0; i < 3; i++) spawnFire(w, Vector3Add(Vector3Scale(F, pl_.v * 0.85f), Vector3Scale(n, 3.f)), frnd(0.7f, 1.1f), 6.f, 0.25f, 0, Color{255, 170, 80, 255}, 2.f);
    spawnFire(w, Vector3Scale(F, pl_.v), 1.0f, 9.f, 0.12f, 3, Color{255, 220, 170, 255}, 1.f);
    fprintf(stderr, "TRENCH hull hit #%d sev %.2f dmg %.2f spots %d holes %d plates %d t=%.1f\n", hullHits_, sev, std::max(hullAcc_, hullDmg_), nSpots_, nHoles_, platesSpawned_, lastT_);
}

void TrenchRun::updateHull(float dt, const Player& player) {
    if (!playerOk_) return;
    if (lastHp_ >= 0 && player.hp > lastHp_ + player.maxHp / 2) {   // a respawned ship is a new one
        nSpots_ = 0; nHoles_ = 0; hullAcc_ = 0.f;
        fprintf(stderr, "TRENCH hull repaired (respawn) t=%.1f\n", lastT_);
    }
    lastHp_ = player.hp;
    const float hpK = player.maxHp > 0 ? 1.f - Clamp(player.hp / (float)player.maxHp, 0.f, 1.f) : 0.f;
    hullDmg_ = std::max(hullAcc_, hpK);
    const Matrix MW = MatrixMultiply(player_.transform, plM_);
    const Vector3 F = fwd(pl_.s), vShip = Vector3Scale(F, pl_.v);
    // heavy damage: whole chunks come off (wing tips): real holes in the hull, edges glowing
    for (int h = 0; h < 2; h++) {
        if (nHoles_ > h || hullDmg_ < (h == 0 ? 0.5f : 0.8f) || deadT_ >= 0.f) continue;
        holeLocal_[h] = Vector3Lerp(playerC_, h == 0 ? pWingL_ : pWingR_, 0.84f);
        holeR_[h] = 0.75f; nHoles_ = h + 1;
        const Vector3 w = Vector3Transform(holeLocal_[h], MW);
        for (int k = 0; k < 6; k++) for (auto& pl : plates_) if (!pl.alive) {
            pl = Plate{}; pl.alive = true; pl.p = w;
            pl.v = Vector3Add(Vector3Scale(vShip, 0.9f), {frnd(-10, 10), frnd(-2, 10), frnd(-10, 10)});
            pl.rot = {frnd(0, 6.28f), frnd(0, 6.28f), frnd(0, 6.28f)}; pl.spin = {frnd(-7, 7), frnd(-7, 7), frnd(-7, 7)};
            pl.size = {frnd(0.7f, 1.3f), frnd(0.08f, 0.16f), frnd(0.4f, 0.8f)}; pl.life = 3.5f;
            pl.c = k % 2 ? Color{185, 38, 30, 255} : Color{200, 200, 206, 255}; pl.hot = 1.f; platesSpawned_++;
            break;
        }
        explode(w, 0.45f, Vector3Scale(vShip, 0.9f));
        if (sndOk_ && sCrash_.frameCount) { SetSoundVolume(sCrash_, 0.5f); PlaySound(sCrash_); }
        fprintf(stderr, "TRENCH hull chunk lost #%d dmg %.2f t=%.1f\n", h + 1, hullDmg_, lastT_);
    }
    if (deadT_ < 0.f) {
        auto trail = [&](Vector3 w, float str, float& acc, float& sparkT, float heat) {
            Vector3 n = Vector3Subtract(w, plPos_); n = Vector3Length(n) > 1e-3f ? Vector3Normalize(n) : plU_;
            acc += dt * (3.f + 22.f * hullDmg_) * (0.4f + str);
            while (acc >= 1.f) {   // smoke trail streaming behind the hit
                acc -= 1.f;
                spawnFire(Vector3Add(w, Vector3Scale(n, 0.2f)), Vector3Add(Vector3Add(Vector3Scale(vShip, 0.6f), Vector3Scale(n, frnd(1.f, 3.f))), {0, 1.f, 0}),
                          frnd(0.35f, 0.6f), frnd(2.5f, 4.f), frnd(0.7f, 1.1f), 1, Color{58, 55, 52, 255}, 1.4f, 0.5f);
                if (hullDmg_ > 0.45f && heat > 0.25f && GetRandomValue(0, 1) == 0)
                    spawnFire(w, Vector3Add(Vector3Scale(vShip, 0.8f), Vector3Scale(n, 2.f)), 0.45f, 3.f, 0.18f, 0, Color{255, 150, 60, 255}, 1.f);
            }
            if (hullDmg_ > 0.2f) {
                sparkT -= dt;
                if (sparkT <= 0.f) {
                    sparkT = frnd(0.25f, 0.9f) / (0.5f + hullDmg_);
                    for (int i = 0; i < 3; i++) spawnFire(w, Vector3Add(Vector3Scale(vShip, 0.85f), {frnd(-8, 8), frnd(-2, 8), frnd(-8, 8)}), frnd(0.07f, 0.12f), 0, frnd(0.2f, 0.4f), 2, Color{255, 210, 140, 255}, 1.5f);
                }
            }
        };
        for (int i = 0; i < nSpots_; i++) {
            HullSpot& sp = spots_[i];
            sp.heat = std::max(0.10f + 0.35f * hullDmg_ * sp.str, sp.heat - dt * 0.35f);
            trail(Vector3Transform(sp.local, MW), sp.str, sp.smokeAcc, sp.sparkT, sp.heat);
        }
        for (int h = 0; h < nHoles_; h++) trail(Vector3Transform(holeLocal_[h], MW), 1.f, holeAcc_[h], holeSpark_[h], 1.f);
    }
    for (auto& pl : plates_) {   // the broken-off panels tumble away behind the ship
        if (!pl.alive) continue;
        pl.life -= dt; if (pl.life <= 0.f) { pl.alive = false; continue; }
        pl.v = Vector3Scale(pl.v, std::max(0.f, 1.f - 0.35f * dt)); pl.v.y -= 6.f * dt;
        pl.p = Vector3Add(pl.p, Vector3Scale(pl.v, dt)); pl.rot = Vector3Add(pl.rot, Vector3Scale(pl.spin, dt));
        pl.hot = std::max(0.f, pl.hot - dt * 1.4f);
        if (pl.hot > 0.3f && GetRandomValue(0, 2) == 0) spawnFire(pl.p, {0, 0, 0}, 0.25f, 1.5f, 0.3f, 2, Color{255, 200, 120, 255}, 1.f);
    }
}

void TrenchRun::updateParts(float dt) {
    for (auto& p : parts_) {
        if (!p.alive) continue;
        p.life -= dt;
        if (p.life <= 0) { p.alive = false; continue; }
        p.v = Vector3Scale(p.v, std::max(0.f, 1.f - p.drag * dt));
        p.v.y += p.rise * dt;
        if (p.kind == 2) p.v.y -= 30.f * dt;
        p.p = Vector3Add(p.p, Vector3Scale(p.v, dt));
        p.size += p.grow * dt;
    }
    for (auto& d : debris_) {
        if (!d.alive) continue;
        d.life -= dt; if (d.life <= 0) { d.alive = false; continue; }
        d.v.y -= 22.f * dt; d.p = Vector3Add(d.p, Vector3Scale(d.v, dt)); d.rot = Vector3Add(d.rot, Vector3Scale(d.spin, dt));
        d.hot = std::max(0.f, d.hot - dt * 0.7f);
        if (GetRandomValue(0, 2) == 0) spawnFire(d.p, {0, 0, 0}, d.size * 1.2f, 2.f, 0.5f, d.hot > 0.3f ? 0 : 1, d.hot > 0.3f ? Color{255, 170, 80, 255} : Color{60, 56, 52, 255}, 1.f, 1.f);
    }
}

void TrenchRun::update(float dt, float t, Player& player, Weapons& weapons, Audio& audio, const View& view, float s,
                       const std::function<void(int, Vector2)>& damage, int& score, float shake) {
    dt = std::min(dt, 1.f / 20.f);
    lastT_ = t;
    uiS_ = s; playW_ = view.forward(); playH_ = view.lateral();
    const bool autoplay = getenv("AUTOPLAY") != nullptr;
    static const bool earlyDive = getenv("TRENCH_TEST_EARLYDIVE") != nullptr, noPullUp = getenv("TRENCH_TEST_NOPULLUP") != nullptr;
    // ---- FPS log
    fpsAcc_ += GetFrameTime(); fpsN_++;
    if (fpsAcc_ >= 1.f) { const float f = fpsN_ / fpsAcc_; fpsMin_ = std::min(fpsMin_, f); fpsSum_ += f; fpsSecs_++;
        fprintf(stderr, "TRENCHFPS t=%.0f fps %.0f min %.0f avg %.0f\n", t, f, fpsMin_, fpsSum_ / fpsSecs_); fpsAcc_ = 0; fpsN_ = 0; }
    scoreP_ = &score;
    const float dEnd = descEnd();
    const float descK = 1.f - sstep(dEnd - 1.f, dEnd + 1.f, t);   // 1 descent .. 0 trench
    descShake_ = sstep(0.4f, 1.8f, t) * (1.f - sstep(dEnd - 2.2f, dEnd, t)) * (0.7f + 0.3f * sinf(t * 7.3f));   // Planetfall-style buffet, settles at level-off
    if (sndOk_ && !IsSoundPlaying(sRoar_) && finaleT_ < 0 && deadT_ < 0) PlaySound(sRoar_);
    if (sndOk_) { SetSoundVolume(sRoar_, 0.55f * (1.f - 0.6f * descK)); SetSoundPitch(sRoar_, 0.85f + pl_.v / 400.f); }   // t267: the engine hum leads the descent
    // ---- speed by phase
    const bool escape = detT_ >= 0 && t > detT_;
    float wantV = t < kSkimEnd ? kSkimSpeed : kTrenchSpeed;
    if (bombDropped_ || escape) wantV = Lerp(kTrenchSpeed, 250.f, sstep(0.f, 3.f, t - (dropT_ >= 0 ? dropT_ : t)));
    pl_.v += Clamp(wantV - pl_.v, -40.f * dt, 60.f * dt);
    if (t < 0.02f) { pl_ = Ship{}; pl_.s = 0; pl_.v = kSkimSpeed; pl_.y = DD + 14.f + kDiveAlt; plPrevPos_ = shipWorld(pl_); hBase_ = pl_.y; }
    if (deadT_ >= 0.f) pl_.v = 0.f;   // t267: the wreck stays where the fire caught it
    // ---- t260 flight model -------------------------------------------------------------------------------
    // Inside the trench the player flies height only; the lateral line follows the trench centre path. LEFT / RIGHT
    // bank along a U across the trench: slide toward the wall, roll to a full knife-edge bank near it and ride up to
    // just below the top line. Releasing runs back along the same path to the centre line, level. Height never
    // changes by itself (only the wall ride, which unwinds on the way back). Screen-space stick: x right, y down.
    // t267: after the descent the entry is the player's: coming down over the trench blends into the trench flight model
    const float wTime = sstep(kSkimEnd, kEntryEnd, t);
    const bool diving = t < kDiveEnd && !descDone_;
    const float wT = diving ? wTime : std::max(wTime, sstep(DD + 12.f, DD + 1.f, pl_.y));   // 0 skim .. 1 trench
    Vector2 stick = g_flightStick;
    const float pitchSign = g_invertPitch ? -1.f : 1.f;
    float climbIn = -stick.y * pitchSign;            // + = climb (UP / swipe up, unless inverted)
    float bankIn = stick.x;                           // - = left
    if (autoplay) {   // demo pilot: the same inputs a player gives
        float ay = 13.f, ax = 0.f;
        if (t < kSkimEnd) { ay = DD + 12.f + 3.f * sinf(t * 1.3f); ax = 0.35f * sinf(t * 0.8f); }
        if (earlyDive && t < kSkimEnd) { ay = 12.f; ax = 0.f; }   // t267 test: straight down into the trench
        else {
            ay = 13.f + 4.f * sinf(t * 0.5f);
            float best = 1e9f;
            for (auto& c : ch_) if (c.alive && c.crashT < 0) {
                const float rel = c.s - pl_.s;
                if (rel > 10.f && rel < 220.f && rel < best && c.mode != 4) { best = rel; ax = Clamp(c.u / 9.f, -0.5f, 0.5f); ay = Clamp(c.y, 6.f, 22.f); }
                if (fabsf(rel) < 8.f && c.mode == 2) ax = c.u > pl_.u ? 1.f : -1.f;   // bank into it: shove it into the wall
            }
            if (bombReady_ && !bombDropped_) { ax = 0; ay = 10.f; }
            bool gantryAhead = false;   // gantries win over chaser chasing (after it, so it is not overridden)
            for (long sg = (long)floorf((pl_.s - 10.f) / SEG); sg < (long)floorf((pl_.s + 300.f) / SEG) + 1; sg++) {
                const int v = segVariant(sg);
                if (v == 8) { ay = 24.f; gantryAhead = true; break; } else if (v == 9) { ay = 6.f; gantryAhead = true; break; }   // nearest one wins
            }
            if (gantryAhead) ax = 0.f;   // no wall ride (it climbs) into a gantry
            if (const char* hi = getenv("TRENCH_TEST_HIGH")) {   // t275 test: fly above the rim in this trench-time window "t0,t1"
                float h0 = 0.f, h1 = 0.f; if (sscanf(hi, "%f,%f", &h0, &h1) == 2 && t >= h0 && t < h1) { ax = 0.f; ay = DD + 30.f; }
            }
            if (bombDropped_) { ax = 0.f; ay = noPullUp ? 10.f : DD + 160.f; }   // t267: the pilot pulls up (TRENCH_TEST_NOPULLUP: stays in)
        }
        bankIn = ax;
        climbIn = Clamp((ay - hBase_) * 0.6f, -1.f, 1.f);
        if (const char* ts = getenv("TRENCH_TEST_SCRIPT")) {   // hidden verification runs (trench time windows)
            (void)ts;
            struct W { float t0, t1, bank, h; };   // h < 0: keep height (no up/down input)
            static const W win[] = {
                {17.f, 20.f, 0.f, 15.f},                                  // settle at mid height
                {20.f, 23.5f, -1.f, -1.f}, {23.5f, 27.f, 0.f, -1.f},      // LEFT hold + release from mid height
                {27.f, 30.5f, 1.f, -1.f}, {30.5f, 34.f, 0.f, -1.f},       // RIGHT hold + release from mid height
                {34.f, 36.f, 0.f, 3.4f},                                  // down to the floor
                {36.f, 39.5f, -1.f, -1.f}, {39.5f, 43.f, 0.f, -1.f},      // LEFT from near the floor
                {43.f, 46.5f, 1.f, -1.f}, {46.5f, 50.f, 0.f, -1.f},       // RIGHT from near the floor
                {50.f, 52.f, 0.f, 15.f},
            };
            for (const W& w : win) if (t >= w.t0 && t < w.t1) { bankIn = w.bank; climbIn = w.h < 0 ? 0.f : Clamp((w.h - hBase_) * 0.4f, -1.f, 1.f); }
            if (t >= 53.f && t < 54.f) climbIn = -(-1.f) * pitchSign;                // stick.y = -1 (UP) through the invert
            if (t >= 54.f && t < 56.f) climbIn = 0.f;
            if (t >= 57.f && t < 58.f) climbIn = -(-1.f) * pitchSign;
            if (t >= 58.f && t < 60.f) climbIn = 0.f;
            if (t >= 60.f && t < 68.f) { bankIn = 0.f; climbIn = Clamp((DD + 10.f - hBase_) * 0.4f, -1.f, 1.f); }   // above the trench: turrets
            if (t >= 68.f && t < 71.f) { bankIn = 0.f; climbIn = Clamp((14.f - hBase_) * 0.4f, -1.f, 1.f); }        // dive back in
        }
    }
    if (fabsf(bankIn) < 0.12f) bankIn = 0.f; else bankIn = (bankIn - 0.12f * (bankIn > 0 ? 1.f : -1.f)) / 0.88f;
    bankIn = Clamp(bankIn, -1.f, 1.f); climbIn = Clamp(climbIn, -1.f, 1.f);
    if (escape) bankIn = 0.f;
    if (deadT_ >= 0.f) { bankIn = 0.f; climbIn = 0.f; }
    const float yBefore = pl_.y;
    // bank progress: critically damped (no overshoot, no wiggle)
    {
        const float w = 3.4f;
        bankBV_ += (w * w * (bankIn - bankB_) - 2.f * w * bankBV_) * dt;
        bankB_ = Clamp(bankB_ + bankBV_ * dt, -1.f, 1.f);
    }
    const float g = fabsf(bankB_), sg = bankB_ < 0 ? -1.f : 1.f;
    const float rollMag = Lerp(50.f * DEG2RAD * sstep(0.f, 1.f, g), 20.f * DEG2RAD * sstep(0.f, 0.5f, g) + 70.f * DEG2RAD * sstep(0.5f, 0.95f, g), wT);
    // t265: wall ride is canopy-to-wall on both sides (belly toward the trench centre), so the roll sign is flipped
    // relative to t260. Magnitude stays <= 90 deg and passes through 0 at the centre: always the short way, no flip-through.
    const float roll = -sg * rollMag;
    const float horizExt = halfSpan_ * cosf(rollMag) + halfThick_ * sinf(rollMag);
    const float vertExt = halfSpan_ * sinf(rollMag) + halfThick_ * cosf(rollMag);
    constexpr float kWallClear = 10.6f;   // conservative wall plane (ledges at 10.8, pipes ~11.5)
    const float yRide = DD - 0.5f - (halfSpan_ + 0.0f);   // full knife-edge bank: wing tip just below the top line
    const float yFloor = 1.4f + vertExt;
    // height (player): rate control, never changed by recentering
    if (diving) {   // t267: Planetfall-style descent onto the base; DOWN dives steeper, straight for the trench
        const float e = Clamp(t / kDiveEnd, 0.f, 1.f);
        const float glide = DD + 14.f + kDiveAlt * powf(1.f - e, 1.8f);
        if (t > 0.6f && (!autoplay || earlyDive)) diveOff_ = std::min(0.f, diveOff_ + climbIn * 120.f * dt);
        diveOff_ = std::max(diveOff_, (DD + 6.f) - glide);
        hBase_ = glide + diveOff_;
        if (t > 0.6f && diveOff_ < -1.f && hBase_ <= DD + 6.05f) {
            descDone_ = true; descDoneT_ = t;
            fprintf(stderr, "TRENCH descent: levelled off early (player dive) t=%.2f\n", t);
        }
    } else {   // rate control; over the trench the floor is the limit at once (no more hits before you may go down)
        const float floorLo = 1.4f + halfThick_ + 0.1f;
        const bool overTrench = fabsf(pl_.u) + horizExt < kWallClear + 0.6f;
        const float hLo = (wTime >= 1.f || overTrench) ? floorLo : DD + 6.f;
        const float hHi = bombDropped_ ? DD + 600.f : Lerp(DD + 26.f, DD + 16.f, wT);   // t267: after the drop the sky is open: PULL UP
        const float rate = escape ? 46.f : bombDropped_ ? 36.f : (pl_.y > DD + 1.f ? 34.f : 24.f);
        hBase_ = Clamp(hBase_ + climbIn * rate * dt, hLo, hHi);
    }
    // wall climb: armed from the height the bank starts at
    if (g < 0.02f) climbArmed_ = true;
    if (climbArmed_ && fabsf(bankIn) > 0.f) { climbD_ = std::max(0.f, yRide - hBase_); climbArmed_ = false; }
    const float climbK = wT * sstep(0.55f, 1.f, g);
    float ty = std::max(yFloor * wT + (1.f - wT) * (DD + 4.f), hBase_ + climbD_ * climbK);
    if (diving) { ty = hBase_; pl_.y = ty; pl_.vy = 0; }
    // lateral: follows the centre path; the bank slides it along the U (skim: a plain side slide)
    const float uTrench = sg * (kWallClear - horizExt) * sstep(0.f, 0.6f, g);
    const float tu = Lerp(bankB_ * 30.f, uTrench, wT);
    const float uPrev = pl_.u;
    pl_.u = (t < kSkimEnd) ? Lerp(pl_.u, tu, std::min(1.f, 6.f * dt)) : tu;   // skim: eased; trench: exactly on the U
    pl_.vu = (pl_.u - uPrev) / std::max(dt, 1e-4f);
    if (!diving) {   // height follows its target smoothly (critically damped)
        const float w = 7.f;
        pl_.vy += (w * w * (ty - pl_.y) - 2.f * w * pl_.vy) * dt;
        pl_.y += pl_.vy * dt;
    }
    pl_.s += pl_.v * dt;
    // ---- collisions (safety: the U is built clear of the walls; this only catches lag)
    const bool inside = (t >= kEntryEnd - 0.5f || pl_.y < DD + 0.5f) && !(escape && pl_.y > DD + 2.f) && deadT_ < 0.f;
    if (inside) {
        if (pl_.y < yFloor) { if (pl_.y < yFloor - 0.3f) clipFix_++; pl_.y = yFloor; pl_.vy = std::max(pl_.vy, 0.f); }
        if (fabsf(pl_.u) + horizExt > kWallClear + 0.2f && pl_.y < DD) { clipFix_++; pl_.u = sg * (kWallClear + 0.2f - horizExt); }
        const int v = segVariant((long)floorf(pl_.s / SEG));
        if (v == 8 || v == 9) {   // gantry: fly over or under it
            const float zLocal = pl_.s - floorf(pl_.s / SEG) * SEG;
            const float g0 = (v == 8 ? 8.f : 18.f) - 1.1f, g1 = (v == 8 ? 8.f : 18.f) + 3.8f;   // incl. the pipe under / rail on top
            if (zLocal > 6.f - 3.5f && zLocal < 9.f + 3.5f && pl_.y + vertExt > g0 && pl_.y - vertExt < g1) {
                const bool over = pl_.y > (g0 + g1) * 0.5f;
                const float py = over ? g1 + vertExt + 0.1f : g0 - vertExt - 0.1f;
                pl_.y = py; pl_.vy = 0.f;
                hBase_ = over ? std::max(hBase_, py) : std::min(hBase_, py);
                damage(12, player.pos); hitFlash_ = 1.f; shake_ = 1.f;
                hullHit(Vector3Add(plPos_, Vector3Scale(plU_, over ? -1.2f : 1.2f)), 0.2f);   // t267
                sparks(plPos_, {0, over ? 1.f : -1.f, 0}, 40, 30.f);
                if (sndOk_ && sCrash_.frameCount) PlaySound(sCrash_);
                fprintf(stderr, "TRENCH gantry hit t=%.1f var %d y %.1f ext %.1f hBase %.1f\n", t, v, py, vertExt, hBase_);
            }
        }
    }
    if (scrapeT_ > 0) scrapeT_ -= dt;
    else if (sndOk_ && sScrape_.frameCount && IsSoundPlaying(sScrape_)) StopSound(sScrape_);
    pl_.bank = roll;
    if (diving) {   // nose follows the descent path (steep at first, flaring out over the base)
        const float sink = std::max(0.f, (yBefore - pl_.y) / std::max(dt, 1e-4f));
        pl_.pitch = Lerp(pl_.pitch, -atanf(sink / std::max(60.f, pl_.v)) * 0.9f, t < 0.05f ? 1.f : std::min(1.f, 4.f * dt));
    } else pl_.pitch = Lerp(pl_.pitch, Clamp(pl_.vy * 0.03f, -0.5f, 0.6f), std::min(1.f, 6.f * dt));
    plPrevPos_ = plPos_;
    plPos_ = shipWorld(pl_);
    {   // ship matrix: model nose is -X; basis columns (-F, U, -R) after pitching / rolling (+roll = right wing down)
        Vector3 F = fwd(pl_.s), R = right(pl_.s), U{0, 1, 0};
        F = rotAxis(F, R, pl_.pitch); U = rotAxis(U, R, pl_.pitch);
        R = rotAxis(R, F, pl_.bank); U = rotAxis(U, F, pl_.bank);
        plR_ = R; plU_ = U;
        const Matrix B = basisM(Vector3Negate(F), U, Vector3Negate(R), plPos_);
        plM_ = MatrixMultiply(MatrixMultiply(MatrixTranslate(-playerC_.x, -playerC_.y, -playerC_.z), MatrixScale(playerScale_, playerScale_, playerScale_)), B);
    }
    if (getenv("TRENCH_TEST_LOG") && t >= testLogT_) {   // 4 Hz flight log for the verification runs
        testLogT_ = t + 0.25f;
        const Camera3D& c = cam_;
        const Vector2 sp = GetWorldToScreen(plPos_, c);
        const Vector3 wl = Vector3Transform(pWingL_, MatrixMultiply(player_.transform, plM_)), wr = Vector3Transform(pWingR_, MatrixMultiply(player_.transform, plM_));
        const Vector2 pl = GetWorldToScreen(wl, c), pr = GetWorldToScreen(wr, c);
        fprintf(stderr, "FLIGHT t=%.2f in=%+.2f/%+.2f inv=%d b=%+.3f u=%+.2f y=%.2f hBase=%.2f roll=%+.1f ext=%.2f/%.2f wallGap=%.2f floorGap=%.2f shipX=%.0f portWingY=%.0f stbdWingY=%.0f canopyLat=%+.2f\n",
                t, bankIn, climbIn, g_invertPitch ? 1 : 0, bankB_, pl_.u, pl_.y, hBase_, roll * RAD2DEG, horizExt, vertExt,
                kWallClear - (fabsf(pl_.u) + horizExt), pl_.y - vertExt - 1.4f, sp.x, pl.y, pr.y, Vector3DotProduct(Vector3Normalize(Vector3Subtract(Vector3Transform({playerC_.x, playerC_.y + 0.4f, playerC_.z}, MatrixMultiply(player_.transform, plM_)), Vector3Transform(playerC_, MatrixMultiply(player_.transform, plM_)))), right(pl_.s)));   // canopy (mesh +Y) lateral dir: - = left
        static float statT = 0; if (t > statT) { statT = t + 5.f;
            fprintf(stderr, "TURRETSTAT t=%.1f shots %d holdFrames %d hitsAbove %d hitsInside %d cutAtTopLine %d clipFix %d\n", t, turretShots_, turretHolds_, turretHitsAbove_, turretHitsInside_, turretBoltsCut_, clipFix_); }
    }
    // ---- t267 weapons: every primary (lasers by volley level, plasma lance, flamethrower) and the missiles work in 3D.
    // The missile button drops the bomb only inside the release window.
    bool dropReq = false;
    updateWeapons3(dt, t, player, weapons, audio, dropReq);
    // ---- waves (trench time)
    static const float waveT[] = {23.f, 32.f, 39.f, 47.f, 56.f, 65.f, 75.f};
    static const int waveK[] = {0, 1, 2, 3, 0, 1, 0};
    while (wave_ < 7 && t >= waveT[wave_] && !bombDropped_) { spawnWave(waveK[wave_], t); wave_++; }
    { static bool extra = false; if (!extra && getenv("TRENCH_TEST_SCRIPT") && t >= 72.f) { extra = true; spawnWave(2, t); } }
    // ---- the bomb
    const float range = sPort_ - pl_.s;
    if (!bombReady_ && range < 1700.f && t > kEntryEnd) {
        bombReady_ = true; readyT_ = t; message("SUPER DROP BOMB READY", 3.5f, Color{255, 220, 90, 255});
        fprintf(stderr, "TRENCH bomb ready t=%.1f range %.0f\n", t, range);
    }
    bool dropKey = IsKeyDown(KEY_B) || IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_KP_ENTER);
    const bool mouse = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (mouse && !prevMouse_ && wantsDropTap(GetMousePosition())) dropKey = true;
    for (int i = 0; i < GetTouchPointCount(); i++) if (wantsDropTap(GetTouchPosition(i))) dropKey = true;
    bool drop = (dropKey && !prevDropKey_) || dropReq;
    prevDropKey_ = dropKey; prevMouse_ = mouse;
    if (autoplay && bombReady_ && !bombDropped_ && range < 92.f) drop = true;
    if (drop && !bombDropped_) {
        if (!bombReady_) { if (dropKey) message("NOT YET", 1.f, Color{255, 120, 90, 255}); }
        else if (range > 150.f) message("TOO FAR - WAIT FOR THE PORT", 1.2f, Color{255, 160, 90, 255});
        else {
            bombDropped_ = true; dropT_ = t; bombS0_ = pl_.s; bombU0_ = pl_.u; bombY0_ = pl_.y - 1.5f; bombPos_ = world(bombS0_, bombU0_, bombY0_);
            if (sndOk_ && sDrop_.frameCount) { SetSoundVolume(sDrop_, 1.f); PlaySound(sDrop_); }
            message("BOMB AWAY!", 2.f, Color{255, 230, 140, 255});
            fprintf(stderr, "TRENCH bomb dropped t=%.1f range %.0f\n", t, range);
        }
    }
    if (bombReady_ && !bombDropped_ && range < -20.f) {   // missed the port: go around to the next one
        sPort_ += 2400.f; missT_ = t; message("MISSED! NEXT PORT AHEAD", 2.5f, Color{255, 130, 90, 255});
        fprintf(stderr, "TRENCH missed port, next at %.0f\n", sPort_);
    }
    if (bombDropped_ && detT_ < 0) {   // glide into the port (0.5..1.4 s), detonate 1 s after it arrives
        const float since = t - dropT_, arrive = Clamp(range * 0 + (sPort_ + SEG * 0.5f - bombS0_) / std::max(60.f, pl_.v) , 0.5f, 1.4f);
        const float e = sstep(0.f, arrive, since);
        bombPos_ = world(Lerp(bombS0_, sPort_ - SEG * 0.5f + SEG, e), Lerp(bombU0_, 0.f, e), Lerp(bombY0_, 0.6f, e * e));
        bombPos_ = world(Lerp(bombS0_, sPort_ + SEG * 0.5f, e), Lerp(bombU0_, 0.f, e), Lerp(bombY0_, 0.6f, e * e));
        if (since > arrive + 1.0f) {
            detT_ = t; finaleT_ = -1.f; flash_ = 1.f; shake_ = 1.5f;   // t267: the moon shot waits for the escape (or never comes)
            if (sndOk_ && sBlast_.frameCount) { SetSoundVolume(sBlast_, 1.f); PlaySound(sBlast_); }
            audio.play(Sfx::BigExplosion, 1.f, 0.f);
            fprintf(stderr, "TRENCH detonation t=%.1f\n", t);
        }
    }
    if (escape && deadT_ < 0.f && escapedT_ < 0.f) {   // t267: controlled exit: climb out before the fire arrives
        const float since = t - detT_;
        if (pl_.y > DD + 22.f) {
            escapedT_ = t; finaleT_ = std::max(detT_ + 6.f, t + 2.2f);
            fprintf(stderr, "TRENCH escaped: pulled up t=%.2f (%.2f s after detonation) y=%.1f\n", t, since, pl_.y);
        } else if (since >= kBlastReach) {
            deadT_ = t; finaleT_ = -1.f;
            const Vector3 vF = Vector3Scale(fwd(pl_.s), pl_.v * 0.3f);
            explode(plPos_, 3.2f, vF); explode(Vector3Add(plPos_, Vector3Scale(fwd(pl_.s), 4.f)), 2.2f, vF); explode(Vector3Add(plPos_, {0, 3.f, 0}), 2.f, vF);
            for (int k = 0; k < 10; k++) for (auto& pl : plates_) if (!pl.alive) {
                pl = Plate{}; pl.alive = true; pl.p = plPos_;
                pl.v = {frnd(-22, 22), frnd(-4, 26), frnd(-22, 22)}; pl.rot = {frnd(0, 6.28f), frnd(0, 6.28f), frnd(0, 6.28f)};
                pl.spin = {frnd(-9, 9), frnd(-9, 9), frnd(-9, 9)}; pl.size = {frnd(0.6f, 1.4f), frnd(0.08f, 0.2f), frnd(0.4f, 0.9f)};
                pl.life = 3.f; pl.c = k % 3 == 0 ? Color{185, 38, 30, 255} : Color{190, 190, 196, 255}; pl.hot = 1.f; break;
            }
            hitFlash_ = 1.f; shake_ = 2.f; flash_ = std::max(flash_, 0.45f);
            if (sndOk_ && sCrash_.frameCount) { SetSoundVolume(sCrash_, 1.f); PlaySound(sCrash_); }
            if (sndOk_) { StopSound(sRoar_); if (sScrape_.frameCount) StopSound(sScrape_); }
            audio.play(Sfx::BigExplosion, 1.f, 0.f);
            message("SHIP DESTROYED", 4.f, Color{255, 120, 90, 255});
            fprintf(stderr, "TRENCH blast death: still in the trench %.2f s after detonation y=%.1f\n", since, pl_.y);
        }
    }
    if (escape) {   // chain reaction: a fire front races down the trench after the ship, fire column from the port
        const float since = t - detT_;
        const Vector3 port = world(sPort_ + SEG * 0.5f, 0, 1.f);
        for (int i = 0; i < 6; i++) spawnFire(Vector3Add(port, {frnd(-4, 4), frnd(0, 6), frnd(-4, 4)}), {frnd(-8, 8), frnd(60, 140), frnd(-8, 8)}, frnd(4, 9), frnd(8, 16), frnd(1.f, 2.f), 0, Color{255, 190, 100, 255}, 0.5f, 10.f);
        // the chain reaction races after the ship; it catches a ship that is still down in the trench (t267)
        float lead = -28.f;
        if (deadT_ >= 0.f) lead = 10.f + (t - deadT_) * 150.f;
        else if (escapedT_ < 0.f) lead = Lerp(-28.f, 6.f, sstep(kBlastReach - 1.0f, kBlastReach, since));
        const float front = std::min(sPort_ + since * 290.f, pl_.s + lead);
        for (int i = 0; i < 16; i++) {
            const float sF = front - frnd(0, 60.f);
            spawnFire(world(sF, frnd(-W2 + 2, W2 - 2), frnd(1, DD + 6)), Vector3Add(Vector3Scale(fwd(sF), frnd(120, 240)), {0, frnd(10, 50), 0}), frnd(6, 13), frnd(12, 24), frnd(0.5f, 1.1f), 0, Color{255, 170, 80, 255}, 0.8f, 4.f);
        }
        for (int i = 0; i < 6; i++) {   // fire erupting out of the trench along the run
            const float sF = front - frnd(20.f, 400.f);
            spawnFire(world(sF, frnd(-W2, W2), DD + frnd(0, 4)), {frnd(-20, 20), frnd(60, 160), frnd(-20, 20)}, frnd(8, 16), frnd(16, 30), frnd(0.8f, 1.5f), 0, Color{255, 150, 60, 255}, 0.5f, 6.f);
        }
        if (since < 3.f) for (int i = 0; i < 4; i++) spawnFire(Vector3Add(port, {frnd(-30, 30), frnd(0, 30), frnd(-30, 30)}), {frnd(-30, 30), frnd(40, 120), frnd(-30, 30)}, frnd(10, 22), frnd(20, 40), frnd(0.8f, 1.6f), 0, Color{255, 210, 140, 255}, 0.3f, 6.f);
        if (since > 1.5f) for (int i = 0; i < 3; i++) {   // giant fireball dome behind the ship
            const float r = 40.f + since * 60.f; const Vector3 d = Vector3Normalize({frnd(-1, 1), frnd(0.05f, 1), frnd(-1, 1)});
            const Vector3 ctr = world(front - 120.f, 0, 0);   // the fireball rolls after the ship
            spawnFire(Vector3Add(ctr, Vector3Scale(d, r * frnd(0.7f, 1.f))), Vector3Add(Vector3Scale(d, 60.f), Vector3Scale(fwd(front), 180.f)), frnd(30, 60), 60.f, frnd(0.8f, 1.5f), 0, Color{255, 160, 70, 255}, 0.2f, 0.f);
        }
        shake_ = std::max(shake_, 0.5f);
    }
    // ---- systems
    updateTurrets(dt, t, audio);
    updateChasers(dt, t, audio, score);
    updateBolts(dt, damage, player.pos, score, audio);
    updateParts(dt);
    updateHull(dt, player);   // t267
    flash_ = std::max(0.f, flash_ - dt * 0.9f);
    hitFlash_ = std::max(0.f, hitFlash_ - dt * 2.5f);
    shake_ = std::max(std::max(0.f, shake_ - dt * 1.8f), shake * 0.3f);
    msgT_ -= dt;
    veil_ = 1.f - sstep(0.f, 0.7f, t);
    if (bombReady_ && !bombDropped_ && range < 1500.f) {   // targeting-computer beeps, faster as the range closes
        beepT_ -= dt;
        if (beepT_ <= 0) { beepT_ = Clamp(range / 1500.f, 0.12f, 1.f) * 0.9f; if (sndOk_ && sBeep_.frameCount) { SetSoundVolume(sBeep_, 0.4f); PlaySound(sBeep_); } }
    }
    if (bombDropped_ && deadT_ < 0.f && escapedT_ < 0.f && t > dropT_ + 0.4f && pl_.y < DD + 22.f) {   // t267: PULL UP warning tone
        pullBeepT_ -= dt;
        if (pullBeepT_ <= 0.f) { pullBeepT_ = 0.24f; if (sndOk_ && sBeep_.frameCount) { SetSoundVolume(sBeep_, 0.55f); SetSoundPitch(sBeep_, 1.6f); PlaySound(sBeep_); } }
    }
    if (IsKeyPressed(KEY_V)) forcedCam_ = forcedCam_ == 1 ? -1 : 1;
    if (getenv("TRENCH_TEST_LOG") && !loggedEvents_[11] && finaleT_ >= 0) { loggedEvents_[11] = true;
        fprintf(stderr, "TURRETSTAT final shots %d holdFrames %d hitsAbove %d hitsInside %d cutAtTopLine %d clipFix %d\n", turretShots_, turretHolds_, turretHitsAbove_, turretHitsInside_, turretBoltsCut_, clipFix_); }
    if (finaleT_ >= 0 && t >= finaleT_ && sndOk_) { StopSound(sRoar_); if (sScrape_.frameCount) StopSound(sScrape_); }
    if (enteredT_ < 0.f && pl_.y < DD - 1.f) {
        enteredT_ = t;
        fprintf(stderr, "TRENCH entered trench t=%.2f s=%.0f turretShots %d turretHitsBeforeEntry %d\n", t, pl_.s, turretShots_, turretHitsBeforeEntry_);
    }
    if (!loggedEvents_[0] && t > kEntryEnd) { loggedEvents_[0] = true; fprintf(stderr, "TRENCH entry phase over t=%.1f s=%.0f y=%.1f\n", t, pl_.s, pl_.y); }
    if (getenv("TRENCH_TEST_LOG")) {   // t267: weapon / damage stats
        if (t >= weapLogT_ || (!loggedEvents_[12] && finaleT_ >= 0)) {
            weapLogT_ = t + 5.f; if (finaleT_ >= 0) loggedEvents_[12] = true;
            fprintf(stderr, "WEAPSTAT t=%.1f primary=%d shots L%d P%d F%d M%d hits L%d P%d F%d M%d kills L%d P%d F%d M%d hull hits %d dmg %.2f spots %d holes %d plates %d\n", t, weapons.primary,
                    shotsBy_[0], shotsBy_[1], shotsBy_[2], shotsBy_[3], hitsBy_[0], hitsBy_[1], hitsBy_[2], hitsBy_[3], killsBy_[0], killsBy_[1], killsBy_[2], killsBy_[3],
                    hullHits_, hullDmg_, nSpots_, nHoles_, platesSpawned_);
        }
    }
    // t267: moon descent sound (engine hum, no plasma / wind); read by Game::updateFlightAudio
    g_trenchAudio.stamp = GetTime();
    g_trenchAudio.drone = 0.55f * descK;
    g_trenchAudio.roar = Clamp(0.22f + 0.55f * descShake_, 0.f, 1.f) * descK;
    g_trenchAudio.roarRate = 0.6f + 0.15f * descShake_;
    g_trenchAudio.musicDuck = 0.25f * descShake_;
    g_trenchAudio.engineDuck = 0.f;
}

// ------------------------------------------------------------------ camera
Camera3D TrenchRun::makeCamera(float t) {
    const bool shots = getenv("TRENCH_SHOTS") != nullptr;
    int mode = 0;   // 0 chase, 1 fpv, 2 rear, 3 detail
    if ((t > 40.f && t < 50.f) || (t > 64.f && t < 72.f)) mode = 1;
    if ((t > 24.6f && t < 27.4f) || (t > 57.6f && t < 59.8f) || (t > 76.f && t < 78.f)) mode = 2;
    if (shots && t > 21.0f && t < 21.5f) mode = 3;
    if (bombReady_ && !bombDropped_ && sPort_ - pl_.s < 600.f) mode = 1;   // targeting run: cockpit view
    if (bombDropped_) mode = 0;
    // t267: the exit is flown in the chase view (PULL UP!); the rear shot of the fire only once the ship is out
    if (escapedT_ >= 0 && t > escapedT_ + 0.6f) mode = 2;
    if (forcedCam_ >= 0 && detT_ < 0) mode = forcedCam_;
    camMode_ = (float)mode;
    const Vector3 F = fwd(pl_.s), R = right(pl_.s);
    Camera3D c{}; c.up = {0, 1, 0}; c.projection = CAMERA_PERSPECTIVE;
    float fov = 70.f + std::max(0.f, pl_.v - kTrenchSpeed) * 0.08f + shake_ * 3.f;
    if (mode == 0) {   // chase: behind and above, lags the bank a little
        const float lag = 0.10f;
        const Vector3 Fb = fwd(pl_.s - 12.f);
        (void)lag; Vector3 eye = Vector3Add(world(pl_.s - 15.f, pl_.u * 0.55f, std::min(pl_.y + 4.2f, std::max(pl_.y, DD) + 3.f)), Vector3Scale(Fb, 0.f));
        if (detT_ >= 0 && t > detT_) eye = Vector3Add(plPos_, Vector3Add(Vector3Scale(F, -17.f), {0, 3.f, 0}));
        c.position = eye; c.target = Vector3Add(plPos_, Vector3Add(Vector3Scale(F, 30.f), {0, 1.f, 0}));
        c.up = rotAxis({0, 1, 0}, F, pl_.bank * 0.22f);
        const float dEnd = descEnd();
        if (t < dEnd + 0.8f) {   // t267: the descent, shot like Planetfall: broadside, swinging round behind, buffet settling
            const float o = descDone_ ? std::max(sstep(0.3f, 4.2f, t), sstep(descDoneT_ - 0.4f, descDoneT_ + 0.8f, t)) : sstep(0.3f, 4.2f, t);
            const Vector3 Vd = Vector3Normalize(Vector3Add(Vector3Scale(R, cosf(o * PI * 0.5f)), Vector3Scale(F, -sinf(o * PI * 0.5f))));
            const float Dc = Lerp(30.f, 17.f, o);
            const Vector3 eyeP = Vector3Add(Vector3Add(plPos_, Vector3Scale(Vd, Dc)), {0, Dc * Lerp(-0.12f, 0.2f, o), 0});
            Vector3 tgtP = Vector3Add(plPos_, Vector3Add(Vector3Scale(F, Lerp(2.f, 30.f, o)), {0, Lerp(0.f, 1.f, o), 0}));
            const Vector3 cf = Vector3Normalize(Vector3Subtract(tgtP, eyeP));
            const Vector3 cr = Vector3Normalize(Vector3CrossProduct(cf, {0, 1, 0})), cu = Vector3CrossProduct(cr, cf);
            const float ja = descShake_ * 0.022f * Vector3Distance(eyeP, tgtP);   // angular jitter, as Planetfall's
            tgtP = Vector3Add(tgtP, Vector3Add(Vector3Scale(cr, ja * (sinf(t * 31.f) + 0.6f * sinf(t * 53.f + 1.3f))), Vector3Scale(cu, ja * (cosf(t * 37.f) + 0.5f * sinf(t * 61.f + 0.4f)))));
            const float k = descDone_ ? std::max(sstep(dEnd - 1.6f, dEnd + 0.6f, t), sstep(descDoneT_, descDoneT_ + 1.2f, t)) : sstep(dEnd - 1.6f, dEnd + 0.6f, t);
            c.position = Vector3Lerp(eyeP, c.position, k); c.target = Vector3Lerp(tgtP, c.target, k);
            c.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, c.up, k));
            fov = Lerp(Lerp(50.f, 70.f, o), fov, k);
        }
    } else if (mode == 1) {   // cockpit-forward FPV
        Vector3 U = rotAxis({0, 1, 0}, R, pl_.pitch); U = rotAxis(U, F, pl_.bank * 0.8f);
        const Vector3 Fp = rotAxis(F, R, pl_.pitch);
        c.position = Vector3Add(plPos_, Vector3Add(Vector3Scale(Fp, 1.6f), Vector3Scale(U, 0.6f)));
        c.target = Vector3Add(c.position, Vector3Scale(Fp, 50.f)); c.up = U; fov += 6.f;
    } else if (mode == 2) {   // rear shot: ahead of the ship looking back at it (and what chases it)
        c.position = Vector3Add(world(pl_.s + 22.f, pl_.u * 0.6f, pl_.y + 2.5f), {0, 0, 0});
        c.target = world(pl_.s - 30.f, pl_.u * 0.6f, pl_.y);
        if (detT_ >= 0 && t > detT_) { c.position = world(pl_.s + 30.f, 0, pl_.y + 6.f); c.target = world(pl_.s - 80.f, 0, std::max(DD, pl_.y - 30.f)); }
        fov = 62.f;
    } else {   // detail: close beside the wall greebles
        c.position = world(pl_.s - 4.f, (pl_.u > 0 ? -1.f : 1.f) * (W2 - 3.f), 15.f);
        c.target = world(pl_.s + 14.f, (pl_.u > 0 ? -1.f : 1.f) * (W2 + 0.f), 13.f);
        fov = 64.f;
    }
    if (shake_ > 0.01f) {
        const float a = shake_ * 0.5f;
        c.position = Vector3Add(c.position, {frnd(-a, a), frnd(-a, a), frnd(-a, a)});
    }
    fov_ = Lerp(fov_, fov, 0.2f);
    c.fovy = fov_;
    cam_ = c;
    return c;
}

// ------------------------------------------------------------------ draw
void TrenchRun::setTrenchUniform(float sAt) {
    const Matrix inv = MatrixInvert(frame(sAt));
    SetShaderValueMatrix(sh_, locTrenchInv_, inv);
}

void TrenchRun::drawShipModel(Model& m, Matrix M, float sAt, Color tint, float glow) {
    const int keep = sh_.locs[SHADER_LOC_VERTEX_TEXCOORD02];
    sh_.locs[SHADER_LOC_VERTEX_TEXCOORD02] = -1;   // models have no texcoord2 buffer
    for (int i = 0; i < m.materialCount; i++) m.materials[i].shader = sh_;
    const int z = 0; SetShaderValue(sh_, locUseAtlas_, &z, SHADER_UNIFORM_INT);
    const float hw = -1.f; SetShaderValue(sh_, locHalfW_, &hw, SHADER_UNIFORM_FLOAT);   // ships: sun shadow only from the trench test below
    float u, y; project(Vector3{M.m12, M.m13, M.m14}, sAt, u, y);
    const float shadowK = (y < DD && fabsf(u) < W2) ? 1.f : 0.f;
    (void)shadowK;
    const Vector3 tk{tint.r / 255.f, tint.g / 255.f, tint.b / 255.f};
    SetShaderValue(sh_, locTint_, &tk, SHADER_UNIFORM_VEC3); SetShaderValue(sh_, locGlow_, &glow, SHADER_UNIFORM_FLOAT);
    for (int i = 0; i < m.meshCount; i++) {
        if (m.meshes[i].triangleCount <= 0 || m.meshes[i].vertexCount <= 0) continue;
        DrawMesh(m.meshes[i], m.materials[m.meshMaterial[i]], MatrixMultiply(m.transform, M));
    }
    const float g0 = 0.f; SetShaderValue(sh_, locGlow_, &g0, SHADER_UNIFORM_FLOAT);
    const float hw2 = W2; SetShaderValue(sh_, locHalfW_, &hw2, SHADER_UNIFORM_FLOAT);
    sh_.locs[SHADER_LOC_VERTEX_TEXCOORD02] = keep;
}

void TrenchRun::beam(const Camera3D& c, Vector3 a, Vector3 b, float w, Color col) const {
    // camera-facing quad from a to b (additive)
    const Vector3 d = Vector3Subtract(b, a), toC = Vector3Subtract(c.position, Vector3Lerp(a, b, 0.5f));
    Vector3 side = Vector3CrossProduct(d, toC);
    const float L = Vector3Length(side); if (L < 1e-5f) return;
    side = Vector3Scale(side, w / L);
    rlSetTexture(soft_.id);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlTexCoord2f(0.5f, 0.f); rlVertex3f(a.x - side.x, a.y - side.y, a.z - side.z);
    rlTexCoord2f(0.5f, 1.f); rlVertex3f(a.x + side.x, a.y + side.y, a.z + side.z);
    rlTexCoord2f(0.5f, 1.f); rlVertex3f(b.x + side.x, b.y + side.y, b.z + side.z);
    rlTexCoord2f(0.5f, 0.f); rlVertex3f(b.x - side.x, b.y - side.y, b.z - side.z);
    rlEnd();
    rlSetTexture(0);
}

void TrenchRun::drawEnv(const Camera3D& c, float t) {
    const float tm = (float)GetTime();
    rlEnableDepthTest();
    SetShaderValue(sh_, locSun_, &sun_, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh_, locCam_, &c.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh_, locTime_, &tm, SHADER_UNIFORM_FLOAT);
    const Vector3 fog{0.028f, 0.028f, 0.042f}; SetShaderValue(sh_, locFog_, &fog, SHADER_UNIFORM_VEC3);
    const float fogK = 1.f / 1100.f, amb = 0.20f, spec = 0.35f, hw = W2, dd = DD, z = 0.f;
    SetShaderValue(sh_, locFogK_, &fogK, SHADER_UNIFORM_FLOAT); SetShaderValue(sh_, locAmb_, &amb, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locSpec_, &spec, SHADER_UNIFORM_FLOAT); SetShaderValue(sh_, locHalfW_, &hw, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locDepth_, &dd, SHADER_UNIFORM_FLOAT); SetShaderValue(sh_, locGlow_, &z, SHADER_UNIFORM_FLOAT);
    const Vector3 zv{0, 0, 0}; SetShaderValue(sh_, locTint_, &zv, SHADER_UNIFORM_VEC3);
    float fk = flash_ * 1.6f; Vector3 fp = world(sPort_ + SEG * 0.5f, 0, 6.f);
    if (detT_ >= 0 && t > detT_) { fk = 0.9f + flash_ * 2.f; fp = world(std::min(sPort_ + (t - detT_) * 290.f, pl_.s - 20.f), 0, 12.f); }
    else if (scrapeT_ > 0) { fk = 0.25f; fp = plPos_; }
    SetShaderValue(sh_, locFlash_, &fk, SHADER_UNIFORM_FLOAT); SetShaderValue(sh_, locFlashPos_, &fp, SHADER_UNIFORM_VEC3);
    const int one = 1; SetShaderValue(sh_, locUseAtlas_, &one, SHADER_UNIFORM_INT);
    // trench segments from behind the camera to the draw distance (t275: and under every visible surface tile beside the trench)
    float cu, cy; const float cs = project(c.position, pl_.s, cu, cy);
    long g0 = (long)floorf((cs - kSegDrawBack) / SEG), g1 = (long)floorf((cs + kSegDrawFwd) / SEG);
    struct TileDraw { float x0, z0, dist; int cls, v; long key; };
    static std::vector<TileDraw> tiles; tiles.clear();
    if (cy > DD - 2.f) {   // surface tiles on a world grid (only when the camera is above the rim): classify + cull first
        const Vector3 F = Vector3Normalize(Vector3Subtract(c.target, c.position));
        const float reach = kTileReachBase + std::max(0.f, cy - DD) * 2.f;
        const int ix0 = (int)floorf((c.position.x - reach) / kTile), ix1 = (int)floorf((c.position.x + reach) / kTile);
        const int iz0 = (int)floorf((c.position.z - reach) / kTile), iz1 = (int)floorf((c.position.z + reach) / kTile);
        int budget = 400;   // new far tiles classified per frame (no hitch when a big area comes into view at once)
        float visMin = 1e9f, visMax = -1e9f;
        for (int ix = ix0; ix <= ix1; ix++) for (int iz = iz0; iz <= iz1; iz++) {
            const float x0 = ix * kTile, z0 = (iz + 1) * kTile;   // tile spans x0..x0+T, z0-T..z0
            const Vector3 ctr{x0 + kTile * 0.5f, 0, z0 - kTile * 0.5f};
            const Vector3 to = Vector3Subtract(ctr, c.position);
            const float dist = Vector3Length(to);
            if (dist > reach) continue;
            if (dist > 90.f && Vector3DotProduct(to, F) < -kTile) continue;
            const long key = ((long)ix << 20) ^ (long)(iz & 0xFFFFF);
            auto it = tileCache_.find(key);
            int cls;
            if (it == tileCache_.end()) {   // 0 = clear of the trench (may have towers), 1 = near, 2 = beside the trench (clipped)
                if (dist > 300.f && budget <= 0) continue;   // next frame
                budget--;
                float minD = 1e9f, sMn = 1e9f, sMx = -1e9f;
                for (int a = 0; a < 5; a++) for (int b = 0; b < 5; b++) {
                    const Vector3 p{x0 + kTile * a / 4.f, 0, z0 - kTile * b / 4.f};
                    float u; const float s2 = nearestS(p, u);
                    if (s2 < kSMin + 30.f || s2 > kSMin + kSLen - 60.f) u = 1e4f;
                    minD = std::min(minD, fabsf(u));
                    if (fabsf(u) < W2 + 120.f) { sMn = std::min(sMn, s2); sMx = std::max(sMx, s2); }
                }
                cls = minD < W2 + 108.f ? 2 : minD < 150.f ? 1 : 0;   // 16 u sample spacing: the strip edge (W2 + 96) can fall between samples
                tileCache_[key] = cls;
                if (cls == 2) {
                    float u; float sc = nearestS(ctr, u);
                    if (sc < -1e8f) sc = 0.5f * (sMn + sMx);
                    const Vector3 C = path_.centre(sc);
                    const float k = (path_.heading(sc + 4.f) - path_.heading(sc - 4.f)) / 8.f;
                    tileClip_[key] = TileClip{C.x, C.z, path_.heading(sc), k, sMn, sMx};
                }
            } else cls = it->second;
            if (cls == 2) { const TileClip& tc = tileClip_[key]; visMin = std::min(visMin, tc.sMin); visMax = std::max(visMax, tc.sMax); }
            // Tablet: skip class-2 (beside-the-trench) tiles instead of discard-clipping them in the shader.
            if (kTrenchTablet && cls == 2) continue;
            const uint32_t h = hash32((uint32_t)(ix * 73856093) ^ (uint32_t)(iz * 19349663));
            const int v = cls == 0 && (h % 3 == 0) ? 3 + (h >> 4) % 3 : (h >> 4) % 3;
            tiles.push_back({x0, z0, dist, cls, v, key});
        }
        if (visMin < visMax) {   // the rim strip + trench must exist under every clipped tile in view (e.g. behind, after the escape climb)
            g0 = std::min(g0, (long)floorf((visMin - SEG) / SEG));
            g1 = std::max(g1, (long)floorf((visMax + SEG) / SEG));
        }
    }
    if (getenv("TRENCH_TEST_LOG")) {   // t275 test log: what the surface pass draws
        static float nextLog = 0.f;
        if (t >= nextLog) { nextLog = t + 1.f; int n[3] = {0, 0, 0}; for (const auto& td : tiles) n[td.cls]++;
            fprintf(stderr, "TILES t=%.1f cam s=%.0f y=%.1f tiles %d/%d/%d segs %.0f..%.0f cache %d\n", t, cs, cy, n[0], n[1], n[2], g0 * SEG, g1 * SEG, (int)tileCache_.size()); }
    }
    long lastFrameSeg = LONG_MIN;
    for (long g = g0; g <= g1; g++) {
        const float s0 = g * SEG;
        if (s0 < kSMin + 20.f || s0 > kSMin + kSLen - 40.f) continue;
        const int v = segVariant(g);
        const Variant& V = seg_[v];
        if (!V.ok) continue;
        // segment-local frame at its start (the bends are gentle: a 16 u segment is ~rigid)
        const Matrix M = frame(s0);
        if (g / 8 != lastFrameSeg) { lastFrameSeg = g / 8; setTrenchUniform(s0 + SEG * 4.f); }
        setTrenchUniform(s0 + SEG * 0.5f);
        const bool lod = fabsf(s0 - cs) > kSegLodDist;
        DrawMesh(lod && V.lod.vertexCount ? V.lod : V.full, matAtlas_, M);
    }
    if (!tiles.empty()) {
        const float hwN = -1.f; SetShaderValue(sh_, locHalfW_, &hwN, SHADER_UNIFORM_FLOAT);
        bool clipOn = false;
        for (const TileDraw& td : tiles) {
            if (td.cls == 2) {
                const TileClip& tc = tileClip_[td.key];
                const Vector4 A{tc.cx, tc.cz, tc.h, tc.k}; const Vector2 Wc{W2 + 3.2f, W2 + 96.f};
                SetShaderValue(sh_, locClipA_, &A, SHADER_UNIFORM_VEC4); SetShaderValue(sh_, locClipW_, &Wc, SHADER_UNIFORM_VEC2);
                clipOn = true;
            } else if (clipOn) { const Vector2 off{-1.f, -1.f}; SetShaderValue(sh_, locClipW_, &off, SHADER_UNIFORM_VEC2); clipOn = false; }
            const Variant& V = tile_[td.v];
            DrawMesh(td.dist > 380.f ? V.lod : V.full, matAtlas_, MatrixTranslate(td.x0, 0, td.z0));
        }
        if (clipOn) { const Vector2 off{-1.f, -1.f}; SetShaderValue(sh_, locClipW_, &off, SHADER_UNIFORM_VEC2); }
        const float hw2 = W2; SetShaderValue(sh_, locHalfW_, &hw2, SHADER_UNIFORM_FLOAT);
    }
    // turrets (round bases, rotating twin barrels)
    setTrenchUniform(pl_.s);
    SetShaderValue(sh_, locUseAtlas_, &one, SHADER_UNIFORM_INT);
    for (auto& tr : turrets_) {
        if (!tr.alive) continue;
        if (tr.s < cs - 30.f || tr.s > cs + 900.f) continue;
        setTrenchUniform(tr.s);
        const Vector3 base = world(tr.s, tr.u, tr.y);
        const float h0 = path_.heading(tr.s);
        DrawMesh(turretBase_, matAtlas_, MatrixMultiply(MatrixRotateY(-h0), MatrixTranslate(base.x, base.y, base.z)));
        const Matrix Hm = MatrixMultiply(MatrixRotateY(tr.yaw), MatrixTranslate(base.x, base.y + 1.95f, base.z));
        DrawMesh(turretHead_, matAtlas_, Hm);
        const Matrix Gm = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, 0, tr.recoil * 0.5f), MatrixRotateX(tr.pitch)), Hm);
        DrawMesh(turretGun_, matAtlas_, Gm);
    }
    // debris + bomb
    for (auto& d : debris_) if (d.alive) {
        const Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(d.size, d.size * 0.6f, d.size * 1.3f), MatrixRotateXYZ(d.rot)), MatrixTranslate(d.p.x, d.p.y, d.p.z));
        DrawMesh(cube_, matAtlas_, M);
    }
    if (bombDropped_ && detT_ < 0) {
        const Vector3 F = fwd(pl_.s);
        const Matrix M = MatrixMultiply(MatrixMultiply(MatrixRotateX(-0.5f), MatrixRotateY(-path_.heading(pl_.s))), MatrixTranslate(bombPos_.x, bombPos_.y, bombPos_.z));
        (void)F; DrawMesh(bombMesh_, matAtlas_, M);
    }
    // ships
    if (playerOk_ && deadT_ < 0.f && !(camMode_ > 0.5f && camMode_ < 1.5f)) {
        setTrenchUniform(pl_.s);
        // t267: the hits stay on the hull: scorched panels, glowing cores, missing chunks; the whole ship darkens with damage
        const Matrix MW = MatrixMultiply(player_.transform, plM_);
        float spot[32]{}, heat[8]{}, hole[8]{};
        for (int i = 0; i < nSpots_; i++) { const Vector3 w = Vector3Transform(spots_[i].local, MW); spot[i * 4] = w.x; spot[i * 4 + 1] = w.y; spot[i * 4 + 2] = w.z; spot[i * 4 + 3] = spots_[i].r; heat[i] = spots_[i].heat * 0.9f; }
        for (int i = 0; i < nHoles_; i++) { const Vector3 w = Vector3Transform(holeLocal_[i], MW); hole[i * 4] = w.x; hole[i * 4 + 1] = w.y; hole[i * 4 + 2] = w.z; hole[i * 4 + 3] = holeR_[i]; }
        if (nSpots_ > 0) { SetShaderValueV(sh_, locDmgSpot_, spot, SHADER_UNIFORM_VEC4, nSpots_); SetShaderValueV(sh_, locDmgHeat_, heat, SHADER_UNIFORM_FLOAT, nSpots_); }
        if (nHoles_ > 0) SetShaderValueV(sh_, locHole_, hole, SHADER_UNIFORM_VEC4, nHoles_);
        SetShaderValue(sh_, locDmgN_, &nSpots_, SHADER_UNIFORM_INT); SetShaderValue(sh_, locHoleN_, &nHoles_, SHADER_UNIFORM_INT);
        std::vector<Color> keep(player_.materialCount);
        const float dk = 1.f - 0.25f * hullDmg_;
        for (int i = 0; i < player_.materialCount; i++) { Color& mc = player_.materials[i].maps[MATERIAL_MAP_DIFFUSE].color; keep[i] = mc;
            mc = Color{(unsigned char)(mc.r * dk), (unsigned char)(mc.g * dk), (unsigned char)(mc.b * dk), mc.a}; }
        drawShipModel(player_, plM_, pl_.s, Color{255, 60, 40, 255}, hitFlash_ * 0.35f);
        for (int i = 0; i < player_.materialCount; i++) player_.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = keep[i];
        const int z0 = 0; SetShaderValue(sh_, locDmgN_, &z0, SHADER_UNIFORM_INT); SetShaderValue(sh_, locHoleN_, &z0, SHADER_UNIFORM_INT);
    }
    {   // t267: broken-off hull panels (plain lit plates, glowing while hot)
        const int z = 0; SetShaderValue(sh_, locUseAtlas_, &z, SHADER_UNIFORM_INT);
        const float hwN = -1.f; SetShaderValue(sh_, locHalfW_, &hwN, SHADER_UNIFORM_FLOAT);
        const Vector3 tk{1.f, 0.42f, 0.12f}; SetShaderValue(sh_, locTint_, &tk, SHADER_UNIFORM_VEC3);
        bool any = false;
        for (auto& pl : plates_) if (pl.alive) {
            any = true;
            const float g = pl.hot * 0.8f; SetShaderValue(sh_, locGlow_, &g, SHADER_UNIFORM_FLOAT);
            matPlain_.maps[MATERIAL_MAP_DIFFUSE].color = pl.c;
            const Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(pl.size.x, pl.size.y, pl.size.z), MatrixRotateXYZ(pl.rot)), MatrixTranslate(pl.p.x, pl.p.y, pl.p.z));
            DrawMesh(cube_, matPlain_, M);
        }
        if (any) { const float g0 = 0.f; SetShaderValue(sh_, locGlow_, &g0, SHADER_UNIFORM_FLOAT); }
        matPlain_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        const float hw2 = W2; SetShaderValue(sh_, locHalfW_, &hw2, SHADER_UNIFORM_FLOAT);
        const int one1 = 1; SetShaderValue(sh_, locUseAtlas_, &one1, SHADER_UNIFORM_INT);
    }
    { const Vector3 zv0{0, 0, 0}; SetShaderValue(sh_, locTint_, &zv0, SHADER_UNIFORM_VEC3); }
    if (chaserOk_) for (auto& ch : ch_) if (ch.alive) {
        Vector3 F = fwd(ch.s), R = right(ch.s), U{0, 1, 0};
        R = rotAxis(R, F, ch.bank); U = rotAxis(U, F, ch.bank);
        const Matrix B = basisM(Vector3Negate(R), U, F, ch.pos);
        const Matrix M = MatrixMultiply(MatrixMultiply(MatrixTranslate(-chaserC_.x, -chaserC_.y, -chaserC_.z), MatrixScale(chaserScale_, chaserScale_, chaserScale_)), B);
        setTrenchUniform(ch.s);
        drawShipModel(chaser_, M, ch.s, Color{255, 255, 255, 255}, 0.f);
    }
}

void TrenchRun::drawGlows(const Camera3D& c, float t) {
    const float tm = (float)GetTime();
    float cu, cy; const float cs = project(c.position, pl_.s, cu, cy);
    const Vector3 camF = Vector3Normalize(Vector3Subtract(c.target, c.position));
    const Vector3 camR = Vector3Normalize(Vector3CrossProduct(camF, c.up)), camU = Vector3CrossProduct(camR, camF);
    auto bill = [&](const Texture2D& tex, Vector3 p, float size, Color col, float rot = 0.f) {
        const float cr = cosf(rot), sr = sinf(rot);
        const Vector3 R = Vector3Add(Vector3Scale(camR, cr * size), Vector3Scale(camU, sr * size));
        const Vector3 U = Vector3Add(Vector3Scale(camR, -sr * size), Vector3Scale(camU, cr * size));
        rlSetTexture(tex.id); rlBegin(RL_QUADS); rlColor4ub(col.r, col.g, col.b, col.a);
        rlTexCoord2f(0, 0); rlVertex3f(p.x - R.x + U.x, p.y - R.y + U.y, p.z - R.z + U.z);
        rlTexCoord2f(0, 1); rlVertex3f(p.x - R.x - U.x, p.y - R.y - U.y, p.z - R.z - U.z);
        rlTexCoord2f(1, 1); rlVertex3f(p.x + R.x - U.x, p.y + R.y - U.y, p.z + R.z - U.z);
        rlTexCoord2f(1, 0); rlVertex3f(p.x + R.x + U.x, p.y + R.y + U.y, p.z + R.z + U.z);
        rlEnd(); rlSetTexture(0);
    };
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    // lamps on the segments: red blinkers out of phase, coils, port glow
    const long g0 = (long)floorf((cs - 30.f) / SEG), g1 = (long)floorf((cs + 700.f) / SEG);
    for (long g = g0; g <= g1; g++) {
        const int v = segVariant(g);
        const Matrix M = frame(g * SEG);
        for (const Lamp& L : seg_[v].lamps) {
            const Vector3 p = Vector3Transform(L.p, M);
            float a = 1.f;
            if (L.kind == 0) { const float ph = sinf(tm * 4.2f + L.phase + g * 1.7f); a = ph > 0.2f ? 1.f : 0.08f; }
            if (L.kind == 3) a = 0.55f + 0.15f * sinf(tm * 3.f + g);
            if (L.kind == 4) a = 0.75f + 0.25f * sinf(tm * 6.f);
            const float d = Vector3Distance(p, c.position);
            const float sz = L.size * (1.f + d * 0.004f);
            bill(soft_, p, sz, Fade(L.c, a * (L.kind == 4 ? 0.9f : 0.85f)));
            if (a > 0.5f && L.kind == 0) bill(soft_, p, sz * 0.35f, Fade(WHITE, 0.8f));
        }
    }
    if (cy > DD - 2.f) for (auto& kv : tileCache_) {   // tower lamps on the visible tiles
        if (kv.second == 2) continue;   // beside the trench (clipped)
        const int ix = (int)(kv.first >> 20), iz = (int)((kv.first << 44) >> 44);
        const uint32_t h = hash32((uint32_t)(ix * 73856093) ^ (uint32_t)(iz * 19349663));
        const int v = kv.second == 0 && (h % 3 == 0) ? 3 + (h >> 4) % 3 : (h >> 4) % 3;
        const Vector3 o{ix * kTile, 0, (iz + 1) * kTile};
        if (Vector3Distance(o, c.position) > 900.f) continue;
        for (const Lamp& L : tile_[v].lamps) {
            const Vector3 p = Vector3Add(L.p, o);
            const float ph = sinf(tm * 3.6f + L.phase + ix * 0.9f + iz * 1.3f);
            if (ph < 0.1f) continue;
            const float d = Vector3Distance(p, c.position);
            bill(soft_, p, L.size * (1.f + d * 0.006f), Fade(L.c, 0.9f));
        }
    }
    // turret telegraph glow
    for (auto& tr : turrets_) if (tr.alive && tr.tele > 0.01f) {
        const Vector3 head = Vector3Add(world(tr.s, tr.u, tr.y), {0, 1.95f, 0});
        const Vector3 dir{-sinf(tr.yaw) * cosf(tr.pitch), sinf(tr.pitch), -cosf(tr.yaw) * cosf(tr.pitch)};
        bill(soft_, Vector3Add(head, Vector3Scale(dir, 3.3f)), 1.2f + tr.tele * 1.8f, Fade(Color{120, 255, 120, 255}, tr.tele));
    }
    // ship nav lights + engine glow (player: port red / starboard green)
    if (playerOk_ && deadT_ < 0.f) {
        const Vector3 wl = Vector3Transform(pWingL_, MatrixMultiply(player_.transform, plM_)), wr = Vector3Transform(pWingR_, MatrixMultiply(player_.transform, plM_));
        const Vector3 nzL = Vector3Transform(pNozL_, MatrixMultiply(player_.transform, plM_));
        const Vector3 nzR = Vector3Transform(pNozR_, MatrixMultiply(player_.transform, plM_));
        for (int i = 0; i < nSpots_; i++) if (spots_[i].heat > 0.2f) {   // t267: hot damage cores
            const Vector3 w = Vector3Transform(spots_[i].local, MatrixMultiply(player_.transform, plM_));
            bill(soft_, w, 0.5f + 0.5f * spots_[i].heat, Fade(Color{255, 120, 40, 255}, 0.55f * spots_[i].heat * (0.8f + 0.2f * sinf(tm * 17.f + i))));
        }
        const float bl = sinf(tm * 5.f) > 0 ? 1.f : 0.25f;
        bill(soft_, wl, 0.55f, Fade(Color{255, 40, 30, 255}, bl)); bill(soft_, wr, 0.55f, Fade(Color{40, 255, 80, 255}, bl));
        (void)nzL; (void)nzR;
        // Twin engine flames + heat-wave mirage are drawn in screen space after EndMode3D.
    }
    for (auto& ch : ch_) if (ch.alive) {
        const Vector3 F = fwd(ch.s);
        bill(soft_, Vector3Add(ch.pos, Vector3Scale(F, -3.f)), 1.4f, Fade(Color{120, 170, 255, 255}, 0.75f));
    }
    // t267: plasma lance beam (pulsing core + halo + impact flare) and the homing missiles
    if (beamOn_) {
        const float pu = 0.85f + 0.15f * sinf(tm * 41.f);
        beam(c, beamA_, beamB_, 1.1f * pu, Fade(Color{110, 170, 255, 255}, 0.45f));
        beam(c, beamA_, beamB_, 0.42f * pu, Fade(Color{190, 225, 255, 255}, 0.8f));
        beam(c, beamA_, beamB_, 0.14f, Fade(WHITE, 0.95f));
        bill(soft_, beamA_, 1.3f * pu, Fade(Color{150, 200, 255, 255}, 0.8f));
        if (beamHit_) { bill(soft_, beamB_, 3.2f * pu, Fade(Color{150, 200, 255, 255}, 0.85f)); bill(soft_, beamB_, 1.2f, Fade(WHITE, 0.9f)); }
    }
    for (auto& m : mis_) if (m.alive) {
        const Vector3 d = Vector3Normalize(m.v);
        beam(c, Vector3Subtract(m.p, Vector3Scale(d, 3.f)), m.p, 0.35f, Fade(Color{255, 170, 90, 255}, 0.8f));
        bill(soft_, m.p, 1.1f, Fade(Color{255, 200, 130, 255}, 0.9f)); bill(soft_, m.p, 0.4f, WHITE);
    }
    // bolts: hot core + coloured halo
    for (auto& b : bolts_) if (b.alive) {
        const Vector3 d = Vector3Normalize(b.v);
        const Vector3 tail = Vector3Subtract(b.p, Vector3Scale(d, b.len));
        beam(c, tail, b.p, b.w * 3.f, Fade(b.c, 0.55f));
        beam(c, tail, b.p, b.w, Fade(Color{255, 255, 230, 255}, 0.9f));
    }
    // speed streaks: dust passing the camera
    if (!streakInit_) { for (auto& s : streaks_) s.p = Vector3Add(c.position, {frnd(-30, 30), frnd(-15, 15), frnd(-30, 30)}); streakInit_ = true; }
    const Vector3 vel = Vector3Scale(fwd(pl_.s), pl_.v);
    for (auto& s : streaks_) {
        Vector3 rel = Vector3Subtract(s.p, c.position);
        if (Vector3Length(rel) > 45.f || Vector3DotProduct(rel, camF) < -8.f) {
            s.p = Vector3Add(c.position, Vector3Add(Vector3Scale(camF, frnd(20, 45)), Vector3Add(Vector3Scale(camR, frnd(-18, 18)), Vector3Scale(camU, frnd(-10, 10)))));
            rel = Vector3Subtract(s.p, c.position);
        }
        const Vector3 tail = Vector3Add(s.p, Vector3Scale(vel, 0.035f));
        beam(c, s.p, tail, 0.05f, Fade(Color{200, 210, 230, 255}, 0.35f));
    }
    // fire / sparks (additive kinds)
    for (auto& p : parts_) if (p.alive && p.kind != 1) {
        const float k = p.life / p.max;
        if (p.kind == 2) { beam(c, p.p, Vector3Subtract(p.p, Vector3Scale(p.v, 0.03f)), p.size, Fade(Color{255, 220, 150, 255}, k)); continue; }
        Color col = p.kind == 3 ? p.c : mixc(Color{255, 80, 20, 255}, p.c, k * k);
        bill(fire_, p.p, p.size, Fade(col, std::min(1.f, k * 1.6f) * 0.8f), p.size * 0.3f + p.max);
    }
    EndBlendMode();
    BeginBlendMode(BLEND_ALPHA);   // smoke
    for (auto& p : parts_) if (p.alive && p.kind == 1) {
        const float k = p.life / p.max;
        bill(fire_, p.p, p.size, Fade(p.c, k * 0.55f), p.max * 2.f);
    }
    EndBlendMode();
    rlEnableDepthMask();
    (void)t;
}

Rectangle TrenchRun::dropButton(float W, float H, float sc) const {
    const float w = 230.f * sc, h = 74.f * sc;
    return {W * 0.5f - w * 0.5f, H - h - 26.f * sc, w, h};
}
bool TrenchRun::wantsDropTap(Vector2 p) const {
    if (!bombReady_ || bombDropped_) return false;
    return CheckCollisionPointRec(p, dropButton((float)GetScreenWidth(), (float)GetScreenHeight(), uiS_));
}

void TrenchRun::hud(const View& view, float t, float W, float H) {
    const float s = view.s;
    const Font& F = ui::text();
    auto centred = [&](const char* txt, float y, float size, Color c) {
        const Vector2 m = MeasureTextEx(F, txt, size, size * 0.08f);
        DrawTextEx(F, txt, {W * 0.5f - m.x * 0.5f + 2, y + 2}, size, size * 0.08f, Fade(BLACK, c.a / 255.f * 0.6f));
        DrawTextEx(F, txt, {W * 0.5f - m.x * 0.5f, y}, size, size * 0.08f, c);
    };
    if (veil_ > 0.001f) DrawRectangle(0, 0, (int)W, (int)H, Fade(Color{70, 70, 78, 255}, veil_));   // t267: a quick dark cut-in (no haze: no air here)
    if (hitFlash_ > 0.01f) {
        const float e = 70.f * s;
        DrawRectangleGradientH(0, 0, (int)e, (int)H, Fade(RED, hitFlash_ * 0.5f), BLANK);
        DrawRectangleGradientH((int)(W - e), 0, (int)e, (int)H, BLANK, Fade(RED, hitFlash_ * 0.5f));
    }
    if (t > 1.f && t < 4.5f) centred("MOON BASE", H * 0.18f, 64.f * s, Fade(Color{230, 235, 245, 255}, sstep(1.f, 1.6f, t) * (1.f - sstep(3.8f, 4.5f, t))));
    if (t > kSkimEnd && t < kEntryEnd + 1.5f) centred("INTO THE TRENCH!", H * 0.18f, 56.f * s, Fade(Color{255, 210, 120, 255}, sstep(kSkimEnd, kSkimEnd + 0.4f, t) * (1.f - sstep(kEntryEnd + 0.8f, kEntryEnd + 1.5f, t))));
    if (msgT_ > 0 && !msg_.empty()) centred(msg_.c_str(), H * 0.26f, 46.f * s, Fade(msgCol_, std::min(1.f, msgT_ * 2.f)));
    if (camMode_ > 1.5f && camMode_ < 2.5f && !(detT_ >= 0 && t > detT_)) centred("REAR VIEW", H * 0.06f, 26.f * s, Fade(Color{200, 210, 230, 255}, 0.7f));
    if (camMode_ > 0.5f && camMode_ < 1.5f) {   // FPV reticle
        const float cx = W * 0.5f, cy = H * 0.5f, r = 26.f * s;
        DrawCircleLines((int)cx, (int)cy, r, Fade(Color{255, 120, 90, 255}, 0.7f));
        DrawLineEx({cx - r * 2.f, cy}, {cx - r * 0.6f, cy}, 2.f * s, Fade(Color{255, 120, 90, 255}, 0.7f));
        DrawLineEx({cx + r * 0.6f, cy}, {cx + r * 2.f, cy}, 2.f * s, Fade(Color{255, 120, 90, 255}, 0.7f));
        // canopy frame edges
        DrawRectangleGradientV(0, 0, (int)W, (int)(H * 0.08f), Fade(BLACK, 0.55f), BLANK);
        DrawRectangleGradientV(0, (int)(H * 0.86f), (int)W, (int)(H * 0.14f), BLANK, Fade(BLACK, 0.6f));
    }
    const float range = sPort_ - pl_.s;
    if (bombReady_ && !bombDropped_ && range < 1500.f) {   // yellow wireframe targeting computer
        const float bw = 360.f * s, bh = 230.f * s, bx = W - bw - 30.f * s, by = H * 0.5f - bh * 0.5f;
        const Color Y{255, 220, 70, 230};
        DrawRectangle((int)bx, (int)by, (int)bw, (int)bh, Fade(BLACK, 0.55f));
        DrawRectangleLinesEx({bx, by, bw, bh}, 2.f * s, Y);
        const float cx = bx + bw * 0.5f, cy = by + bh * 0.46f;
        const float k = Clamp(range / 1500.f, 0.f, 1.f);
        for (int i = 0; i < 7; i++) {   // the trench in perspective, rushing toward the port
            const float z = fmodf(i / 7.f + (1.f - k) * 6.f, 1.f), sc = 0.12f + z * z * 0.9f;
            DrawRectangleLinesEx({cx - bw * 0.42f * sc, cy - bh * 0.32f * sc, bw * 0.84f * sc, bh * 0.64f * sc}, 1.5f, Fade(Y, 0.25f + 0.6f * z));
        }
        DrawLineEx({cx - bw * 0.42f, cy + bh * 0.32f}, {cx, cy}, 1.5f, Y); DrawLineEx({cx + bw * 0.42f, cy + bh * 0.32f}, {cx, cy}, 1.5f, Y);
        const float tw = (1.f - k) * bh * 0.3f + 4.f * s;
        DrawLineEx({cx - tw, cy}, {cx + tw, cy}, 3.f * s, range < 150.f ? RED : Y);
        char buf[48]; snprintf(buf, sizeof buf, "RANGE %05d", (int)std::max(0.f, range * 10.f));
        DrawTextEx(F, buf, {bx + 14 * s, by + bh - 34 * s}, 24 * s, 2, Y);
        DrawTextEx(F, "TARGETING", {bx + 14 * s, by + 8 * s}, 20 * s, 2, Y);
        if (range < 150.f && range > 15.f && fmodf(t * 4.f, 1.f) < 0.6f) centred("RELEASE!", H * 0.62f, 60.f * s, Color{255, 80, 60, 255});
    }
    if (bombReady_ && !bombDropped_) {   // DROP BOMB button (touch / click), B / Enter / missile button
        const Rectangle r = dropButton(W, H, s);
        const bool hot = range < 150.f && range > 15.f;
        DrawRectangleRounded(r, 0.3f, 8, Fade(hot ? Color{200, 40, 30, 255} : Color{60, 40, 20, 255}, 0.75f));
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 3.f * s, hot ? Color{255, 220, 120, 255} : Color{255, 190, 90, 200});
        const char* l = "DROP BOMB [B]"; const Vector2 m = MeasureTextEx(F, l, 30 * s, 2);
        DrawTextEx(F, l, {r.x + r.width * 0.5f - m.x * 0.5f, r.y + r.height * 0.5f - m.y * 0.5f}, 30 * s, 2, WHITE);
    }
    // hull bar
    if (t > kDiveEnd && detT_ < 0) {
        DrawRectangle((int)(30 * s), (int)(H - 40 * s), (int)(220 * s), (int)(12 * s), Fade(BLACK, 0.5f));
        DrawTextEx(F, "SPEED", {30 * s, H - 66 * s}, 20 * s, 2, Fade(WHITE, 0.7f));
        DrawRectangle((int)(30 * s), (int)(H - 40 * s), (int)(220 * s * Clamp(pl_.v / 250.f, 0.f, 1.f)), (int)(12 * s), Color{255, 170, 70, 220});
    }
    if (detT_ >= 0 && t > detT_) {   // escape: the fire at the screen edges
        const float since = t - detT_, k = sstep(0.f, 0.4f, since) * (1.f - sstep(5.f, 6.f, since)) * (camMode_ > 1.5f ? 0.5f : 1.f);
        const float e = (150.f + 70.f * sinf(t * 9.f)) * s;
        DrawRectangleGradientH(0, 0, (int)(e * 2), (int)H, Fade(Color{255, 120, 30, 255}, 0.8f * k), BLANK);
        DrawRectangleGradientH((int)(W - e * 2), 0, (int)(e * 2), (int)H, BLANK, Fade(Color{255, 120, 30, 255}, 0.8f * k));
        DrawRectangleGradientV(0, (int)(H - e * 2), (int)W, (int)(e * 2), BLANK, Fade(Color{255, 150, 50, 255}, 0.75f * k));
        if (since < 3.f && escapedT_ >= 0.f) centred("GET OUT OF THERE!", H * 0.2f, 52.f * s, Fade(Color{255, 230, 160, 255}, 1.f - sstep(2.3f, 3.f, since)));
    }
    // t267: controlled exit: PULL UP! until the ship is out of the trench; the bar is the time until the fire arrives
    if (bombDropped_ && deadT_ < 0.f && escapedT_ < 0.f && t > dropT_ + 0.4f && pl_.y < DD + 22.f) {
        const bool on = fmodf(t * 3.2f, 1.f) < 0.62f;
        const float left = detT_ >= 0.f ? Clamp(1.f - (t - detT_) / kBlastReach, 0.f, 1.f) : 1.f;
        const Color cc = left < 0.4f ? Color{255, 70, 50, 255} : Color{255, 215, 90, 255};
        if (on) centred("PULL UP!", H * 0.33f, 92.f * s, cc);
        const float bw = 420.f * s, bh = 14.f * s, bx = W * 0.5f - bw * 0.5f, by = H * 0.33f + 104.f * s;
        DrawRectangle((int)bx, (int)by, (int)bw, (int)bh, Fade(BLACK, 0.55f));
        DrawRectangle((int)bx, (int)by, (int)(bw * left), (int)bh, cc);
        const float ax = W * 0.5f, ay = by + 36.f * s;   // up arrows
        for (int i = 0; i < 2; i++) DrawTriangle({ax, ay - 18.f * s + i * 16.f * s}, {ax - 26.f * s, ay + 8.f * s + i * 16.f * s}, {ax + 26.f * s, ay + 8.f * s + i * 16.f * s}, Fade(cc, on ? 0.95f : 0.4f));
    }
    if (deadT_ >= 0.f) {   // caught by the blast
        const float k = sstep(0.f, 0.5f, t - deadT_);
        DrawRectangleGradientV(0, 0, (int)W, (int)H, Fade(Color{255, 110, 30, 255}, 0.35f * k), Fade(Color{255, 60, 20, 255}, 0.55f * k));
    }
    if (flash_ > 0.01f) DrawRectangle(0, 0, (int)W, (int)H, Fade(Color{255, 245, 220, 255}, std::min(1.f, flash_)));
}

void TrenchRun::draw(Render3D& r3d, const View& view, float t, float renderScale) {
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ClearBackground(Color{3, 3, 7, 255});
    Camera3D c = makeCamera(t);
    {   // sky: stars, nebula, the purple planet, the sun (the single light)
        Render3D::ApproachScene sc;
        sc.f = Vector3Normalize(Vector3Subtract(c.target, c.position)); sc.u = c.up;
        sc.sun = sun_; sc.pdir = Vector3Normalize({-0.42f, 0.16f, -0.89f});
        sc.fovy = c.fovy; sc.sunR = 3.2f * DEG2RAD; sc.planet = true; sc.dist = 2.45f; sc.atmoK = 0.65f;
        sc.tint = {210, 150, 240, 255}; sc.stars = 1.f; sc.nebula = 0.3f; sc.nebulaTint = {55, 35, 100, 255};
        sc.time = t; sc.starSpeed = 0.f;
        Render3D::ApproachOut out;
        r3d.drawApproachScenery(sc, out);
    }
    rlDrawRenderBatchActive();
    rlSetClipPlanes(0.4, 3000.0);
    BeginMode3D(c);
    drawEnv(c, t);
    drawGlows(c, t);
    EndMode3D();
    rlSetClipPlanes(RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    (void)renderScale;
    rlDrawRenderBatchActive();
    if (playerOk_ && deadT_ < 0.f) {
        // Same animated blue-white billboards + fast heat-wave mirage as Render3D gameplay,
        // sitting in the two engine nozzles. scale = playerScale_ so the plume is ~0.7 world
        // units long on the 7-unit trench ship (same fraction as the side-view fighter).
        r3d.clearPlumes();
        const Matrix W = MatrixMultiply(player_.transform, plM_);
        const Vector3 nL = Vector3Transform(pNozL_, W);
        const Vector3 nR = Vector3Transform(pNozR_, W);
        const Vector3 aftAxis = Vector3Subtract(Vector3Transform({1.f, 0.f, 0.f}, W), Vector3Transform({0.f, 0.f, 0.f}, W));
        const float aftLen = Vector3Length(aftAxis);
        const Vector3 aft1 = aftLen > 1e-6f ? Vector3Scale(aftAxis, 1.f / aftLen) : Vector3{1, 0, 0};
        const Vector2 sL = GetWorldToScreen(nL, c), sLb = GetWorldToScreen(Vector3Add(nL, aft1), c);
        const Vector2 sR = GetWorldToScreen(nR, c);
        const float unitPx = fmaxf(1.f, Vector2Distance(sL, sLb));
        const float angDeg = atan2f(sLb.y - sL.y, sLb.x - sL.x) * RAD2DEG;
        const float power = 1.f + std::max(0.f, pl_.v - kTrenchSpeed) / 80.f;
        r3d.addPlume(sL, angDeg, unitPx, playerScale_, power);
        r3d.addPlume(sR, angDeg, unitPx, playerScale_, power);
        r3d.drawHeatAndFlames();
    }
    rlDrawRenderBatchActive();
    rlDisableDepthTest();   // the HUD must not be depth-tested against the trench
    hud(view, t, W, H);
    rlDrawRenderBatchActive();
    // t267: leave the depth test OFF (raylib's 2D default): the game's HUD (life / shield / energy, FIRE button)
    // is drawn after this and was depth-failed against the trench before.
}

void TrenchRun::drawEscapeShip(const Camera3D& c, Vector3 moonCentre, float moonR, float since) {
    // the ship racing away from the exploding moon, toward the camera-left, engine glowing, fire trail behind
    const Vector3 F = Vector3Normalize(Vector3Subtract(c.target, c.position));
    const Vector3 R = Vector3Normalize(Vector3CrossProduct(F, c.up)), U = Vector3CrossProduct(R, F);
    const float k = Clamp(since / 4.f, 0.f, 1.f);
    const Vector3 toCam = Vector3Normalize(Vector3Subtract(c.position, moonCentre));
    const Vector3 start = Vector3Add(moonCentre, Vector3Scale(toCam, moonR * 1.15f));
    const Vector3 end = Vector3Add(Vector3Add(c.position, Vector3Scale(F, moonR * 0.6f)), Vector3Add(Vector3Scale(R, -moonR * 0.5f), Vector3Scale(U, -moonR * 0.1f)));
    const Vector3 p = Vector3Lerp(start, end, k * k * (3 - 2 * k));
    const float size = moonR * (0.012f + 0.05f * k);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    const Vector3 back = Vector3Normalize(Vector3Subtract(start, end));
    for (int i = 0; i < 10; i++) {
        const Vector3 q = Vector3Add(p, Vector3Scale(back, size * (1.5f + i * 1.6f)));
        DrawBillboard(c, fire_, q, size * (1.5f + i * 0.6f), Fade(Color{255, (unsigned char)(200 - i * 12), (unsigned char)(120 - i * 10), 255}, 0.6f * (1.f - i / 10.f)));
    }
    DrawBillboard(c, soft_, p, size * 2.f, Fade(Color{255, 190, 120, 255}, 0.9f));
    DrawBillboard(c, soft_, p, size * 0.8f, WHITE);
    EndBlendMode();
    rlEnableDepthMask();
}

bool TrenchRun::shot(const char* name) {
    const char* dir = getenv("TRENCH_SHOTS");
    if (!dir) return false;
    Image img = LoadImageFromScreen();
    if (img.width > 1280) ImageResize(&img, 1280, img.height * 1280 / img.width);
    char path[512]; snprintf(path, sizeof path, "%s/%s.png", dir, name);
    ExportImage(img, path); UnloadImage(img);
    fprintf(stderr, "TRENCH shot %s t=%.2f\n", path, lastT_);
    return true;
}

void TrenchRun::captures(float t) {
    // frames for the video
    if (const char* fd = getenv("TRENCH_FRAMES")) {
        static int every = getenv("TRENCH_FRAMES_EVERY") ? atoi(getenv("TRENCH_FRAMES_EVERY")) : 1;
        static float from = getenv("TRENCH_FRAMES_FROM") ? (float)atof(getenv("TRENCH_FRAMES_FROM")) : 0.f;
        static int n = 0, k = 0;
        if (t >= from && (k++ % std::max(1, every)) == 0) {
            Image img = LoadImageFromScreen();
            ImageResize(&img, 1280, 720);
            char path[512]; snprintf(path, sizeof path, "%s/f%05d.jpg", fd, n++);
            ExportImage(img, path); UnloadImage(img);
        }
    }
    if (const char* sa = getenv("TRENCH_SNAP_AT")) {   // test hook: full-size snapshots at exact trench times
        static int done = 0; int i = 0;
        for (const char* p = sa; *p; i++) {
            const float ts = (float)atof(p);
            if (!(done & (1 << i)) && t >= ts) {
                done |= 1 << i;
                const char* dir = getenv("TRENCH_SHOTS") ? getenv("TRENCH_SHOTS") : ".";
                char path[512]; snprintf(path, sizeof path, "%s/snap_%05.1f.png", dir, ts);
                { Image im = LoadImageFromScreen(); ExportImage(im, path); UnloadImage(im); } fprintf(stderr, "TRENCH snap %s u=%.2f y=%.2f roll=%.1f\n", path, pl_.u, pl_.y, pl_.bank * RAD2DEG);
            }
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
    }
    if (!getenv("TRENCH_SHOTS")) return;
    static bool done[32]{};
    auto once = [&](int i, bool cond, const char* name) { if (!done[i] && cond) { done[i] = shot(name); } };
    bool passing = false, along = false, behind = 0;
    for (auto& c : ch_) if (c.alive && c.crashT < 0) {
        const float rel = c.s - pl_.s;
        if (rel > -2.f && rel < 14.f && c.vs > pl_.v + 20.f) passing = true;
        if (fabsf(rel) < 6.f && c.mode == 2) along = true;
        if (rel < -15.f) behind = true;
    }
    bool turretNear = false; for (auto& tr : turrets_) if (tr.alive && tr.s - pl_.s > 30.f && tr.s - pl_.s < 160.f) turretNear = true;
    once(1, t > 2.5f, "01_dive");
    once(2, t > 9.f, "02_skim");
    once(3, t > kEntryEnd - 0.6f, "03_entry");
    once(4, t > 19.f && turretNear && camMode_ < 0.5f, "04_trench_turrets");
    once(5, camMode_ > 2.5f && t > 21.2f, "05_detail");
    once(6, camMode_ > 1.5f && camMode_ < 2.5f && behind && t < 30.f, "06_rear_chasers");
    once(7, passing && camMode_ < 1.5f, "07_passby");
    once(8, along && camMode_ < 1.5f, "08_alongside");
    once(9, hitFlash_ > 0.8f && scrapeT_ > 0.f && t > kEntryEnd, "09_wall_crash");
    once(10, camMode_ > 0.5f && camMode_ < 1.5f && t > 42.f && turretNear, "10_fpv");
    once(11, bombReady_ && readyT_ >= 0 && t > readyT_ + 0.6f, "11_bomb_ready");
    once(12, bombReady_ && !bombDropped_ && sPort_ - pl_.s < 140.f, "12_targeting_release");
    once(13, bombDropped_ && dropT_ >= 0 && t > dropT_ + 0.45f, "13_bomb_drop");
    once(14, detT_ >= 0 && t > detT_ + 0.25f, "14_detonation");
    once(15, detT_ >= 0 && t > detT_ + 1.6f, "15_escape_fire");
    once(16, detT_ >= 0 && t > detT_ + 4.0f, "16_rear_burning");
    // t267 proof shots
    bool misNear = false; for (auto& m : mis_) if (m.alive && Vector3Distance(m.p, plPos_) > 25.f && Vector3Distance(m.p, plPos_) < 120.f) misNear = true;
    once(20, bombDropped_ && detT_ >= 0 && t > detT_ + 0.7f && escapedT_ < 0.f && deadT_ < 0.f, "20_pull_up");
    once(21, deadT_ >= 0.f && t > deadT_ + 0.7f, "21_blast_death");
    once(22, nHoles_ >= 1 && camMode_ < 0.5f && t > 1.f, "22_hull_damage");
    once(23, beamHit_ && camMode_ < 0.5f, "23_plasma_hit");
    once(24, flameTick_ > 0.f && camMode_ < 0.5f && hitsBy_[2] > 3, "24_flame");
    once(25, misNear && camMode_ < 0.5f, "25_missiles");
    once(26, t > 1.2f && t < 2.0f, "00a_descent_side");
    once(27, t > 3.4f && t < 4.2f, "00b_descent_turn");
    once(28, enteredT_ >= 0.f && t > enteredT_ + 0.4f && t < kSkimEnd, "03a_early_entry");
    once(29, hullHits_ >= 1 && camMode_ < 0.5f && nHoles_ == 0 && t > kDiveEnd, "22a_first_hits");
}

void TrenchRun::toast(const char* m) { message(m, 1.8f, Color{190, 230, 255, 255}); }
