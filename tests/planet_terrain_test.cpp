// Level 6 chunked ground: neighbouring chunks must agree exactly on their shared edges (mesh
// heights, albedo and normal texels), generation must be deterministic, props must sit on the
// ground and gun sites must be flat, dry and clear of props.
#include "../planet_terrain.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace planet;

static void checkEdgeX(const Params& p, const ChunkData& a, const ChunkData& b) {  // a left of b
    const int V = p.segments + 1, T = p.texRes + 1;
    for (int j = 0; j < V; j++) {
        const float ha = a.vtx[(j * V + (V - 1)) * 3 + 1], hb = b.vtx[(j * V + 0) * 3 + 1];
        assert(ha == hb);
        assert(a.vtx[(j * V + (V - 1)) * 3 + 0] == p.chunkSize && b.vtx[(j * V) * 3] == 0.f);
        assert(a.vtx[(j * V + (V - 1)) * 3 + 2] == b.vtx[(j * V) * 3 + 2]);
    }
    for (int j = 0; j < T; j++) {
        assert(std::memcmp(&a.albedo[(j * T + T - 1) * 4], &b.albedo[(j * T) * 4], 4) == 0);
        assert(std::memcmp(&a.normal[(j * T + T - 1) * 4], &b.normal[(j * T) * 4], 4) == 0);
    }
}
static void checkEdgeZ(const Params& p, const ChunkData& a, const ChunkData& b) {  // a above (lower z) b
    const int V = p.segments + 1, T = p.texRes + 1;
    for (int i = 0; i < V; i++) assert(a.vtx[((V - 1) * V + i) * 3 + 1] == b.vtx[i * 3 + 1]);
    assert(std::memcmp(&a.albedo[(T - 1) * T * 4], &b.albedo[0], T * 4) == 0);
    assert(std::memcmp(&a.normal[(T - 1) * T * 4], &b.normal[0], T * 4) == 0);
}

int main() {
    for (int variant = 0; variant < 2; variant++) {
        Params p;
        if (variant == 1) { p.segments = 24; p.texRes = 128; p.propDensity = 0.8f; }   // tablet-like LOD
        // a 3x3 block around a negative/positive boundary, far down the scroll (negative z)
        static ChunkData c[3][3];
        for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) buildChunk(p, i - 1, j - 14, c[j][i]);
        for (int j = 0; j < 3; j++) for (int i = 0; i < 2; i++) checkEdgeX(p, c[j][i], c[j][i + 1]);
        for (int j = 0; j < 2; j++) for (int i = 0; i < 3; i++) checkEdgeZ(p, c[j][i], c[j + 1][i]);
        // deterministic
        ChunkData again; buildChunk(p, 0, -13, again);
        assert(again.vtx == c[1][1].vtx && again.albedo == c[1][1].albedo && again.propPos == c[1][1].propPos);
        // vertex heights equal the shared field (lakes flat) at the same world point
        const int V = p.segments + 1;
        for (int k = 0; k < V * V; k += 37) {
            const float x = c[1][1].vtx[k * 3] + 0 * p.chunkSize, z = c[1][1].vtx[k * 3 + 2] + -13 * p.chunkSize;
            assert(std::fabs(c[1][1].vtx[k * 3 + 1] - surface(p, x, z)) < 1e-4f);
        }
        // props: inside their chunk, on (not floating above) the ground, never in water
        int props = 0;
        for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) for (const Prop& pr : c[j][i].props) {
            props++;
            assert(pr.x >= (i - 1) * p.chunkSize && pr.x < i * p.chunkSize);
            assert(pr.z >= (j - 14) * p.chunkSize && pr.z < (j - 13) * p.chunkSize);
            const float g = surface(p, pr.x, pr.z);
            assert(pr.y <= g + 1e-4f && pr.y > g - 0.6f);
            assert(height(p, pr.x, pr.z) > p.waterLevel);
        }
        assert(props > 100);
        // gun sites: dry, inside the lane, flattened, and no prop within the clearing
        for (long cell = -30; cell < -5; cell++) {
            const GunSite s = gunSite(p, cell);
            assert(std::fabs(s.x) <= p.gunLane && s.y > p.waterLevel);
            assert(std::fabs(height(p, s.x, s.z) - s.y) < 1e-3f);
            assert(std::fabs(height(p, s.x + 0.4f, s.z) - s.y) < 1e-3f);
            const int cz = (int)std::floor(s.z / p.chunkSize), cx = (int)std::floor(s.x / p.chunkSize);
            ChunkData d; buildChunk(p, cx, cz, d);
            for (const Prop& pr : d.props) assert(std::hypot(pr.x - s.x, pr.z - s.z) > p.gunClear);
        }
        // Ryan's pine: present in forests, and the prop triangle budget must not grow versus no pines
        {
            Params q = p; q.pineShare = 0.f;
            long tris[2] = {0, 0}; int types[2][PropTypeCount] = {};
            int nch = 0;
            for (int cz = -30; cz < -10; cz++) for (int cx = -2; cx < 2; cx++) {
                for (int k = 0; k < 2; k++) {
                    ChunkData d; buildChunk(k ? p : q, cx, cz, d);
                    tris[k] += (long)d.propPos.size() / 9;
                    for (const Prop& pr : d.props) types[k][pr.type]++;
                }
                nch++;
            }
            printf("  props/chunk over %d chunks: before %ld tris, with pines %ld tris (%+.1f%%)\n", nch, tris[0] / nch, tris[1] / nch, 100.0 * (tris[1] - tris[0]) / tris[0]);
            const char* nm[PropTypeCount] = {"conifer", "broadleaf", "bush", "rock", "boulder", "crystal", "pine"};
            for (int t = 0; t < PropTypeCount; t++) printf("    %-9s %6d -> %6d\n", nm[t], types[0][t], types[1][t]);
            assert(types[0][Pine] == 0 && types[1][Pine] > 20);
            assert(tris[1] <= tris[0]);
        }
        printf("planet_terrain_test variant %d ok: %d props in 9 chunks, tris/chunk terrain=%zu props=%zu\n", variant, props,
               c[1][1].idx.size() / 3, c[1][1].propPos.size() / 9);
    }
    return 0;
}
