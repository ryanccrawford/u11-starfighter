#include "planet_terrain.hpp"
#include <algorithm>
#include <cmath>

#if defined(_MSC_VER)
#define PT_NOINLINE __declspec(noinline)
#else
#define PT_NOINLINE __attribute__((noinline))
#endif

namespace planet {
namespace {

constexpr float kPi = 3.14159265358979f;

inline std::uint32_t hash2(int x, int y, std::uint32_t s) {
    std::uint32_t h = s * 0x9E3779B1u;
    h ^= (std::uint32_t)x * 0x85EBCA77u; h = (h << 13) | (h >> 19);
    h ^= (std::uint32_t)y * 0xC2B2AE3Du;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}
inline float hash01(int x, int y, std::uint32_t s) { return (hash2(x, y, s) & 0xFFFFFF) / 16777216.f; }

struct GradTable { float x[16], y[16]; GradTable() { for (int i = 0; i < 16; i++) { x[i] = std::cos(i * kPi / 8.f + 0.2f); y[i] = std::sin(i * kPi / 8.f + 0.2f); } } };
const GradTable& grads() { static const GradTable g; return g; }

inline float gnoise(float x, float y, std::uint32_t s) {
    const GradTable& G = grads();
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const int ix = (int)fx0, iy = (int)fy0;
    const float fx = x - fx0, fy = y - fy0;
    const float u = fx * fx * fx * (fx * (fx * 6.f - 15.f) + 10.f), v = fy * fy * fy * (fy * (fy * 6.f - 15.f) + 10.f);
    const std::uint32_t h00 = hash2(ix, iy, s) & 15, h10 = hash2(ix + 1, iy, s) & 15, h01 = hash2(ix, iy + 1, s) & 15, h11 = hash2(ix + 1, iy + 1, s) & 15;
    const float a = G.x[h00] * fx + G.y[h00] * fy, b = G.x[h10] * (fx - 1.f) + G.y[h10] * fy;
    const float c = G.x[h01] * fx + G.y[h01] * (fy - 1.f), d = G.x[h11] * (fx - 1.f) + G.y[h11] * (fy - 1.f);
    return (a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v) * 1.41f;
}
// Octaves are rotated against each other (no grid-aligned streaks).
inline float fbm(float x, float y, int oct, std::uint32_t s) {
    float sum = 0.f, amp = 0.5f, norm = 0.f;
    for (int i = 0; i < oct; i++) {
        sum += amp * gnoise(x, y, s + 7919u * i); norm += amp;
        const float nx = 1.6f * x - 1.2f * y, ny = 1.2f * x + 1.6f * y;  // rotate ~37 deg, x2
        x = nx + 17.3f; y = ny - 9.1f; amp *= 0.5f;
    }
    return sum / norm;
}
inline float ridged(float x, float y, int oct, std::uint32_t s) {
    float sum = 0.f, amp = 0.5f, norm = 0.f, w = 1.f;
    for (int i = 0; i < oct; i++) {
        float n = 1.f - std::fabs(gnoise(x, y, s + 104729u * i)); n *= n; n *= w;
        w = std::min(1.f, std::max(0.f, n * 1.7f));
        sum += n * amp; norm += amp;
        const float nx = 1.6f * x - 1.2f * y, ny = 1.2f * x + 1.6f * y;
        x = nx - 5.7f; y = ny + 3.3f; amp *= 0.5f;
    }
    return sum / norm;
}
inline float sstep(float a, float b, float x) { float t = (x - a) / (b - a); t = t < 0.f ? 0.f : t > 1.f ? 1.f : t; return t * t * (3.f - 2.f * t); }
inline float mixf(float a, float b, float k) { return a + (b - a) * k; }
struct C3 { float r, g, b; };
inline C3 mix3(C3 a, C3 b, float k) { return {mixf(a.r, b.r, k), mixf(a.g, b.g, k), mixf(a.b, b.b, k)}; }
inline C3 mul3(C3 a, float k) { return {a.r * k, a.g * k, a.b * k}; }
inline unsigned char u8(float v) { v = v < 0.f ? 0.f : v > 1.f ? 1.f : v; return (unsigned char)(v * 255.f + 0.5f); }

// Height with a given set of gun sites (flattened pads). One code path for every caller.
PT_NOINLINE float heightSites(const Params& p, float x, float z, const GunSite* sites, int n) {
    float h = rawHeight(p, x, z);
    for (int i = 0; i < n; i++) {
        const float dx = x - sites[i].x, dz = z - sites[i].z, d = std::sqrt(dx * dx + dz * dz);
        const float r0 = p.gunClear * 0.75f, r1 = p.gunClear * 1.8f;
        if (d < r1) h = mixf(h, sites[i].y, 1.f - sstep(r0, r1, d));
    }
    return h;
}
int sitesNear(const Params& p, float z0, float z1, GunSite* out, int cap) {
    const float m = p.gunClear * 2.f;
    int n = 0;
    for (long c = gunCellOf(p, z0 - m); c <= gunCellOf(p, z1 + m) && n < cap; c++) out[n++] = gunSite(p, c);
    return n;
}
inline float microDetail(const Params& p, float x, float z) { return 0.016f * fbm(x * 2.6f, z * 2.6f, 2, p.seed + 61u); }

// ---- props ----------------------------------------------------------------------------------
struct Soup {
    std::vector<float>& pos; std::vector<float>& nrm; std::vector<unsigned char>& col;
    void v(float x, float y, float z, float nx, float ny, float nz, C3 c) {
        pos.push_back(x); pos.push_back(y); pos.push_back(z);
        const float l = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-9f;
        nrm.push_back(nx / l); nrm.push_back(ny / l); nrm.push_back(nz / l);
        col.push_back(u8(c.r)); col.push_back(u8(c.g)); col.push_back(u8(c.b)); col.push_back(255);
    }
};
struct Xf {  // prop local -> chunk local: scale (sxz, sy), yaw, translate
    float sxz, sy, c, s, tx, ty, tz;
    void apply(float lx, float ly, float lz, float& ox, float& oy, float& oz) const {
        const float x = lx * sxz, z = lz * sxz;
        ox = tx + x * c + z * s; oy = ty + ly * sy; oz = tz - x * s + z * c;
    }
    void dir(float lx, float ly, float lz, float& ox, float& oy, float& oz) const {  // normals: inverse-transpose of the scale
        const float x = lx / sxz, z = lz / sxz, y = ly / sy;
        ox = x * c + z * s; oy = y; oz = -x * s + z * c;
    }
};
void tri(Soup& S, const Xf& X, const float* a, const float* b, const float* c, const float* na, const float* nb, const float* nc, C3 ca, C3 cb, C3 cc) {
    float x, y, z, nx, ny, nz;
    X.apply(a[0], a[1], a[2], x, y, z); X.dir(na[0], na[1], na[2], nx, ny, nz); S.v(x, y, z, nx, ny, nz, ca);
    X.apply(b[0], b[1], b[2], x, y, z); X.dir(nb[0], nb[1], nb[2], nx, ny, nz); S.v(x, y, z, nx, ny, nz, cb);
    X.apply(c[0], c[1], c[2], x, y, z); X.dir(nc[0], nc[1], nc[2], nx, ny, nz); S.v(x, y, z, nx, ny, nz, cc);
}
void flatTri(Soup& S, const Xf& X, const float* a, const float* b, const float* c, C3 col) {
    float A[3], B[3], Cc[3];
    X.apply(a[0], a[1], a[2], A[0], A[1], A[2]); X.apply(b[0], b[1], b[2], B[0], B[1], B[2]); X.apply(c[0], c[1], c[2], Cc[0], Cc[1], Cc[2]);
    const float ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2], vx = Cc[0] - A[0], vy = Cc[1] - A[1], vz = Cc[2] - A[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (ny < 0) { nx = -nx; ny = -ny; nz = -nz; }
    S.v(A[0], A[1], A[2], nx, ny, nz, col); S.v(B[0], B[1], B[2], nx, ny, nz, col); S.v(Cc[0], Cc[1], Cc[2], nx, ny, nz, col);
}

#include "pine_lod.inc"
// Ryan's pine: pre-baked far LOD (unit height, pivot at trunk base), vertex colours varied per tree.
void addPine(Soup& S, const Xf& X, float bright, float warm) {
    for (int t = 0; t < kPineTris; t++) {
        const float* P = &kPinePos[t * 9]; const float* N = &kPineNrm[t * 9]; const float* Cc = &kPineCol[t * 9];
        C3 c[3];
        for (int k = 0; k < 3; k++) c[k] = {Cc[k * 3] * bright * (0.9f + 0.25f * warm), Cc[k * 3 + 1] * bright, Cc[k * 3 + 2] * bright * (1.2f - 0.4f * warm)};
        tri(S, X, P, P + 3, P + 6, N, N + 3, N + 6, c[0], c[1], c[2]);
    }
}

// Unit-height shapes (y up, radius relative to height).
void addConifer(Soup& S, const Xf& X, C3 col, int sides, std::uint32_t h) {
    const float tiers[3][3] = {{0.10f, 0.56f, 0.42f}, {0.36f, 0.80f, 0.31f}, {0.60f, 1.00f, 0.20f}};
    for (int t = 0; t < 3; t++) {
        const float y0 = tiers[t][0], y1 = tiers[t][1], r = tiers[t][2];
        const float ap[3] = {0.f, y1, 0.f}, an[3] = {0.f, 1.f, 0.f};
        const C3 cTop = mul3(col, 1.04f + 0.04f * t), cRing = mul3(col, 0.76f + 0.08f * t);
        const float a0 = (h >> (t * 4) & 15) / 16.f * 2.f * kPi / sides;
        for (int i = 0; i < sides; i++) {
            const float aa = a0 + i * 2.f * kPi / sides, ab = a0 + (i + 1) * 2.f * kPi / sides;
            const float ra = r * (0.88f + 0.24f * ((h >> ((i + t) % 8 * 3)) & 7) / 7.f);
            const float rb = r * (0.88f + 0.24f * ((h >> ((i + 1 + t) % 8 * 3)) & 7) / 7.f);
            const float A[3] = {ra * std::cos(aa), y0, ra * std::sin(aa)}, B[3] = {rb * std::cos(ab), y0, rb * std::sin(ab)};
            const float slope = r / (y1 - y0);
            const float NA[3] = {std::cos(aa), slope, std::sin(aa)}, NB[3] = {std::cos(ab), slope, std::sin(ab)};
            tri(S, X, ap, B, A, an, NB, NA, cTop, cRing, cRing);
        }
    }
}
void addDome(Soup& S, const Xf& X, C3 col, int sides, int rings, float yc, float ry, float rxz, std::uint32_t h, float wobble) {
    // rings from the top down: elevation angles in (90..~-10) deg; apex fan + bands
    std::vector<float> ring((rings + 1) * sides * 3);
    std::vector<float> nr((rings + 1) * sides * 3);
    for (int k = 0; k <= rings; k++) {
        const float el = kPi * 0.5f * (1.f - (k + 1.f) / (rings + 0.6f));
        for (int i = 0; i < sides; i++) {
            const float az = (i + 0.5f * (k & 1)) * 2.f * kPi / sides;
            const float w = 1.f + wobble * (((hash2(i, k, h) & 255) / 255.f) - 0.5f);
            const float x = std::cos(el) * std::cos(az), y = std::sin(el), z = std::cos(el) * std::sin(az);
            float* P = &ring[(k * sides + i) * 3]; P[0] = x * rxz * w; P[1] = yc + y * ry * w; P[2] = z * rxz * w;
            float* N = &nr[(k * sides + i) * 3]; N[0] = x / rxz; N[1] = y / ry; N[2] = z / rxz;
        }
    }
    const float ap[3] = {0.f, yc + ry, 0.f}, an[3] = {0.f, 1.f, 0.f};
    auto shade = [&](int k) { return mul3(col, 1.02f - 0.20f * k / (float)rings); };
    for (int i = 0; i < sides; i++) {
        const int j = (i + 1) % sides;
        tri(S, X, ap, &ring[j * 3], &ring[i * 3], an, &nr[j * 3], &nr[i * 3], mul3(col, 1.06f), shade(0), shade(0));
    }
    for (int k = 0; k < rings; k++)
        for (int i = 0; i < sides; i++) {
            const int j = (i + 1) % sides;
            // odd rings are offset by half a step: connect i,j on ring k with the vertex between them on ring k+1
            const int i2 = (k & 1) ? j : i, j2 = (k & 1) ? (j + 1) % sides : j;
            const float* a = &ring[(k * sides + i) * 3], *b = &ring[(k * sides + j) * 3];
            const float* c = &ring[((k + 1) * sides + i2) * 3], *d = &ring[((k + 1) * sides + (k & 1 ? i : (i + sides - 1) % sides)) * 3];
            (void)j2; (void)d;
            const float* na = &nr[(k * sides + i) * 3], *nb = &nr[(k * sides + j) * 3], *nc = &nr[((k + 1) * sides + i2) * 3];
            tri(S, X, a, b, c, na, nb, nc, shade(k), shade(k), shade(k + 1));
            const int i3 = (k & 1) ? i : (i + sides - 1) % sides;   // the ring-(k+1) vertex before i2
            const float* e = &ring[((k + 1) * sides + i3) * 3]; const float* ne = &nr[((k + 1) * sides + i3) * 3];
            tri(S, X, a, c, e, na, nc, ne, shade(k), shade(k + 1), shade(k + 1));
        }
}
void addRock(Soup& S, const Xf& X, C3 col, int sides, std::uint32_t h) {
    std::vector<float> top(sides * 3), bot(sides * 3);
    for (int i = 0; i < sides; i++) {
        const float a = (i + ((h & 7) / 8.f)) * 2.f * kPi / sides;
        const float w = 0.75f + 0.5f * ((hash2(i, 1, h) & 255) / 255.f), w2 = 0.85f + 0.3f * ((hash2(i, 2, h) & 255) / 255.f);
        top[i * 3] = 0.62f * w * std::cos(a); top[i * 3 + 1] = 0.55f + 0.25f * ((hash2(i, 3, h) & 255) / 255.f); top[i * 3 + 2] = 0.62f * w * std::sin(a);
        bot[i * 3] = 1.0f * w2 * std::cos(a); bot[i * 3 + 1] = -0.15f; bot[i * 3 + 2] = 1.0f * w2 * std::sin(a);
    }
    const float apex[3] = {0.08f, 1.0f, -0.05f};
    for (int i = 0; i < sides; i++) {
        const int j = (i + 1) % sides;
        const C3 c1 = mul3(col, 0.92f + 0.16f * ((hash2(i, 4, h) & 255) / 255.f));
        flatTri(S, X, apex, &top[j * 3], &top[i * 3], mul3(c1, 1.12f));
        flatTri(S, X, &top[i * 3], &top[j * 3], &bot[j * 3], c1);
        flatTri(S, X, &top[i * 3], &bot[j * 3], &bot[i * 3], mul3(c1, 0.9f));
    }
}
void addCrystal(Soup& S, const Xf& X, C3 col, std::uint32_t h) {
    for (int k = 0; k < 3; k++) {
        const float ox = 0.18f * std::cos(k * 2.1f + (h & 7)), oz = 0.18f * std::sin(k * 2.1f + (h & 7));
        const float ht = k == 0 ? 1.f : 0.55f + 0.3f * ((hash2(k, 5, h) & 255) / 255.f), r = 0.13f;
        const float tipx = ox * 2.2f, tipz = oz * 2.2f;   // shards lean outward
        const float tip[3] = {tipx, ht, tipz};
        float base[4][3];
        for (int i = 0; i < 4; i++) { const float a = i * kPi * 0.5f + k; base[i][0] = ox + r * std::cos(a); base[i][1] = -0.05f; base[i][2] = oz + r * std::sin(a); }
        for (int i = 0; i < 4; i++) flatTri(S, X, tip, base[(i + 1) % 4], base[i], mul3(col, 0.85f + 0.1f * i));
    }
}

}  // namespace

PT_NOINLINE float rawHeight(const Params& p, float x, float z) {
    const std::uint32_t s = p.seed;
    const float wx = x + 2.4f * fbm(x * 0.045f, z * 0.045f, 3, s + 101u);
    const float wz = z + 2.4f * fbm(x * 0.045f + 5.2f, z * 0.045f + 1.3f, 3, s + 202u);
    const float hills = fbm(wx * 0.085f, wz * 0.085f, 4, s + 1u);
    const float mask = sstep(-0.12f, 0.42f, fbm(x * 0.028f, z * 0.028f, 2, s + 7u));
    const float ridges = ridged(wx * 0.10f, wz * 0.10f, 3, s + 3u);
    const float valley = std::fabs(fbm(wx * 0.038f + 3.1f, wz * 0.038f - 2.7f, 3, s + 9u));
    const float carve = 1.f - sstep(0.f, 0.11f, valley);
    const float detail = fbm(x * 0.55f, z * 0.55f, 3, s + 11u);
    const float h = hills * 1.5f + mask * ridges * 2.7f + detail * 0.14f - carve * 1.05f * (1.f - 0.5f * mask) - 0.25f;
    return h * p.heightScale;
}

long gunCellOf(const Params& p, float z) { return (long)std::floor(z / p.gunSpacing); }

GunSite gunSite(const Params& p, long cell) {
    GunSite best; best.cell = cell;
    float bestScore = 1e9f;
    for (int k = 0; k < 6; k++) {
        const float x = (hash01((int)cell, k * 2, p.seed + 501u) * 2.f - 1.f) * p.gunLane * 0.85f;
        const float z = (cell + 0.2f + 0.6f * hash01((int)cell, k * 2 + 1, p.seed + 502u)) * p.gunSpacing;
        const float e = 0.35f;
        const float h = rawHeight(p, x, z);
        const float gx = (rawHeight(p, x + e, z) - rawHeight(p, x - e, z)) / (2 * e);
        const float gz = (rawHeight(p, x, z + e) - rawHeight(p, x, z - e)) / (2 * e);
        float score = gx * gx + gz * gz + std::fabs(x) * 0.004f;
        if (h < p.waterLevel + 0.15f) score += 10.f;           // never in a lake
        if (score < bestScore) { bestScore = score; best.x = x; best.z = z; best.y = std::max(h, p.waterLevel + 0.15f); }
    }
    return best;
}

float height(const Params& p, float x, float z) {
    GunSite s[8]; const int n = sitesNear(p, z, z, s, 8);
    return heightSites(p, x, z, s, n);
}

void buildChunk(const Params& p, int cx, int cz, ChunkData& out) {
    const int seg = p.segments, tex = p.texRes, T = tex + 1;
    const float C = p.chunkSize;
    out = ChunkData{};
    out.cx = cx; out.cz = cz; out.seg = seg; out.tex = tex;
    GunSite sites[16];
    const int ns = sitesNear(p, cz * C, (cz + 1) * C, sites, 16);
    const std::uint32_t s = p.seed;

    // ---- mesh: vertices on the global vertex lattice --------------------------------------
    const int V = seg + 1;
    out.vtx.resize(V * V * 3); out.uv.resize(V * V * 2);
    out.minH = 1e9f; out.maxH = -1e9f;
    const long gx0 = (long)cx * seg, gz0 = (long)cz * seg;
    const float ox = cx * C, oz = cz * C;
    for (int j = 0; j < V; j++)
        for (int i = 0; i < V; i++) {
            const float x = (float)vertexCoord(p, gx0 + i), z = (float)vertexCoord(p, gz0 + j);
            float h = heightSites(p, x, z, sites, ns);
            if (h < p.waterLevel) h = p.waterLevel;
            const int k = j * V + i;
            out.vtx[k * 3] = (float)(i * (double)C / seg); out.vtx[k * 3 + 1] = h; out.vtx[k * 3 + 2] = (float)(j * (double)C / seg);
            out.uv[k * 2] = (i * (float)tex / seg + 0.5f) / T; out.uv[k * 2 + 1] = (j * (float)tex / seg + 0.5f) / T;
            out.minH = std::min(out.minH, h); out.maxH = std::max(out.maxH, h);
        }
    out.idx.reserve(seg * seg * 6);
    for (int j = 0; j < seg; j++)
        for (int i = 0; i < seg; i++) {
            const unsigned short a = (unsigned short)(j * V + i), b = (unsigned short)(a + 1), c = (unsigned short)(a + V), d = (unsigned short)(c + 1);
            // split along the diagonal that follows the terrain (fewer flat-looking folds)
            const float hA = out.vtx[a * 3 + 1], hD = out.vtx[d * 3 + 1], hB = out.vtx[b * 3 + 1], hC = out.vtx[c * 3 + 1];
            if (std::fabs(hA - hD) < std::fabs(hB - hC)) { out.idx.insert(out.idx.end(), {a, c, d, a, d, b}); }
            else { out.idx.insert(out.idx.end(), {a, c, b, b, c, d}); }
        }

    // ---- textures: height grid on the global texel lattice (4-texel apron) ----------------
    const int B = 4, G = T + 2 * B;
    std::vector<float> hg(G * G), hb(G * G);   // hg: + micro relief (normals only), hb: terrain height
    const long tx0 = (long)cx * tex - B, tz0 = (long)cz * tex - B;
    for (int j = 0; j < G; j++)
        for (int i = 0; i < G; i++) {
            const float x = (float)texelCoord(p, tx0 + i), z = (float)texelCoord(p, tz0 + j);
            hb[j * G + i] = heightSites(p, x, z, sites, ns);
            hg[j * G + i] = hb[j * G + i] + microDetail(p, x, z);
        }
    const float dt = C / tex;
    out.albedo.resize(T * T * 4); out.normal.resize(T * T * 4);
    const C3 vegA{0.33f, 0.20f, 0.42f}, vegB{0.24f, 0.24f, 0.42f}, vegC{0.42f, 0.24f, 0.40f};
    const C3 soil{0.56f, 0.43f, 0.47f}, sand{0.68f, 0.58f, 0.62f}, rock{0.53f, 0.49f, 0.56f}, rockDark{0.34f, 0.30f, 0.38f}, snow{0.90f, 0.88f, 0.96f};
    const C3 shallow{0.13f, 0.16f, 0.36f}, deep{0.03f, 0.035f, 0.13f};
    for (int j = 0; j < T; j++)
        for (int i = 0; i < T; i++) {
            const int gi = i + B, gj = j + B;
            const float h = hb[gj * G + gi];
            const float hl = hg[gj * G + gi - 1], hr = hg[gj * G + gi + 1], hu = hg[(gj - 1) * G + gi], hd = hg[(gj + 1) * G + gi];
            float nx = -(hr - hl) / (2 * dt), nz = -(hd - hu) / (2 * dt), ny = 1.f;
            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
            const float lap = (hb[gj * G + gi - B] + hb[gj * G + gi + B] + hb[(gj - B) * G + gi] + hb[(gj + B) * G + gi] - 4.f * h) / ((B * dt) * (B * dt));
            const long GX = tx0 + gi, GZ = tz0 + gj;
            const float x = (float)texelCoord(p, GX), z = (float)texelCoord(p, GZ);
            const float bnx = -(hb[gj * G + gi + 1] - hb[gj * G + gi - 1]) / (2 * dt), bnz = -(hb[(gj + 1) * G + gi] - hb[(gj - 1) * G + gi]) / (2 * dt);
            const float slope = 1.f - 1.f / std::sqrt(1.f + bnx * bnx + bnz * bnz);
            const float pn = fbm(x * 0.16f, z * 0.16f, 3, s + 21u), pn2 = fbm(x * 0.32f, z * 0.32f, 2, s + 22u);
            C3 c = mix3(vegA, vegB, sstep(-0.35f, 0.35f, pn));
            c = mix3(c, vegC, sstep(0.2f, 0.7f, pn2) * 0.45f);
            const float dry = sstep(0.5f, 1.9f, h) * (0.6f + 0.4f * gnoise(x * 0.33f, z * 0.33f, s + 23u));
            c = mix3(c, soil, std::min(1.f, std::max(0.f, dry)) * 0.55f);
            const float strata = 0.5f + 0.5f * std::sin(h * 9.f + gnoise(x * 0.5f, z * 0.5f, s + 24u) * 2.5f);
            c = mix3(c, mix3(rockDark, rock, 0.35f + 0.4f * strata), sstep(0.20f, 0.40f, slope + 0.08f * pn2));
            {   // forest floor: the same mask the trees use, so stands read as one canopy mass
                const float fo = sstep(-0.05f, 0.35f, fbm(x * 0.11f, z * 0.11f, 3, s + 31u));
                const C3 canopy{0.19f, 0.14f, 0.29f};
                c = mix3(c, canopy, fo * fo * 0.55f * (1.f - sstep(0.18f, 0.35f, slope)) * (1.f - sstep(1.8f, 2.6f, h)));
            }
            c = mix3(c, snow, sstep(2.2f, 2.9f, h + 0.35f * pn) * (1.f - sstep(0.35f, 0.6f, slope)));
            c = mix3(c, sand, (1.f - sstep(p.waterLevel + 0.03f, p.waterLevel + 0.20f, h)) * 0.7f);
            c = mul3(c, 1.f - 0.35f * (1.f - sstep(p.waterLevel, p.waterLevel + 0.06f, h)));   // wet margin
            const float valleyDark = mixf(0.68f, 1.f, sstep(-1.3f, 0.6f, h));
            const float ao = std::min(1.05f, std::max(0.80f, 1.f - 0.18f * lap));
            const float grain = 0.96f + 0.08f * hash01((int)GX, (int)GZ, s + 77u);
            c = mul3(c, valleyDark * ao * grain);
            unsigned char wa = 0;
            if (h < p.waterLevel) {
                const float d = p.waterLevel - h;
                c = mix3(shallow, deep, sstep(0.f, 0.45f, d));
                wa = 255; nx = 0.f; ny = 1.f; nz = 0.f;
            }
            unsigned char* A = &out.albedo[(j * T + i) * 4];
            A[0] = u8(c.r); A[1] = u8(c.g); A[2] = u8(c.b); A[3] = wa;
            unsigned char* N = &out.normal[(j * T + i) * 4];
            N[0] = u8(nx * 0.5f + 0.5f); N[1] = u8(ny * 0.5f + 0.5f); N[2] = u8(nz * 0.5f + 0.5f); N[3] = 255;
        }

    // ---- props on a global jittered grid (owned by the chunk that contains them) ----------
    if (p.propDensity <= 0.f) return;
    Soup S{out.propPos, out.propNrm, out.propCol};
    const float gp = 0.21f;
    const int ci0 = (int)std::floor(ox / gp) - 1, ci1 = (int)std::floor((ox + C) / gp) + 1;
    const int cj0 = (int)std::floor(oz / gp) - 1, cj1 = (int)std::floor((oz + C) / gp) + 1;
    const int sides = p.propDetail > 0 ? 6 : 5;
    const float dens = p.propDensity;
    for (int cj = cj0; cj <= cj1; cj++)
        for (int ci = ci0; ci <= ci1; ci++) {
            const float x = (ci + 0.1f + 0.8f * hash01(ci, cj, s + 301u)) * gp, z = (cj + 0.1f + 0.8f * hash01(ci, cj, s + 302u)) * gp;
            if (x < ox || x >= ox + C || z < oz || z >= oz + C) continue;
            bool nearGun = false;
            for (int k = 0; k < ns; k++) { const float dx = x - sites[k].x, dz = z - sites[k].z; if (dx * dx + dz * dz < p.gunClear * p.gunClear * 1.44f) nearGun = true; }
            if (nearGun) continue;
            const float h = heightSites(p, x, z, sites, ns);
            if (h < p.waterLevel + 0.06f) continue;
            const float e = 0.12f;
            const float hx = heightSites(p, x + e, z, sites, ns) - heightSites(p, x - e, z, sites, ns);
            const float hz = heightSites(p, x, z + e, sites, ns) - heightSites(p, x, z - e, sites, ns);
            const float slope = 1.f - 1.f / std::sqrt(1.f + (hx * hx + hz * hz) / (4 * e * e));
            const float forest = sstep(-0.05f, 0.35f, fbm(x * 0.11f, z * 0.11f, 3, s + 31u));
            const float valleyK = 1.f - sstep(-0.6f, 1.8f, h);
            const float flat = 1.f - sstep(0.18f, 0.40f, slope);
            const float snowy = sstep(2.1f, 2.7f, h);
            const float pTree = dens * (0.012f + 0.92f * forest * forest * (0.40f + 0.60f * valleyK)) * flat * (1.f - snowy);
            const float pBush = dens * (0.02f + 0.07f * valleyK) * flat * (1.f - snowy);
            const float pRock = dens * (0.004f + 0.16f * sstep(0.22f, 0.55f, slope) + 0.03f * sstep(1.6f, 3.0f, h));
            const float pCrys = dens * 0.03f * sstep(0.35f, 0.7f, gnoise(x * 0.07f, z * 0.07f, s + 41u)) * (1.f - snowy);
            const float r = hash01(ci, cj, s + 303u), r2 = hash01(ci, cj, s + 304u), r3 = hash01(ci, cj, s + 305u);
            Prop pr{}; pr.x = x; pr.z = z; pr.yaw = r3 * 2.f * kPi; pr.tint = std::min(1.f, std::max(0.f, 0.5f + 0.9f * fbm(x * 0.21f, z * 0.21f, 2, s + 41u))) * 0.75f + 0.25f * r2;   // per stand, small per-tree spread
            // Pines stand alone in 3x3-cell blocks: one anchor cell may grow a pine, the block's other cells
            // grow no trees (bushes/rocks still), so the ~150-tri pine replaces ~8 small trees (tri-neutral).
            const int bi = ci >= 0 ? ci / 3 : -((-ci + 2) / 3), bj = cj >= 0 ? cj / 3 : -((-cj + 2) / 3);
            const float bx = (bi * 3 + 1.5f) * gp, bz = (bj * 3 + 1.5f) * gp;
            const float bForest = sstep(-0.05f, 0.35f, fbm(bx * 0.11f, bz * 0.11f, 3, s + 31u));
            const bool pineBlock = hash01(bi, bj, s + 311u) < p.pineShare * sstep(0.15f, 0.75f, bForest);
            const std::uint32_t ah = hash2(bi, bj, s + 312u);
            const bool anchor = pineBlock && ci == bi * 3 + (int)(ah % 3u) && cj == bj * 3 + (int)(ah / 3u % 3u);
            if (pineBlock && r < pTree && !anchor) continue;
            if (r < pTree) pr.type = anchor ? Pine : r2 < 0.30f + 0.5f * sstep(0.f, 1.6f, h) ? Conifer : Broadleaf;
            else if (r < pTree + pBush) pr.type = Bush;
            else if (r < pTree + pBush + pRock) pr.type = r2 < 0.18f ? Boulder : Rock;
            else if (r < pTree + pBush + pRock + pCrys) pr.type = Crystal;
            else continue;
            const float v = hash01(ci, cj, s + 306u);
            float radius = 0.f;
            switch (pr.type) {
                case Conifer: pr.scale = 0.22f + 0.20f * v; radius = 0.42f * pr.scale; break;
                case Broadleaf: pr.scale = 0.17f + 0.13f * v; radius = 0.62f * pr.scale; break;
                case Bush: pr.scale = 0.05f + 0.04f * v; radius = 0.9f * pr.scale; break;
                case Rock: pr.scale = 0.05f + 0.06f * v; radius = 0.95f * pr.scale; break;
                case Boulder: pr.scale = 0.12f + 0.10f * v; radius = 0.95f * pr.scale; break;
                case Pine: pr.scale = 0.60f + 0.28f * v; radius = kPineCrown * 0.85f * pr.scale; break;
                default: pr.scale = 0.16f + 0.16f * v; radius = 0.35f * pr.scale; break;
            }
            const float foot = pr.type == Pine ? 0.10f * pr.scale : radius;   // grounding footprint (pine: trunk flare only)
            // grounded: the lowest surface point under the footprint, slightly sunk
            float y = std::max(h, p.waterLevel);
            for (int k = 0; k < 4; k++) {
                const float a = k * kPi * 0.5f + pr.yaw;
                y = std::min(y, heightSites(p, x + 0.6f * foot * std::cos(a), z + 0.6f * foot * std::sin(a), sites, ns));
            }
            pr.y = y - 0.01f;
            out.props.push_back(pr);
            const Xf X{1.f, pr.scale, std::cos(pr.yaw), std::sin(pr.yaw), x - ox, pr.y, z - oz};
            const std::uint32_t ph = hash2(ci, cj, s + 307u);
            switch (pr.type) {
                case Conifer: {
                    const C3 col = mix3(C3{0.22f, 0.14f, 0.29f}, C3{0.17f, 0.16f, 0.29f}, pr.tint);
                    Xf Xc = X; Xc.sxz = pr.scale; addConifer(S, Xc, col, sides, ph); break;
                }
                case Broadleaf: {
                    const C3 col = mix3(C3{0.31f, 0.17f, 0.34f}, C3{0.22f, 0.18f, 0.34f}, pr.tint);
                    Xf Xc = X; Xc.sxz = pr.scale; addDome(S, Xc, col, sides + 1, p.propDetail > 0 ? 2 : 1, 0.55f, 0.30f, 0.62f, ph, 0.45f); break;
                }
                case Pine: {
                    Xf Xc = X; Xc.sxz = pr.scale * (0.92f + 0.16f * r2); Xc.sy = pr.scale;
                    addPine(S, Xc, 1.50f + 0.35f * (pr.tint - 0.5f) + 0.2f * (r3 - 0.5f), r2); break;
                }
                case Bush: {
                    const C3 col = mix3(C3{0.27f, 0.18f, 0.33f}, C3{0.23f, 0.20f, 0.32f}, pr.tint);
                    Xf Xc = X; Xc.sxz = pr.scale; addDome(S, Xc, col, 5, 1, 0.1f, 0.85f, 0.9f, ph, 0.4f); break;
                }
                case Rock: case Boulder: {
                    const C3 col = mix3(C3{0.46f, 0.42f, 0.48f}, C3{0.36f, 0.32f, 0.39f}, pr.tint);
                    Xf Xc = X; Xc.sxz = pr.scale * 1.15f; Xc.sy = pr.scale * (0.55f + 0.3f * v); addRock(S, Xc, col, sides, ph); break;
                }
                default: {
                    const C3 col = pr.tint < 0.6f ? C3{0.58f, 0.44f, 0.86f} : C3{0.44f, 0.66f, 0.84f};
                    Xf Xc = X; Xc.sxz = pr.scale; addCrystal(S, Xc, col, ph); break;
                }
            }
            // blob shadow caster
            if (out.shPos.size() / 3 + 4 < 65535) {
                const unsigned short base = (unsigned short)(out.shPos.size() / 3);
                const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
                const float sh = pr.type == Rock || pr.type == Bush ? pr.scale * 0.6f : pr.type == Boulder ? pr.scale * 0.7f : pr.type == Pine ? pr.scale * 0.8f : pr.scale;
                for (int k = 0; k < 4; k++) {
                    out.shPos.insert(out.shPos.end(), {x - ox, pr.y + 0.01f, z - oz});
                    out.shUv.insert(out.shUv.end(), {corner[k][0], corner[k][1]});
                    out.shUv2.insert(out.shUv2.end(), {radius, sh});
                }
                out.shIdx.insert(out.shIdx.end(), {base, (unsigned short)(base + 2), (unsigned short)(base + 1), base, (unsigned short)(base + 3), (unsigned short)(base + 2)});
            }
        }
}

}  // namespace planet
