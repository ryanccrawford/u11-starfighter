#pragma once
#include "assets.hpp"
#include "audio.hpp"
#include "raylib.h"
#include <string>
#include <vector>

// Data-driven cutscene: assets/cutscenes/<id>.json = { "steps": [ ... ] }
// Step types: background, actor, dialog, wait, fade, shake, sfx, title.
// Tap advances dialog (first tap completes the typewriter); SKIP button ends it.
class Cutscene {
public:
    ~Cutscene();
    bool load(const std::string& id, Assets& assets);
    void update(float dt, Audio& audio);
    void draw(float W, float H, float s, const Texture2D& glow) const;
    bool done() const { return done_; }

private:
    struct Step {
        std::string type, text, speaker, portrait, image, sprite, actor, sfx;
        float duration = 1, from = 0, to = 1, amount = 0.5f;
        Vector2 a{}, b{};  // actor path, normalized screen coords
        Color color = WHITE;
    };
    struct Actor { std::string id; const SpriteSheet* sheet = nullptr; Vector2 a{}, b{}; float t = 0, dur = 1, size = 1; bool flip = false; };

    void begin();
    std::vector<Step> steps_;
    std::vector<Actor> actors_;
    std::size_t idx_ = 0;
    float t_ = 0, fade_ = 1, fadeFrom_ = 1, fadeTo_ = 0, shake_ = 0, chars_ = 0;
    const Texture2D* bgImage_ = nullptr;
    Color bgColor_ = {6, 6, 18, 255};
    std::string title_;
    bool done_ = true;
    Assets* assets_ = nullptr;
    mutable Model heroModel_{};
    bool heroModelLoaded_ = false;
};
