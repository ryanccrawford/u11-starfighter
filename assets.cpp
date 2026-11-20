#include "assets.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>

using nlohmann::json;

Rectangle SpriteSheet::frame(int row, int col, bool flip) const {
    row = row % rows; col = col % cols;
    float w = (float)frameW, h = (float)frameH;
    return {col * w, row * h, flip ? -w : w, h};
}

std::array<Vector2, 2> SpriteSheet::nozzle(int row, int col) const {
    std::size_t i = (std::size_t)((row % rows) * cols + (col % cols));
    if (i < nozzles.size()) return nozzles[i];
    return {Vector2{0.27f, 0.57f}, Vector2{0.27f, 0.45f}};  // sensible default: tail of a right-facing ship
}

float SpriteSheet::pitchDeg(int row, int col) const {
    std::size_t i = (std::size_t)((row % rows) * cols + (col % cols));
    float p = i < pitch.size() ? pitch[i] * pitchGain : 0.f;
    return p > pitchMax ? pitchMax : p < -pitchMax ? -pitchMax : p;
}

void Assets::init(const std::string& root) { root_ = root; }

void Assets::shutdown() {
    for (auto& t : owned_) UnloadTexture(t);
    owned_.clear(); sheets_.clear(); textures_.clear(); for (auto& h : hot_) h = {};
}

void Assets::reload() { shutdown(); }

const Texture2D* Assets::texture(const std::string& rel) {
    if (rel.empty()) return nullptr;
    auto it = textures_.find(rel);
    if (it != textures_.end()) return it->second.id ? &it->second : nullptr;
    Texture2D t{};
    std::string p = path(rel);
    if (FileExists(p.c_str())) {
        t = LoadTexture(p.c_str());
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        owned_.push_back(t);
    } else {
        fprintf(stderr, "ASSET missing texture %s (using fallback)\n", p.c_str());
    }
    textures_[rel] = t;
    return t.id ? &textures_[rel] : nullptr;
}

// Procedural stand-in ship: a simple arrow shape facing right, 8x5 frames.
static Texture2D makePlaceholderSheet() {
    Image img = GenImageColor(1024, 640, BLANK);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 8; c++) {
            int x = c * 128, y = r * 128, tilt = (r == 3 ? -12 : r == 4 ? 12 : 0);
            ImageDrawTriangle(&img, {(float)x + 110, (float)y + 64 + tilt}, {(float)x + 25, (float)y + 40}, {(float)x + 25, (float)y + 88}, LIGHTGRAY);
            ImageDrawRectangle(&img, x + 30, y + 58, 40, 12, DARKGRAY);
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

const SpriteSheet& Assets::sheet(const std::string& rel, const std::string& section) {
    // hot path: called per entity per pass each frame; def strings have stable addresses
    for (auto& c : hot_) if (c.rel == rel.data() && c.sec == section[0] && c.sheet) return *c.sheet;
    const std::string key = rel + "#" + section;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) { hot_[hotNext_++ % 8] = {rel.data(), section[0], &it->second}; return it->second; }
    SpriteSheet sh;
    std::string pngRel;
    std::ifstream f(path(rel));
    if (f) {
        try {
            json j = json::parse(f);
            if (j.contains("frame_size_px")) { sh.frameW = j["frame_size_px"][0]; sh.frameH = j["frame_size_px"][1]; }
            const json& s = j.contains(section) ? j[section] : j;
            int sheetW = s.contains("sheet_size_px") ? s["sheet_size_px"][0].get<int>() : sh.frameW * 8;
            int sheetH = s.contains("sheet_size_px") ? s["sheet_size_px"][1].get<int>() : sh.frameH * 5;
            sh.cols = s.value("columns", sheetW / sh.frameW); sh.rows = s.value("rows", sheetH / sh.frameH);
            pngRel = s.value("file", "");
            if (!pngRel.empty() && pngRel.find('/') == std::string::npos) {  // file is relative to the json
                auto slash = rel.find_last_of('/');
                if (slash != std::string::npos) pngRel = rel.substr(0, slash + 1) + pngRel;
            }
            if (s.contains("nozzles_by_frame"))
                for (auto& row : s["nozzles_by_frame"])
                    for (auto& fr : row) {
                        std::array<Vector2, 2> n{};
                        for (int k = 0; k < 2 && k < (int)fr.size(); k++) n[k] = {fr[k]["x"], fr[k]["y"]};
                        sh.nozzles.push_back(n);
                    }
            if (s.contains("pitch_deg_by_frame"))  // measured from the art by tools/measure_pitch.py
                for (auto& row : s["pitch_deg_by_frame"])
                    for (auto& v : row) sh.pitch.push_back(v.get<float>());
            sh.pitchGain = s.value("pitch_gain", sh.pitchGain);
            sh.pitchMax = s.value("pitch_max_deg", sh.pitchMax);
            sh.aimPitchMin = s.value("aim_pitch_min_deg", sh.aimPitchMin);
        } catch (const std::exception& e) {
            fprintf(stderr, "ASSET bad json %s: %s\n", rel.c_str(), e.what());
        }
    } else {
        fprintf(stderr, "ASSET missing sheet %s (placeholder)\n", rel.c_str());
    }
    if (const Texture2D* t = texture(pngRel)) sh.tex = *t;
    else {
        sh.tex = makePlaceholderSheet(); owned_.push_back(sh.tex);
        sh.frameW = sh.frameH = 128; sh.cols = 8; sh.rows = 5; sh.placeholder = true;
    }
    return sheets_[key] = sh;
}
