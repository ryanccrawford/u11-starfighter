#include "vfx.hpp"
#include "entities.hpp"
#include "config.hpp"
#include "rlgl.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using nlohmann::json;

namespace {
// Three.js Color.set(hex).convertLinearToSRGB(): the hex is treated as linear and encoded to sRGB (brightens)
float toSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f; }
Vfx::Col hexCol(const json& j, const char* k, const char* def) {
    std::string h = j.value(k, std::string(def));
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    unsigned v = (unsigned)strtoul(h.c_str(), nullptr, 16);
    return {toSrgb(((v >> 16) & 255) / 255.f), toSrgb(((v >> 8) & 255) / 255.f), toSrgb((v & 255) / 255.f)};
}
json loadJson(const std::string& path) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "VFX missing %s (defaults)\n", path.c_str()); return json::object(); }
    try { return json::parse(f); } catch (const std::exception& e) { fprintf(stderr, "VFX bad %s: %s\n", path.c_str(), e.what()); return json::object(); }
}
#define VS_HDR "#version 330\nin vec2 fragTexCoord; in vec4 fragColor; out vec4 o;\n"
// progress rides in fragColor.r (and .g); colours/gains are uniforms
// ONE shader for every effect quad (fewer batch flushes on Panfork): fragColor.b*255 = layer id
// (0 blast, 1 shock ring, 2 impact flash, 3 impact scorch+ring; +4 = warm impact palette), r/g = progress.
const char* FX_FS = VS_HDR R"(uniform vec3 coreColor, fireColor, outerColor, ringColor; uniform float blastGain, turbulence, thickness, ringGain;
uniform vec3 flashColor, plasmaColor, plasmaWarm, scorchColor, iRingColor, iRingWarm; uniform float flashGain, sharp, scorchGain, iThick;
void main(){ int id = int(fragColor.b * 255.0 + 0.5); bool warm = id >= 4; if (warm) id -= 4;
  vec2 p = fragTexCoord - 0.5; float r = length(p) * 2.0; if (r > 1.0) discard;
  vec3 col; float a;
  if (id == 0) { float progress = fragColor.r; float angle = atan(p.y, p.x);
    float noise = sin(angle * 7.0 + progress * 8.0) * 0.12 + cos(angle * 13.0 - progress * 5.0) * 0.08;
    float distorted = r - noise * turbulence * 0.15 * (1.0 - progress);
    float edge = 1.0 - smoothstep(0.0, 1.0, distorted);
    float core = 1.0 - smoothstep(0.0, 0.45, distorted * (1.0 + progress * 2.5));
    col = mix(outerColor, fireColor, smoothstep(0.2, 0.7, edge)); col = mix(col, coreColor, core);
    a = edge * (1.0 - smoothstep(0.4, 1.0, progress)) * blastGain * fragColor.a;
  } else if (id == 1) { float progress = fragColor.r;
    float ring = exp(-pow(r - progress, 2.0) / max(0.0005, thickness * 0.04));
    a = ring * (1.0 - smoothstep(0.6, 1.0, progress)) * ringGain * fragColor.a; col = ringColor;
  } else if (id == 2) { float q = fragColor.r;
    float core = exp(-r * sharp * (1.0 + q * 2.0)); float halo = exp(-r * sharp * 0.28 * (1.0 + q));
    col = mix(warm ? plasmaWarm : plasmaColor, flashColor, core); a = (core + halo * 0.6) * (1.0 - q) * flashGain;
  } else { float ringP = fragColor.r, scorchP = fragColor.g;
    float scorch = exp(-r * r * 8.0) * (1.0 - scorchP) * scorchGain;
    float ring = exp(-pow(r - ringP, 2.0) / max(0.0005, iThick * 0.04)) * (1.0 - ringP);
    col = scorch * scorchColor + ring * (warm ? iRingWarm : iRingColor); a = clamp(scorch + ring, 0.0, 1.0);
    o = vec4(col * a, a); return; }
  o = vec4(col * a, clamp(a, 0.0, 1.0)); })";

