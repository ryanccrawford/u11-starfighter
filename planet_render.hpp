#pragma once
// GPU side of the Level 6 chunked ground (planet_terrain.hpp): background worker threads build
// chunk data ahead of the scroll, the render thread uploads a few per frame and draws terrain,
// blob shadows and batched props (one draw per chunk each) lit by the single sun.
#include "raylib.h"
#include "planet_terrain.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

class PlanetRenderer {
public:
    struct Stats { int chunks = 0, cached = 0, pending = 0, terrainTris = 0, propTris = 0, shadowQuads = 0, draws = 0; float dMin = 0, dMax = 0; };
    ~PlanetRenderer() { shutdown(); }
    bool init(const planet::Params& p, int workers);
    void shutdown();
    bool ready() const { return inited_; }
    const planet::Params& params() const { return p_; }
    // World rectangle wanted (x0..x1, z0..z1): missing chunks are queued nearest-first; chunks well
    // outside it are recycled (their textures are reused).
    void request(float x0, float x1, float z0, float z1);
    void pump(int maxUploads);
    void setLight(Vector3 toSun, float amb);
    // Inside BeginMode3D. Chunk (cx,cz) is drawn at ground (cx*C, base, cz*C + scroll), then `layer`.
    void draw(const Matrix& layer, float base, float scroll, float x0, float x1, float z0, float z1,
              Vector3 eye, float propFar);
    bool covered(float x0, float x1, float z0, float z1) const;  // every chunk of the rect resident
    Stats stats;
private:
    struct Gpu {
        int cx = 0, cz = 0;
        Mesh terrain{}, props{}, shadows{};
        Texture2D alb{}, nrm{};
        bool hasProps = false, hasShadows = false;
        int terrainTris = 0, propTris = 0, shadowQuads = 0;
        float minH = 0, maxH = 0;
    };
    static long key(int cx, int cz) { return ((long)cx << 20) ^ (long)(cz & 0xFFFFF); }
    void worker();
    void release(Gpu& g);
    planet::Params p_{};
    bool inited_ = false;
    Shader terrainSh_{}, propSh_{}, shadowSh_{};
    Material terrainMat_{}, propMat_{}, shadowMat_{};
    Texture2D detail_{};
    int tLocOrigin_ = -1, tLocSun_ = -1, tLocAmb_ = -1, pLocSun_ = -1, pLocAmb_ = -1, sLocSun_ = -1;
    std::map<long, Gpu> chunks_;
    std::vector<std::pair<Texture2D, Texture2D>> texPool_;
    // worker side
    std::vector<std::thread> threads_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::pair<int, int>> queue_;
    std::set<long> queued_;                       // queued or being built
    std::vector<std::unique_ptr<planet::ChunkData>> done_;
    bool quit_ = false;
    float rx0_ = 0, rx1_ = 0, rz0_ = 0, rz1_ = 0; bool haveReq_ = false;
};
