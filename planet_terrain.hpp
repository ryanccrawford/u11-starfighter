#pragma once
// Level 6 "Planetfall" ground: one deterministic, seamless height/colour field sampled in world
// units (the overhead playfield is CameraSystem::kViewHeight = 14 units tall). Chunks of it are
// generated on the fly (off the render thread) and recycled behind the scroll. Every sample is a
// pure function of its GLOBAL lattice index, so two neighbouring chunks compute bit-identical
// values on their shared edge (see tests/planet_terrain_test.cpp). No raylib in here.
#include <cstdint>
#include <vector>

namespace planet {

struct Params {
    std::uint32_t seed = 6006;
    float chunkSize = 8.f;     // world units; a power of two keeps chunk origins and edges exact
    int segments = 32;         // terrain quads per chunk edge (geometry LOD knob)
    int texRes = 256;          // albedo / normal texels per chunk edge (+1 shared border sample)
    float heightScale = 1.f;
    float waterLevel = -1.6f;
    float propDensity = 1.f;   // 0 = no props
    int propDetail = 1;        // 0 = fewer sides per prop
    float gunSpacing = 11.f;   // world units between ground-gun sites along the scroll
    float gunLane = 5.f;       // gun sites stay within |x| < gunLane (inside the playfield)
    float gunClear = 1.1f;     // flattened, prop-free radius around a gun site
    float pineShare = 0.5f;    // share of dense-forest 3x3 blocks that hold one big pine (Ryan's tree)
};

// Raw field (no gun pads), final height (gun pads flattened), and the drawn surface (lakes flat).
float rawHeight(const Params& p, float x, float z);
float height(const Params& p, float x, float z);
inline float surface(const Params& p, float x, float z) { const float h = height(p, x, z); return h > p.waterLevel ? h : p.waterLevel; }

struct GunSite { float x = 0, z = 0, y = 0; long cell = 0; };
long gunCellOf(const Params& p, float z);
GunSite gunSite(const Params& p, long cell);   // deterministic: the flattest of a few candidates in the cell

enum PropType : std::uint8_t { Conifer = 0, Broadleaf, Bush, Rock, Boulder, Crystal, Pine, PropTypeCount };   // Pine: Ryan's tree (pine_lod.inc)
struct Prop { std::uint8_t type; float x, z, y, scale, yaw, tint; };

struct ChunkData {
    int cx = 0, cz = 0, seg = 0, tex = 0;
    // Terrain mesh, chunk-local (origin = chunk corner cx*C, cz*C): positions xyz, texcoords.
    std::vector<float> vtx, uv;
    std::vector<unsigned short> idx;
    // (tex+1)^2 RGBA texels at global lattice points: albedo (A = water depth) and normal (xyz*0.5+0.5).
    std::vector<unsigned char> albedo, normal;
    // Props: list + one batched triangle soup (chunk-local positions, normals, RGBA colours).
    std::vector<Prop> props;
    std::vector<float> propPos, propNrm;
    std::vector<unsigned char> propCol;
    // Blob-shadow quads (4 verts per caster): centre xyz, corner uv, (radius, height).
    std::vector<float> shPos, shUv, shUv2;
    std::vector<unsigned short> shIdx;
    float minH = 0, maxH = 0;
};

// Global lattice helpers (shared by generator and tests).
inline double vertexCoord(const Params& p, long globalIndex) { return (double)globalIndex * (p.chunkSize / p.segments); }
inline double texelCoord(const Params& p, long globalIndex) { return (double)globalIndex * (p.chunkSize / p.texRes); }
void buildChunk(const Params& p, int cx, int cz, ChunkData& out);

}  // namespace planet