void setV3(Shader& sh, const char* n, Vfx::Col c) { float v[3] = {c.r, c.g, c.b}; SetShaderValue(sh, GetShaderLocation(sh, n), v, SHADER_UNIFORM_VEC3); }
void setF(Shader& sh, const char* n, float f) { SetShaderValue(sh, GetShaderLocation(sh, n), &f, SHADER_UNIFORM_FLOAT); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f); return t * t * (3 - 2 * t); }
unsigned char u8(float v) { return (unsigned char)std::clamp(v * 255.f + 0.5f, 0.f, 255.f); }
constexpr int kSlot = 64;          // atlas: 3 soft discs (ember / smoke / spark falloffs)
constexpr int kMaxPts = 3200;      // hard cap on all live point sprites
// Adaptive detail: below ~55 FPS spawn fewer embers/sparks (down to 35%) and drop the (near-invisible) smoke.
float loadQ() { int f = GetFPS(); return f <= 0 ? 1.f : std::clamp((f - 30) / 25.f, 0.35f, 1.f); }
}  // namespace

void Vfx::init(const char* dir) {
    const std::string d = dir;
    json e = loadJson(d + "/fx/enemy_explosion.json"), i = loadJson(d + "/fx/impact_damage.json");
    E_.blastRadius = e.value("blastRadius", 2.4f); E_.duration = e.value("duration", 0.85f); E_.blastGain = e.value("blastGain", 1.3f);
    E_.turbulence = e.value("turbulence", 4.5f); E_.ringRadius = e.value("ringRadius", 3.8f); E_.ringThickness = e.value("ringThickness", 0.08f);
    E_.ringGain = e.value("ringGain", 1.1f); E_.ringSpeed = e.value("ringSpeed", 7.5f); E_.debrisCount = e.value("debrisCount", 160);
    E_.dSpMin = e.value("debrisSpeedMin", 2.5f); E_.dSpMax = e.value("debrisSpeedMax", 9.5f); E_.dSzMin = e.value("debrisSizeMin", 2.f);
    E_.dSzMax = e.value("debrisSizeMax", 6.f); E_.dLifeMin = e.value("debrisLifeMin", 0.35f); E_.dLifeMax = e.value("debrisLifeMax", 1.1f);
    E_.dDrag = e.value("debrisDrag", 1.8f); E_.debrisGain = e.value("debrisGain", 1.2f); E_.smokeCount = e.value("smokeCount", 26);
    E_.smokeSize = e.value("smokeSize", 1.8f); E_.smokeOpacity = e.value("smokeOpacity", 0.45f); E_.smokeLife = e.value("smokeLife", 1.4f);
    E_.shipMul = e.value("shipBlastMul", 2.25f); E_.pointScale = e.value("pointScale", 1.f);
    E_.core = hexCol(e, "coreColor", "#ffffff"); E_.fire = hexCol(e, "fireColor", "#ff7a18"); E_.outer = hexCol(e, "outerColor", "#b51806");
    E_.ring = hexCol(e, "ringColor", "#ffbb55"); E_.ember = hexCol(e, "emberColor", "#ff4d00"); E_.hot = hexCol(e, "hotEmberColor", "#fffae0");
    E_.smoke = hexCol(e, "smokeColor", "#1b2330");
    I_.flashRadius = i.value("flashRadius", 1.2f); I_.flashDuration = i.value("flashDuration", 0.22f); I_.flashGain = i.value("flashGain", 1.4f);
    I_.sharp = i.value("flashSharpness", 18.f); I_.sparkCount = i.value("sparkCount", 120); I_.spMin = i.value("sparkSpeedMin", 3.5f);
    I_.spMax = i.value("sparkSpeedMax", 14.f); I_.spread = i.value("sparkSpread", 140.f) * DEG2RAD; I_.angle = i.value("sparkAngle", 180.f) * DEG2RAD;
    I_.szMin = i.value("sparkSizeMin", 1.5f); I_.szMax = i.value("sparkSizeMax", 5.5f); I_.lifeMin = i.value("sparkLifeMin", 0.15f);
    I_.lifeMax = i.value("sparkLifeMax", 0.55f); I_.sparkGain = i.value("sparkGain", 1.3f); I_.drag = i.value("sparkDrag", 2.2f);
    I_.scorchDuration = i.value("scorchDuration", 1.6f); I_.scorchGain = i.value("scorchGain", 0.9f); I_.ringRadius = i.value("ringRadius", 1.6f);
    I_.ringThickness = i.value("ringThickness", 0.06f); I_.ringSpeed = i.value("ringSpeed", 6.5f);
    I_.shipMul = i.value("shipFlashMul", 0.6f); I_.pointScale = i.value("pointScale", 1.f); I_.maxSparks = i.value("maxSparks", 900);
    I_.flash = hexCol(i, "flashColor", "#ffffff"); I_.plasma = hexCol(i, "plasmaColor", "#0fe6ff"); I_.spark = hexCol(i, "sparkColor", "#0a85ff");
    I_.hot = hexCol(i, "hotSparkColor", "#e0f7ff"); I_.scorch = hexCol(i, "scorchColor", "#ff5500"); I_.ring = hexCol(i, "ringColor", "#3cd7ff");

    fx_ = LoadShaderFromMemory(nullptr, FX_FS);
    setV3(fx_, "coreColor", E_.core); setV3(fx_, "fireColor", E_.fire); setV3(fx_, "outerColor", E_.outer);
    setF(fx_, "blastGain", E_.blastGain); setF(fx_, "turbulence", E_.turbulence);
    setV3(fx_, "ringColor", E_.ring); setF(fx_, "thickness", E_.ringThickness); setF(fx_, "ringGain", E_.ringGain);
    // impacts: player bolts hitting enemies keep Ryan's cyan; enemy shots hitting the player use a warm palette
    setV3(fx_, "flashColor", I_.flash); setV3(fx_, "plasmaColor", I_.plasma); setV3(fx_, "plasmaWarm", {1.f, 0.42f, 0.12f});
    setF(fx_, "flashGain", I_.flashGain); setF(fx_, "sharp", I_.sharp);
    setV3(fx_, "scorchColor", I_.scorch); setV3(fx_, "iRingColor", I_.ring); setV3(fx_, "iRingWarm", {1.f, 0.62f, 0.3f});
    setF(fx_, "scorchGain", I_.scorchGain); setF(fx_, "iThick", I_.ringThickness);
    // point-sprite atlas, premultiplied-style (rgb = alpha = falloff) so tint*alpha reproduces col * a^2 of the reference
    Image img = GenImageColor(kSlot * 3, kSlot, BLANK);
    auto* px = (Color*)img.data;
    const float e0[3] = {0.1f, 0.f, 0.05f};
    for (int k = 0; k < 3; k++)
        for (int y = 0; y < kSlot; y++)
            for (int x = 0; x < kSlot; x++) {
                float dx = (x + 0.5f) / kSlot - 0.5f, dy = (y + 0.5f) / kSlot - 0.5f, r = std::sqrt(dx * dx + dy * dy) * 2;
                float f = r > 1 ? 0 : 1 - smooth(e0[k], 1, r);
                px[y * kSlot * 3 + k * kSlot + x] = {u8(f), u8(f), u8(f), u8(f)};
            }
    atlas_ = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(atlas_, TEXTURE_FILTER_BILINEAR);
    Image wi = GenImageColor(1, 1, WHITE); white_ = LoadTextureFromImage(wi); UnloadImage(wi);
    pts_.reserve(kMaxPts);
    ok_ = fx_.id != 0;
    fprintf(stderr, "VFX %s (explosion + impact shaders)\n", ok_ ? "ready" : "FAILED");
}

