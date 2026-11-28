#include "planet_render.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
const char* PT_TERRAIN_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord;
uniform mat4 mvp; uniform vec2 origin;
out vec2 uv; out vec2 wxz;
void main(){ uv = vertexTexCoord; wxz = origin + vertexPosition.xz; gl_Position = mvp * vec4(vertexPosition, 1.0); })";
// Sun (the only light) + purple sky fill. Albedo / normal come from the chunk textures; a tiling
// detail texture adds grain and small-scale relief up close. Lakes get a sun glint.
const char* PT_TERRAIN_FS = R"(#version 330
in vec2 uv; in vec2 wxz;
uniform sampler2D texture0; uniform sampler2D texture1; uniform sampler2D texture2;
uniform vec3 sunL; uniform float amb;
out vec4 o;
void main(){
  vec4 A = texture(texture0, uv);
  vec3 n = normalize(texture(texture1, uv).xyz * 2.0 - 1.0);
  float d1 = texture(texture2, wxz * 2.1).r, d2 = texture(texture2, wxz * 7.3 + vec2(0.37, 0.71)).r;
  vec3 L = normalize(sunL);
  float up = smoothstep(-0.05, 0.25, L.y);
  vec3 sunC = mix(vec3(1.0, 0.66, 0.50), vec3(1.0, 0.96, 0.90), smoothstep(0.1, 0.6, L.y)) * 1.55 * up;
  vec3 sky = vec3(0.52, 0.47, 0.80) * (amb + 0.46);
  vec3 col;
  if (A.a > 0.5) {
    float w1 = texture(texture2, wxz * 0.35).r;
    vec3 wn = normalize(vec3((w1 - 0.5) * 0.10, 1.0, (d1 - 0.5) * 0.10));
    vec3 H = normalize(L + vec3(0.0, 1.0, 0.0));
    float spec = pow(max(dot(wn, H), 0.0), 36.0) * 0.55;
    col = A.rgb * (sky * 1.0 + sunC * max(dot(wn, L), 0.0) * 0.40) + sky * 0.05 + sunC * spec;
  } else {
    vec3 alb = A.rgb * (0.90 + 0.20 * (d1 * 0.5 + d2 * 0.5));
    n = normalize(n + vec3((d2 - 0.5) * 0.22, 0.0, (d1 - 0.5) * 0.22));
    col = alb * (sunC * max(dot(n, L), 0.0) + sky * (0.55 + 0.45 * n.y));
  }
  o = vec4(col, 1.0);
})";
const char* PT_PROP_VS = R"(#version 330
in vec3 vertexPosition; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; out vec3 nrm; out vec4 col;
void main(){ nrm = vertexNormal; col = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); })";
const char* PT_PROP_FS = R"(#version 330
in vec3 nrm; in vec4 col; uniform vec3 sunL; uniform float amb; out vec4 o;
void main(){
  vec3 n = normalize(nrm), L = normalize(sunL);
  float up = smoothstep(-0.05, 0.25, L.y);
  vec3 sunC = mix(vec3(1.0, 0.66, 0.50), vec3(1.0, 0.96, 0.90), smoothstep(0.1, 0.6, L.y)) * 1.55 * up;
  vec3 sky = vec3(0.52, 0.47, 0.80) * (amb + 0.46);
  float d = max(dot(n, L) * 0.85 + 0.15, 0.0);
  o = vec4(col.rgb * (sunC * d + sky * (0.5 + 0.5 * n.y)), 1.0);
})";
// Blob shadow: a soft ellipse stretched away from the sun by the caster's height.
const char* PT_SHADOW_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec2 vertexTexCoord2;
uniform mat4 mvp; uniform vec3 sunL; out vec2 q; out float fade;
void main(){
  vec3 L = normalize(sunL);
  float upL = max(L.y, 0.12), hl = length(L.xz);
  vec2 dir = hl > 1e-4 ? -L.xz / hl : vec2(1.0, 0.0);
  float len = min(vertexTexCoord2.y * hl / upL, vertexTexCoord2.y * 3.5);
  float r = vertexTexCoord2.x;
  vec2 c = vertexPosition.xz + dir * len * 0.5;
  vec2 perp = vec2(-dir.y, dir.x);
  vec2 p = c + dir * vertexTexCoord.x * (r + len * 0.5) + perp * vertexTexCoord.y * r;
  q = vertexTexCoord; fade = smoothstep(-0.02, 0.2, L.y);
  gl_Position = mvp * vec4(p.x, vertexPosition.y, p.y, 1.0);
})";
const char* PT_SHADOW_FS = R"(#version 330
in vec2 q; in float fade; out vec4 o;
void main(){ float d = length(q); float a = (1.0 - smoothstep(0.35, 1.0, d)) * 0.45 * fade; o = vec4(0.05, 0.02, 0.12, a); })";
// Craft shadows: vertices already lie on the terrain; uv is the unit-ellipse coordinate.
const char* PT_CRAFT_SHADOW_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;
uniform mat4 mvp; out vec2 q; out float fade;
void main(){ q = vertexTexCoord; fade = vertexColor.a; gl_Position = mvp * vec4(vertexPosition, 1.0); })";
const char* PT_CRAFT_SHADOW_FS = R"(#version 330
in vec2 q; in float fade; uniform float softness; out vec4 o;
void main(){
  float d = length(q);
  float a = (1.0 - smoothstep(clamp(softness, 0.05, 0.8), 1.0, d)) * fade;
  o = vec4(0.05, 0.02, 0.12, a);
})";

