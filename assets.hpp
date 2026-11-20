#pragma once
#include "raylib.h"
#include <array>
#include <map>
#include <string>
#include <vector>

// A sprite sheet in Ryan's *_sprites.json format (side_scroller section):
// fixed-size frames in a grid, rows = states, cols = animation frames,
// plus 2 normalized nozzle points per frame for attaching engine flames.
struct SpriteSheet {
    Texture2D tex{};
    int frameW = 128, frameH = 128, cols = 8, rows = 5;
    std::vector<std::array<Vector2, 2>> nozzles;  // index row*cols+col
    std::vector<float> pitch;                       // side_scroller.pitch_deg_by_frame (+ = nose up), row*cols+col
    float pitchGain = 0.5f, pitchMax = 12, aimPitchMin = 8;  // restraint: applied = clamp(measured*gain, +-max)
    bool placeholder = false;                       // true if the PNG was missing

    Rectangle frame(int row, int col, bool flip) const;
    std::array<Vector2, 2> nozzle(int row, int col) const;
    float pitchDeg(int row, int col) const;  // applied (restrained) nose pitch of this frame, 0 if not authored
};

// Loads + caches textures/sheets. Missing files never crash: they fall back
// to a placeholder so levels are playable before the art exists.
class Assets {
public:
    void init(const std::string& root);
    void shutdown();
    // section = "side_scroller" | "top_down" (Ryan's JSON has both views)
    const SpriteSheet& sheet(const std::string& jsonPath, const std::string& section);
    const Texture2D* texture(const std::string& path);       // nullptr if missing
    std::string path(const std::string& rel) const { return root_ + "/" + rel; }
    void reload();  // drop caches (hot reload)

private:
    std::string root_;
    std::map<std::string, SpriteSheet> sheets_;
    struct Hot { const char* rel = nullptr; char sec = 0; const SpriteSheet* sheet = nullptr; };
    Hot hot_[8]{}; unsigned hotNext_ = 0;
    std::map<std::string, Texture2D> textures_;
    std::vector<Texture2D> owned_;
};