void Vfx::shutdown() {
    if (!ok_) return;
    UnloadShader(fx_);
    UnloadTexture(atlas_); UnloadTexture(white_); ok_ = false;
}

void Vfx::clear() { for (auto& x : ex_) x.on = false; for (auto& x : im_) x.on = false; pts_.clear(); shards_.clear(); nSparks_ = 0; }

void Vfx::explode(Vector2 pos, float shipPx, float gain) {
    if (!ok_) return;
    Explosion* x = &ex_[0];
    for (auto& c : ex_) { if (!c.on) { x = &c; break; } if (c.prog > x->prog) x = &c; }  // free slot, else the oldest
    *x = {true, pos, E_.shipMul * shipPx / (2 * E_.blastRadius), 0, 0, gain};
    const float K = x->K, ps = s_ * E_.pointScale;
    const int room = kMaxPts - (int)pts_.size();
    const float q = loadQ();
    const bool smoke = !gfx::low && q > 0.7f;
    const int nd = std::min((int)(E_.debrisCount * q * gain), std::max(0, room - (smoke ? E_.smokeCount : 0)));
    for (int i = 0; i < nd; i++) {
        float a = frand(0, 2 * PI), sp = lerp(E_.dSpMin, E_.dSpMax, frand(0, 1)) * K;
        pts_.push_back({pos, {std::cos(a) * sp, std::sin(a) * sp}, 1, lerp(E_.dLifeMin, E_.dLifeMax, frand(0, 1)),
                        lerp(E_.dSzMin, E_.dSzMax, frand(0, 1)) * ps, E_.dDrag, 0});
    }
    if (smoke)  // smoke is near-invisible (dark + additive, as in the reference) but costs big quads: skipped on Low
        for (int i = 0; i < (int)(E_.smokeCount * gain) && (int)pts_.size() < kMaxPts; i++) {
            float a = frand(0, 2 * PI), sp = frand(0.3f, 1.8f) * K;
            pts_.push_back({{pos.x + frand(-0.1f, 0.1f) * K, pos.y + frand(-0.1f, 0.1f) * K}, {std::cos(a) * sp, std::sin(a) * sp}, 1,
                            E_.smokeLife * frand(0.8f, 1.2f), E_.smokeSize * frand(20, 50) * ps, 0, 1});
        }
}