void setAttribLocs(Shader& s) {
    s.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(s, "vertexPosition");
    s.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(s, "vertexTexCoord");
    s.locs[SHADER_LOC_VERTEX_TEXCOORD02] = GetShaderLocationAttrib(s, "vertexTexCoord2");
    s.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(s, "vertexNormal");
    s.locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(s, "vertexColor");
    s.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(s, "mvp");
}
template <class T> T* copyOut(const std::vector<T>& v) {
    if (v.empty()) return nullptr;
    T* p = (T*)MemAlloc((unsigned int)(v.size() * sizeof(T)));
    std::memcpy(p, v.data(), v.size() * sizeof(T));
    return p;
}
// Tileable value-noise detail (grayscale, mipmapped, repeat).
Texture2D makeDetail() {
    const int N = 256;
    std::vector<unsigned char> px(N * N);
    auto h = [](int x, int y, int s) { unsigned int v = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)s * 83492791u; v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15; return (v & 0xFFFF) / 65535.f; };
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float sum = 0, amp = 0.5f, norm = 0;
            for (int o = 0; o < 5; o++) {
                const int P = 8 << o; const float fx = x * P / (float)N, fy = y * P / (float)N;
                const int ix = (int)fx, iy = (int)fy; const float tx = fx - ix, ty = fy - iy;
                const float u = tx * tx * (3 - 2 * tx), v = ty * ty * (3 - 2 * ty);
                const float a = h(ix % P, iy % P, o), b = h((ix + 1) % P, iy % P, o), c = h(ix % P, (iy + 1) % P, o), d = h((ix + 1) % P, (iy + 1) % P, o);
                sum += amp * (a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v); norm += amp; amp *= 0.55f;
            }
            px[y * N + x] = (unsigned char)std::clamp((int)(sum / norm * 255.f), 0, 255);
        }
    Image img{px.data(), N, N, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
    Texture2D t = LoadTextureFromImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}
}  // namespace

bool PlanetRenderer::init(const planet::Params& p, int workers) {
    if (inited_) return true;
    p_ = p;
    terrainSh_ = LoadShaderFromMemory(PT_TERRAIN_VS, PT_TERRAIN_FS);
    propSh_ = LoadShaderFromMemory(PT_PROP_VS, PT_PROP_FS);
    shadowSh_ = LoadShaderFromMemory(PT_SHADOW_VS, PT_SHADOW_FS);
    craftShadowSh_ = LoadShaderFromMemory(PT_CRAFT_SHADOW_VS, PT_CRAFT_SHADOW_FS);
    if (terrainSh_.id == 0 || propSh_.id == 0 || shadowSh_.id == 0 || craftShadowSh_.id == 0) return false;
    for (Shader* s : {&terrainSh_, &propSh_, &shadowSh_, &craftShadowSh_}) setAttribLocs(*s);
    terrainSh_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(terrainSh_, "texture0");
    terrainSh_.locs[SHADER_LOC_MAP_SPECULAR] = GetShaderLocation(terrainSh_, "texture1");
    terrainSh_.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(terrainSh_, "texture2");
    tLocOrigin_ = GetShaderLocation(terrainSh_, "origin");
    tLocSun_ = GetShaderLocation(terrainSh_, "sunL"); tLocAmb_ = GetShaderLocation(terrainSh_, "amb");
    pLocSun_ = GetShaderLocation(propSh_, "sunL"); pLocAmb_ = GetShaderLocation(propSh_, "amb");
    sLocSun_ = GetShaderLocation(shadowSh_, "sunL");
    csLocSoft_ = GetShaderLocation(craftShadowSh_, "softness");
    detail_ = makeDetail();
    terrainMat_ = LoadMaterialDefault(); terrainMat_.shader = terrainSh_;
    terrainMat_.maps[MATERIAL_MAP_NORMAL].texture = detail_;
    propMat_ = LoadMaterialDefault(); propMat_.shader = propSh_;
    shadowMat_ = LoadMaterialDefault(); shadowMat_.shader = shadowSh_;
    craftShadowMat_ = LoadMaterialDefault(); craftShadowMat_.shader = craftShadowSh_;
    setLight({-0.45f, 0.8f, 0.35f}, 0.2f);
    quit_ = false;
    for (int i = 0; i < std::max(1, workers); i++) threads_.emplace_back([this] { worker(); });
    inited_ = true;
    return true;
}

