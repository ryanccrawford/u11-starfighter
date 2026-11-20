#pragma once
#include "assets.hpp"
#include "data.hpp"
#include "view.hpp"
#include <vector>

// Parallax background in local space, so it scrolls toward the player in
// both modes. Each layer is a tiled PNG (Ryan's art) or a procedural placeholder.
class Background {
public:
    void build(const LevelDef& level, Assets& assets, const View& v, const Texture2D& glow);
    void update(float dt, float scrollMul = 1.f);  // scrollMul: edge-throttle world speed (spin unaffected)
    void draw() const;
    void setView(const View& v) { v_ = v; }
    float starAlpha = 1.f;
    const Texture2D* nebulaTex = nullptr;   // baked nebula / gas sky (Render3D): replaces the old glow-blob nebula layers
    float margin = 0.f;     // level 6 full screen: star field covers this much beyond the playfield (set before build)  // level 6: the 2D star layers fade out when the 3D chase camera takes over  // level 6: the view turns / shakes during play
    // 3D layers (planet3d / terrain3d) for Render3D: local pos + radius px, or terrain scroll offset px
    struct Bg3D {
        bool terrain;
        std::string model, cloudModel;
        Vector2 pos;
        float sizePx, offset, rot, cloudRot;
        Color col;
    };
    void collect3d(std::vector<Bg3D>& out) const;
    float terrainOffset() const;       // scroll offset px of the terrain3d layer (0 if none)
    void setTerrainOffset(float px);   // continue a ground scroll from the previous level

private:
    struct Item { Vector2 pos; float size, rot, spin, cloudRot, cloudSpin; Color col; };
    struct Layer { BgLayer def; const Texture2D* tex = nullptr; float offset = 0; std::vector<Item> items; };
    std::vector<Layer> layers_;
    Texture2D glow_{};
    View v_{};
};