void Vfx::impact(Vector2 hitLocal, Vector2 dir, float shipPx, const Vector2* anchor, const void* owner, bool warm) {
    if (!ok_) return;
    for (auto& c : im_)  // constant fire: max one new impact per ship per ~70 ms; extra hits re-flash the newest
        if (c.on && c.owner == owner && owner && c.scorchProg * I_.scorchDuration < 0.07f) { c.prog = 0; return; }
    Imp* x = &im_[0];
    for (auto& c : im_) { if (!c.on) { x = &c; break; } if (c.scorchProg > x->scorchProg) x = &c; }
    const Vector2 off = anchor ? Vector2{hitLocal.x - anchor->x, hitLocal.y - anchor->y} : hitLocal;
    *x = {true, off, toScreen(hitLocal), anchor, owner, I_.shipMul * shipPx / (2 * I_.flashRadius), 0, 0, 0, warm};
    // sparks: spray back out of the impact (sparkAngle 180 = reversed shot direction) +/- spread/2.
    // Constant fire -> adaptive: the count shrinks as the live-spark budget fills.
    const float budget = std::clamp(1.f - (float)nSparks_ / I_.maxSparks, 0.f, 1.f);
    const int n = std::min((int)(I_.sparkCount * budget * loadQ()), kMaxPts - (int)pts_.size());
    const float base = std::atan2(dir.y, dir.x) + I_.angle, K = x->K, ps = s_ * I_.pointScale;
    for (int i = 0; i < n; i++) {
        float a = base + frand(-0.5f, 0.5f) * I_.spread, sp = lerp(I_.spMin, I_.spMax, frand(0, 1)) * K;
        pts_.push_back({x->p, {std::cos(a) * sp, std::sin(a) * sp}, 1, lerp(I_.lifeMin, I_.lifeMax, frand(0, 1)),
                        lerp(I_.szMin, I_.szMax, frand(0, 1)) * ps, I_.drag, (unsigned char)(warm ? 3 : 2)});
    }
}

void Vfx::follow() { for (auto& x : im_) if (x.on && x.anchor) x.p = toScreen({x.anchor->x + x.off.x, x.anchor->y + x.off.y}); }

void Vfx::shards(Vector2 pos, Vector2 dir, Color hull, const ShardCfg& c) {
    if (!ok_) return;
    const float base = std::atan2(dir.y, dir.x);
    const bool aimed = dir.x != 0 || dir.y != 0;
    for (int i = 0; i < c.count && (int)shards_.size() < c.maxShards; i++) {
        float a = aimed ? base + frand(-1.1f, 1.1f) : frand(0, 2 * PI), sp = frand(c.speedMin, c.speedMax) * s_;
        shards_.push_back({pos, {std::cos(a) * sp, std::sin(a) * sp}, frand(0, 2 * PI), frand(-14, 14), frand(c.sizeMin, c.sizeMax) * s_,
                           1, c.life * frand(0.7f, 1.2f), c.drag, hull, {frand(0.6f, 1.f), frand(0.4f, 0.9f), frand(0.5f, 1.f)}});
    }
}