void PlanetRenderer::shutdown() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        quit_ = true; queue_.clear();
    }
    cv_.notify_all();
    for (auto& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
    if (!inited_) return;
    for (auto& kv : chunks_) release(kv.second);
    chunks_.clear();
    for (auto& tp : texPool_) { UnloadTexture(tp.first); UnloadTexture(tp.second); }
    texPool_.clear();
    UnloadTexture(detail_);
    UnloadShader(terrainSh_); UnloadShader(propSh_); UnloadShader(shadowSh_); UnloadShader(craftShadowSh_);
    // materials share the shaders / textures released above: only their map arrays are owned
    MemFree(terrainMat_.maps); MemFree(propMat_.maps); MemFree(shadowMat_.maps); MemFree(craftShadowMat_.maps);
    inited_ = false;
}

void PlanetRenderer::worker() {
    for (;;) {
        std::pair<int, int> job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            job = queue_.front(); queue_.pop_front();
        }
        auto d = std::make_unique<planet::ChunkData>();
        planet::buildChunk(p_, job.first, job.second, *d);
        std::lock_guard<std::mutex> lk(mu_);
        if (quit_) return;
        done_.push_back(std::move(d));
    }
}

void PlanetRenderer::request(float x0, float x1, float z0, float z1) {
    if (!inited_) return;
    rx0_ = x0; rx1_ = x1; rz0_ = z0; rz1_ = z1; haveReq_ = true;
    const float C = p_.chunkSize;
    const int i0 = (int)std::floor(x0 / C), i1 = (int)std::floor(x1 / C), j0 = (int)std::floor(z0 / C), j1 = (int)std::floor(z1 / C);
    const float mx = (x0 + x1) * 0.5f, mz = (z0 + z1) * 0.5f;
    // recycle chunks that are well outside the wanted rectangle (behind the scroll)
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        const Gpu& g = it->second;
        if (g.cx < i0 - 1 || g.cx > i1 + 1 || g.cz < j0 - 2 || g.cz > j1 + 2) { release(it->second); it = chunks_.erase(it); }
        else ++it;
    }
    std::lock_guard<std::mutex> lk(mu_);
    // drop queued jobs nobody wants any more
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->first < i0 - 1 || it->first > i1 + 1 || it->second < j0 - 2 || it->second > j1 + 2) { queued_.erase(key(it->first, it->second)); it = queue_.erase(it); }
        else ++it;
    }
    bool added = false;
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            const long k = key(i, j);
            if (chunks_.count(k) || queued_.count(k)) continue;
            queue_.emplace_back(i, j); queued_.insert(k); added = true;
        }
    if (added) {
        std::sort(queue_.begin(), queue_.end(), [&](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            const float ax = (a.first + 0.5f) * C - mx, az = (a.second + 0.5f) * C - mz, bx = (b.first + 0.5f) * C - mx, bz = (b.second + 0.5f) * C - mz;
            return ax * ax + az * az < bx * bx + bz * bz;
        });
        cv_.notify_all();
    }
}