void Vfx::detach(const void* owner) { for (auto& x : im_) if (x.on && x.owner == owner) x.on = false; }

void Vfx::update(float dt, float s) {
    s_ = s;
    if (!ok_ || dt <= 0) return;
    dt = std::min(dt, 0.05f);
    for (auto& x : ex_) {
        if (!x.on) continue;
        x.prog = std::min(1.f, x.prog + dt / std::max(0.01f, E_.duration));
        x.ringProg = std::min(1.f, x.ringProg + dt * E_.ringSpeed / std::max(0.1f, E_.ringRadius));
        if (x.prog >= 1 && x.ringProg >= 1) x.on = false;  // its points live on independently
    }
    for (auto& x : im_) {
        if (!x.on) continue;
        x.prog = std::min(1.f, x.prog + dt / std::max(0.01f, I_.flashDuration));
        x.ringProg = std::min(1.f, x.ringProg + dt * I_.ringSpeed / std::max(0.1f, I_.ringRadius));
        x.scorchProg = std::min(1.f, x.scorchProg + dt / std::max(0.01f, I_.scorchDuration));
        if (x.anchor) x.p = toScreen({x.anchor->x + x.off.x, x.anchor->y + x.off.y});  // ride the ship
        if (x.prog >= 1 && x.ringProg >= 1 && x.scorchProg >= 1) x.on = false;
    }
    {   size_t w = 0;
        for (auto q : shards_) {
            q.life -= dt / q.dur;
            if (q.life <= 0) continue;
            float f = std::exp(-q.drag * dt); q.v.x *= f; q.v.y *= f;
            q.p.x += q.v.x * dt; q.p.y += q.v.y * dt; q.ang += q.spin * dt;
            shards_[w++] = q;
        }
        shards_.resize(w);
    }
    nSparks_ = 0;
    size_t w = 0;
    for (size_t i = 0; i < pts_.size(); i++) {
        Pt q = pts_[i];
        q.life -= dt / q.dur;
        if (q.life <= 0) continue;
        if (q.drag > 0) { float f = std::exp(-q.drag * dt); q.v.x *= f; q.v.y *= f; }
        q.p.x += q.v.x * dt; q.p.y += q.v.y * dt;
        if (q.kind >= 2) nSparks_++;
        pts_[w++] = q;
    }
    pts_.resize(w);
}

void Vfx::draw() const {
    if (!ok_) return;
    static const int dbg = getenv("VFXDBG") ? atoi(getenv("VFXDBG")) : 0;  // perf bisect: 1 no points, 2 no smoke, 4 no shader quads, 8 no impacts
    const Rectangle src{0, 0, 1, 1};
    auto quad = [&](Vector2 p, float size, Color c) { size = std::max(size, 0.01f); DrawTexturePro(white_, src, {p.x, p.y, size, size}, {size / 2, size / 2}, 0, c); };
    if (!shards_.empty() && !(dbg & 16)) {  // shards: normal alpha, one triangle batch, drawn under the additive glow
        rlDrawRenderBatchActive(); rlDisableBackfaceCulling();  // spinning 2D triangles: either winding
        rlSetTexture(white_.id); rlBegin(RL_TRIANGLES);
        for (const Shard& q : shards_) {
            const float hot = std::clamp((q.life - 0.6f) / 0.4f, 0.f, 1.f), fade = std::min(1.f, q.life / 0.4f);
            const Color hc = q.hull;
            const unsigned char A = u8(fade);
            // vertex 0 is the torn, ember-hot edge (cools to dark hull), the others hull-coloured
            const unsigned char er = u8(lerp(hc.r / 400.f, 1.f, hot)), eg = u8(lerp(hc.g / 400.f, 0.55f, hot)), eb = u8(lerp(hc.b / 400.f, 0.15f, hot));
            Vector2 v[3];
            for (int k = 0; k < 3; k++) { float a = q.ang + k * 2.1f; v[k] = {q.p.x + std::cos(a) * q.size * q.shape[k], q.p.y + std::sin(a) * q.size * q.shape[k]}; }
            rlColor4ub(er, eg, eb, A); rlTexCoord2f(0, 0); rlVertex2f(v[0].x, v[0].y);
            rlColor4ub(hc.r, hc.g, hc.b, A); rlTexCoord2f(0, 0); rlVertex2f(v[2].x, v[2].y);
            rlColor4ub((unsigned char)(hc.r * 0.6f), (unsigned char)(hc.g * 0.6f), (unsigned char)(hc.b * 0.6f), A); rlTexCoord2f(0, 0); rlVertex2f(v[1].x, v[1].y);
        }
        rlEnd(); rlSetTexture(0);
        rlDrawRenderBatchActive(); rlEnableBackfaceCulling();
    }
    BeginBlendMode(BLEND_ADDITIVE);
    // all effect quads in ONE shader batch (layer id in colour.b), back to front like the reference z order
    bool any = false; for (auto& x : ex_) any |= x.on; for (auto& x : im_) any |= x.on;
    if (dbg & 4) any = false;
    if (any) {
        Shader sh = fx_; BeginShaderMode(sh);
        for (auto& x : ex_) if (x.on && x.prog < 1) quad(x.p, x.K * E_.blastRadius * 2 * std::sin(x.prog * PI * 0.5f), {u8(x.prog), 0, 0, u8(x.gain)});
        for (auto& x : ex_) if (x.on && x.ringProg < 1) quad(x.p, x.K * E_.ringRadius * 2 * x.ringProg, {u8(x.ringProg), 0, 1, u8(x.gain)});
        if (!(dbg & 8)) {
            for (auto& x : im_) if (x.on && x.scorchProg < 1) quad(x.p, x.K * I_.ringRadius * 2 * x.ringProg, {u8(x.ringProg), u8(x.scorchProg), (unsigned char)(x.warm ? 7 : 3), 255});
            for (auto& x : im_) if (x.on && x.prog < 1) quad(x.p, x.K * I_.flashRadius * 2 * (1 - x.prog * 0.3f), {u8(x.prog), 0, (unsigned char)(x.warm ? 6 : 2), 255});
        }
        EndShaderMode();
    }
    // all point sprites in one textured batch
    static const Col warmSpark{1.f, 0.35f, 0.08f}, warmHot{1.f, 0.93f, 0.8f};
    rlSetTexture(atlas_.id); rlBegin(RL_QUADS);
    for (const Pt& q : pts_) {
        if ((dbg & 1) || ((dbg & 2) && q.kind == 1)) continue;
        Col c; float a, size; int slot;
        if (q.kind == 1) {  // smoke
            a = q.life * (1 - q.life) * 4 * E_.smokeOpacity; c = E_.smoke; size = q.size * (1 + (1 - q.life) * 1.5f); slot = 1;
        } else if (q.kind == 0) {  // ember
            float t = q.life * q.life; c = {lerp(E_.ember.r, E_.hot.r, t), lerp(E_.ember.g, E_.hot.g, t), lerp(E_.ember.b, E_.hot.b, t)};
            a = q.life * E_.debrisGain; size = q.size * lerp(0.3f, 1, q.life); slot = 0;
        } else {  // spark
            const Col& s0 = q.kind == 3 ? warmSpark : I_.spark; const Col& s1 = q.kind == 3 ? warmHot : I_.hot;
            float t = std::pow(q.life, 1.8f); c = {lerp(s0.r, s1.r, t), lerp(s0.g, s1.g, t), lerp(s0.b, s1.b, t)};
            a = q.life * I_.sparkGain; size = q.size * lerp(0.2f, 1, q.life); slot = 2;
        }
        a = std::min(a, 1.f);
        const float h = std::max(size, 1.f) * 0.5f, u0 = slot / 3.f, u1 = (slot + 1) / 3.f;
        // raw rlgl quad (DrawTexturePro per sprite was the measured L1 cost: ~2.4 ms for ~600 sparks)
        rlColor4ub(u8(c.r * a), u8(c.g * a), u8(c.b * a), u8(a));
        rlTexCoord2f(u0, 0); rlVertex2f(q.p.x - h, q.p.y - h);
        rlTexCoord2f(u0, 1); rlVertex2f(q.p.x - h, q.p.y + h);
        rlTexCoord2f(u1, 1); rlVertex2f(q.p.x + h, q.p.y + h);
        rlTexCoord2f(u1, 0); rlVertex2f(q.p.x + h, q.p.y - h);
    }
    rlEnd(); rlSetTexture(0);
    EndBlendMode();
}