void PlanetRenderer::pump(int maxUploads) {
    if (!inited_) return;
    std::vector<std::unique_ptr<planet::ChunkData>> ready;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const int n = std::min<int>(maxUploads, (int)done_.size());
        for (int i = 0; i < n; i++) ready.push_back(std::move(done_[i]));
        done_.erase(done_.begin(), done_.begin() + n);
        for (auto& d : ready) queued_.erase(key(d->cx, d->cz));
        stats.pending = (int)(queue_.size() + done_.size());
    }
    const float C = p_.chunkSize;
    for (auto& dp : ready) {
        planet::ChunkData& d = *dp;
        if (haveReq_) {
            const int i0 = (int)std::floor(rx0_ / C), i1 = (int)std::floor(rx1_ / C), j0 = (int)std::floor(rz0_ / C), j1 = (int)std::floor(rz1_ / C);
            if (d.cx < i0 - 1 || d.cx > i1 + 1 || d.cz < j0 - 2 || d.cz > j1 + 2) continue;
        }
        if (chunks_.count(key(d.cx, d.cz))) continue;
        Gpu g; g.cx = d.cx; g.cz = d.cz; g.minH = d.minH; g.maxH = d.maxH;
        const int T = d.tex + 1;
        if (!texPool_.empty() && texPool_.back().first.width == T) {
            g.alb = texPool_.back().first; g.nrm = texPool_.back().second; texPool_.pop_back();
            UpdateTexture(g.alb, d.albedo.data()); UpdateTexture(g.nrm, d.normal.data());
        } else {
            Image ia{d.albedo.data(), T, T, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            Image in{d.normal.data(), T, T, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            g.alb = LoadTextureFromImage(ia); g.nrm = LoadTextureFromImage(in);
            for (Texture2D* t : {&g.alb, &g.nrm}) { SetTextureFilter(*t, TEXTURE_FILTER_BILINEAR); SetTextureWrap(*t, TEXTURE_WRAP_CLAMP); }
        }
        Mesh& m = g.terrain;
        m.vertexCount = (int)d.vtx.size() / 3; m.triangleCount = (int)d.idx.size() / 3;
        m.vertices = copyOut(d.vtx); m.texcoords = copyOut(d.uv); m.indices = copyOut(d.idx);
        UploadMesh(&m, false);
        MemFree(m.vertices); m.vertices = nullptr; MemFree(m.texcoords); m.texcoords = nullptr;  // GPU copy is enough (indices stay: DrawMesh keys on them)
        g.terrainTris = m.triangleCount;
        if (!d.propPos.empty()) {
            Mesh& pm = g.props;
            pm.vertexCount = (int)d.propPos.size() / 3; pm.triangleCount = pm.vertexCount / 3;
            pm.vertices = copyOut(d.propPos); pm.normals = copyOut(d.propNrm); pm.colors = copyOut(d.propCol);
            UploadMesh(&pm, false);
            MemFree(pm.vertices); pm.vertices = nullptr; MemFree(pm.normals); pm.normals = nullptr; MemFree(pm.colors); pm.colors = nullptr;
            g.hasProps = true; g.propTris = pm.triangleCount;
        }
        if (!d.shIdx.empty()) {
            Mesh& sm = g.shadows;
            sm.vertexCount = (int)d.shPos.size() / 3; sm.triangleCount = (int)d.shIdx.size() / 3;
            sm.vertices = copyOut(d.shPos); sm.texcoords = copyOut(d.shUv); sm.texcoords2 = copyOut(d.shUv2); sm.indices = copyOut(d.shIdx);
            UploadMesh(&sm, false);
            MemFree(sm.vertices); sm.vertices = nullptr; MemFree(sm.texcoords); sm.texcoords = nullptr; MemFree(sm.texcoords2); sm.texcoords2 = nullptr;
            g.hasShadows = true; g.shadowQuads = sm.triangleCount / 2;
        }
        chunks_[key(d.cx, d.cz)] = g;
    }
    stats.cached = (int)chunks_.size();
}

void PlanetRenderer::release(Gpu& g) {
    if (g.alb.id) texPool_.push_back({g.alb, g.nrm});
    if (texPool_.size() > 12) { UnloadTexture(texPool_.front().first); UnloadTexture(texPool_.front().second); texPool_.erase(texPool_.begin()); }
    UnloadMesh(g.terrain);
    if (g.hasProps) UnloadMesh(g.props);
    if (g.hasShadows) UnloadMesh(g.shadows);
    g = Gpu{};
}

void PlanetRenderer::setLight(Vector3 toSun, float amb) {
    if (terrainSh_.id == 0) return;
    SetShaderValue(terrainSh_, tLocSun_, &toSun, SHADER_UNIFORM_VEC3); SetShaderValue(terrainSh_, tLocAmb_, &amb, SHADER_UNIFORM_FLOAT);
    SetShaderValue(propSh_, pLocSun_, &toSun, SHADER_UNIFORM_VEC3); SetShaderValue(propSh_, pLocAmb_, &amb, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shadowSh_, sLocSun_, &toSun, SHADER_UNIFORM_VEC3);
}

bool PlanetRenderer::covered(float x0, float x1, float z0, float z1) const {
    const float C = p_.chunkSize;
    for (int j = (int)std::floor(z0 / C); j <= (int)std::floor(z1 / C); j++)
        for (int i = (int)std::floor(x0 / C); i <= (int)std::floor(x1 / C); i++)
            if (!chunks_.count(key(i, j))) return false;
    return true;
}

void PlanetRenderer::draw(const Matrix& layer, float base, float scroll, float x0, float x1, float z0, float z1, Vector3 eye, float propFar) {
    stats.chunks = stats.terrainTris = stats.propTris = stats.shadowQuads = stats.draws = 0;
    if (!inited_) return;
    const float C = p_.chunkSize;
    std::vector<std::pair<const Gpu*, Matrix>> vis;
    std::vector<char> near;
    stats.dMin = 1e9f; stats.dMax = 0.f;
    for (const auto& kv : chunks_) {
        const Gpu& g = kv.second;
        const float gx0 = g.cx * C, gz0 = g.cz * C;
        if (gx0 > x1 || gx0 + C < x0 || gz0 > z1 || gz0 + C < z0) continue;
        vis.push_back({&g, MatrixMultiply(MatrixTranslate(gx0, base, gz0 + scroll), layer)});
        const Vector3 c = Vector3Transform({gx0 + 0.5f * C, base, gz0 + 0.5f * C + scroll}, layer);
        const float d = Vector3Distance(c, eye);
        stats.dMin = fminf(stats.dMin, d); stats.dMax = fmaxf(stats.dMax, d);
        near.push_back(d <= propFar);   // distance culling of props + shadows (oblique views)
    }
    rlDrawRenderBatchActive();
    for (auto& v : vis) {
        const Gpu& g = *v.first;
        const float origin[2] = {g.cx * C, g.cz * C};
        SetShaderValue(terrainSh_, tLocOrigin_, origin, SHADER_UNIFORM_VEC2);
        terrainMat_.maps[MATERIAL_MAP_ALBEDO].texture = g.alb;
        terrainMat_.maps[MATERIAL_MAP_METALNESS].texture = g.nrm;
        DrawMesh(g.terrain, terrainMat_, v.second);
        stats.terrainTris += g.terrainTris; stats.draws++; stats.chunks++;
    }
    // shadows lie on the ground: drawn over it (no depth test), under the props
    rlDrawRenderBatchActive();
    rlDisableDepthTest(); rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    for (size_t i = 0; i < vis.size(); ++i) if (near[i] && vis[i].first->hasShadows) { auto& v = vis[i]; DrawMesh(v.first->shadows, shadowMat_, v.second); stats.shadowQuads += v.first->shadowQuads; stats.draws++; }
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask(); rlEnableDepthTest();
    for (size_t i = 0; i < vis.size(); ++i) {
        auto& v = vis[i];
        const Gpu& g = *v.first;
        if (!g.hasProps || !near[i]) continue;
        DrawMesh(g.props, propMat_, v.second);
        stats.propTris += g.propTris; stats.draws++;
    }
    rlDrawRenderBatchActive();
}

void PlanetRenderer::drawCraftShadows(const Matrix& layer, float base, float scroll, Vector3 toSun,
                                      const CraftCaster* casters, int count, int quality) {
    if (!inited_ || !casters || count <= 0 || quality < 1 || craftShadowSh_.id == 0) return;
    const Vector3 L = Vector3Normalize(toSun);
    const float sunFade = Clamp((L.y + 0.02f) / 0.22f, 0.f, 1.f);
    if (sunFade <= 0.01f) return;
    const Matrix inv = MatrixInvert(layer);
    const int N = (quality >= 2) ? 8 : 2;                 // verts per side
    const float softness = (quality >= 2) ? 0.18f : 0.38f;
    const float maxA = (quality >= 2) ? 0.55f : 0.38f;
    std::vector<float> vtx, uv;
    std::vector<unsigned char> col;
    std::vector<unsigned short> idx;
    vtx.reserve((size_t)count * N * N * 3);
    uv.reserve((size_t)count * N * N * 2);
    col.reserve((size_t)count * N * N * 4);
    idx.reserve((size_t)count * (N - 1) * (N - 1) * 6);

    const float hl = sqrtf(L.x * L.x + L.z * L.z);
    const float upL = fmaxf(L.y, 0.12f);
    const Vector2 dir = hl > 1e-4f ? Vector2{-L.x / hl, -L.z / hl} : Vector2{1.f, 0.f};
    const Vector2 perp{-dir.y, dir.x};

    for (int ci = 0; ci < count; ci++) {
        const Vector3 g = Vector3Transform(casters[ci].world, inv);
        const float radius = fmaxf(0.08f, casters[ci].radius);
        // First guess: drop straight down, then walk the sun ray onto the surface.
        float tx = g.x, tz = g.z;
        float hy = base + planet::surface(p_, tx, tz - scroll);
        float alt = g.y - hy;
        if (alt < -2.f) continue;   // well below the mesh (off the terrain / wrong space)
        for (int it = 0; it < 6; it++) {
            hy = base + planet::surface(p_, tx, tz - scroll);
            const float dy = g.y - hy;
            const float t = dy / upL;
            const float nx = g.x - L.x * t, nz = g.z - L.z * t;
            if (fabsf(nx - tx) + fabsf(nz - tz) < 0.01f) { tx = nx; tz = nz; break; }
            tx = nx; tz = nz;
        }
        hy = base + planet::surface(p_, tx, tz - scroll);
        alt = fmaxf(0.f, g.y - hy);
        const float len = fminf(alt * hl / upL, alt * 3.5f);
        const float grow = 1.f + 0.45f * fminf(alt / 28.f, 1.f);
        const float r = radius * grow;
        const float fade = maxA * sunFade * (1.f - Clamp((alt - 10.f) / 30.f, 0.f, 1.f));
        if (fade < 0.02f) continue;
        const Vector2 c{tx + dir.x * len * 0.5f, tz + dir.y * len * 0.5f};
        const float ext = r + len * 0.5f;
        const unsigned short baseV = (unsigned short)(vtx.size() / 3);
        if ((int)baseV + N * N >= 65535) break;
        for (int j = 0; j < N; j++) {
            const float v = -1.f + 2.f * (float)j / (float)(N - 1);
            for (int i = 0; i < N; i++) {
                const float u = -1.f + 2.f * (float)i / (float)(N - 1);
                const float px = c.x + dir.x * u * ext + perp.x * v * r;
                const float pz = c.y + dir.y * u * ext + perp.y * v * r;
                const float py = base + planet::surface(p_, px, pz - scroll) + 0.04f;
                vtx.insert(vtx.end(), {px, py, pz});
                uv.insert(uv.end(), {u, v});
                const unsigned char a = (unsigned char)std::clamp((int)(fade * 255.f + 0.5f), 0, 255);
                col.insert(col.end(), {12, 6, 30, a});
            }
        }
        for (int j = 0; j < N - 1; j++)
            for (int i = 0; i < N - 1; i++) {
                const unsigned short a = (unsigned short)(baseV + j * N + i);
                const unsigned short b = (unsigned short)(a + 1);
                const unsigned short c0 = (unsigned short)(a + N);
                const unsigned short d = (unsigned short)(a + N + 1);
                idx.insert(idx.end(), {a, c0, b, b, c0, d});
            }
    }
    if (idx.empty()) return;

    Mesh mesh{};
    mesh.vertexCount = (int)(vtx.size() / 3);
    mesh.triangleCount = (int)(idx.size() / 3);
    mesh.vertices = copyOut(vtx);
    mesh.texcoords = copyOut(uv);
    mesh.colors = copyOut(col);
    mesh.indices = copyOut(idx);
    UploadMesh(&mesh, false);
    SetShaderValue(craftShadowSh_, csLocSoft_, &softness, SHADER_UNIFORM_FLOAT);
    rlDrawRenderBatchActive();
    rlDisableDepthTest(); rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    DrawMesh(mesh, craftShadowMat_, layer);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask(); rlEnableDepthTest();
    UnloadMesh(mesh);
}
