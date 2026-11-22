#include "cutscene.hpp"
#include "entities.hpp"
#include "ui_theme.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>

using nlohmann::json;

Cutscene::~Cutscene() {
    if (heroModelLoaded_) UnloadModel(heroModel_);
}

bool Cutscene::load(const std::string& id, Assets& assets) {
    if (heroModelLoaded_) { UnloadModel(heroModel_); heroModel_ = {}; heroModelLoaded_ = false; }
    assets_ = &assets;
    steps_.clear(); actors_.clear(); idx_ = 0; t_ = 0; done_ = true;
    bgImage_ = nullptr; title_.clear(); fade_ = 1; fadeFrom_ = 1; fadeTo_ = 0;
    std::ifstream f(assets.path("cutscenes/" + id + ".json"));
    if (!f) { fprintf(stderr, "CUTSCENE missing %s\n", id.c_str()); return false; }
    try {
        for (auto& j : json::parse(f).value("steps", json::array())) {
            Step st;
            st.type = j.value("type", "wait");
            st.text = j.value("text", ""); st.speaker = j.value("speaker", ""); st.portrait = j.value("portrait", "");
            st.image = j.value("image", ""); st.sprite = j.value("sprite", ""); st.actor = j.value("actor", "");
            st.sfx = j.value("sfx", ""); st.duration = j.value("duration", 1.0f);
            st.from = j.value("from_alpha", 1.0f); st.to = j.value("to_alpha", 0.0f); st.amount = j.value("amount", 0.5f);
            if (j.contains("from")) st.a = {j["from"][0], j["from"][1]};
            if (j.contains("to")) st.b = {j["to"][0], j["to"][1]};
            if (j.contains("color")) st.color = {(unsigned char)j["color"][0].get<int>(), (unsigned char)j["color"][1].get<int>(), (unsigned char)j["color"][2].get<int>(), 255};
            steps_.push_back(st);
        }
    } catch (const std::exception& e) { fprintf(stderr, "CUTSCENE bad %s: %s\n", id.c_str(), e.what()); return false; }
    done_ = steps_.empty();
    if (!done_) begin();
    return !done_;
}

void Cutscene::begin() {
    if (idx_ >= steps_.size()) { done_ = true; return; }
    Step& st = steps_[idx_];
    t_ = 0; chars_ = 0;
    if (st.type == "background") { bgImage_ = assets_->texture(st.image); bgColor_ = st.color; }
    else if (st.type == "fade") { fadeFrom_ = st.from; fadeTo_ = st.to; }
    else if (st.type == "shake") shake_ = st.amount;
    else if (st.type == "title") title_ = st.text;
    else if (st.type == "actor") {
        Actor a; a.id = st.actor; a.a = st.a; a.b = st.b; a.dur = st.duration; a.size = st.amount > 0 ? st.amount : 1;
        a.flip = st.b.x < st.a.x;
        if (a.id == "hero") {
            heroModel_ = LoadModel(assets_->path("models/player_starfighter.glb").c_str());
            heroModelLoaded_ = heroModel_.meshCount > 0;
            if (!heroModelLoaded_) fprintf(stderr, "CUTSCENE missing 3D hero model\n");
        } else if (!st.sprite.empty()) a.sheet = &assets_->sheet(st.sprite, "side_scroller");
        for (auto& x : actors_) if (x.id == a.id) { x = a; return; }
        actors_.push_back(a);
    }
}

void Cutscene::update(float dt, Audio& audio) {
    if (done_) return;
    t_ += dt;
    shake_ = std::fmax(0.f, shake_ - dt);
    for (auto& a : actors_) a.t = std::fmin(a.t + dt, a.dur);
    bool tap = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);
    // SKIP button: top-right corner
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && GetMouseX() > GetScreenWidth() - 160 && GetMouseY() < 70) { done_ = true; return; }
    Step& st = steps_[idx_];
    bool next = false;
    if (st.type == "dialog") {
        chars_ += dt * 40;
        if (tap) { if (chars_ < st.text.size()) chars_ = (float)st.text.size(); else next = true; }
    } else if (st.type == "sfx") {
        if (st.sfx == "explosion") audio.play(Sfx::BigExplosion);
        else if (st.sfx == "pickup") audio.play(Sfx::Pickup);
        else if (st.sfx == "laser") audio.play(Sfx::Laser);
        next = true;
    } else if (st.type == "fade") {
        fade_ = fadeFrom_ + (fadeTo_ - fadeFrom_) * std::fmin(1.f, t_ / st.duration);
        next = t_ >= st.duration;
    } else if (st.type == "actor" || st.type == "background" || st.type == "title") {
        next = st.duration <= 0 || t_ >= st.duration || tap;  // non-blocking-ish: duration 0 = continue at once
    } else {  // wait, shake
        next = t_ >= st.duration || tap;
    }
    if (next) { idx_++; begin(); }
}

void Cutscene::draw(float W, float H, float s, const Texture2D& glow) const {
    Vector2 off{};
    if (shake_ > 0) off = {frand(-1, 1) * shake_ * 25 * s, frand(-1, 1) * shake_ * 25 * s};
    ClearBackground(bgColor_);
    if (bgImage_) DrawTexturePro(*bgImage_, {0, 0, (float)bgImage_->width, (float)bgImage_->height}, {off.x, off.y, W, H}, {}, 0, WHITE);
    else {  // placeholder backdrop: the title's still stars + light nebula (t231), and the scene's soft colour glow
        ui::backdrop();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(glow, {0, 0, (float)glow.width, (float)glow.height}, {W * 0.6f + off.x, H * 0.4f + off.y, W, H}, {W / 2, H / 2}, 0, Fade(bgColor_.r + bgColor_.g + bgColor_.b > 60 ? bgColor_ : Color{60, 80, 200, 255}, 0.5f));
        EndBlendMode();
    }
    for (auto& a : actors_) {
        float k = a.dur > 0 ? a.t / a.dur : 1;
        k = k * k * (3 - 2 * k);  // smoothstep
        Vector2 p{(a.a.x + (a.b.x - a.a.x) * k) * W + off.x, (a.a.y + (a.b.y - a.a.y) * k) * H + off.y};
        float sz = 160 * s * a.size;
        if (a.id == "hero" && heroModelLoaded_) {
            Camera3D camera{};
            camera.position = {0, 0, 20}; camera.target = {0, 0, 0}; camera.up = {0, 1, 0};
            camera.fovy = 8.f; camera.projection = CAMERA_ORTHOGRAPHIC;
            const float aspect = H > 0 ? W / H : 1.f;
            const float worldH = 8.f;
            const Vector3 pos{(p.x / W - 0.5f) * worldH * aspect, (0.5f - p.y / H) * worldH, 0};
            heroModel_.transform = MatrixMultiply(MatrixMultiply(MatrixRotateY(PI), MatrixScale(0.15f * a.size, 0.15f * a.size, 0.15f * a.size)), MatrixTranslate(pos.x, pos.y, 0));
            rlDrawRenderBatchActive();
            BeginMode3D(camera);
            DrawModel(heroModel_, {0, 0, 0}, 1.f, WHITE);
            EndMode3D();
        } else if (a.sheet) {
            int col = (int)(GetTime() * 20) % a.sheet->cols;
            DrawTexturePro(a.sheet->tex, a.sheet->frame(0, col, a.flip), {p.x, p.y, sz, sz}, {sz / 2, sz / 2}, 0, WHITE);
        } else DrawCircleV(p, sz * 0.2f, SKYBLUE);
    }
    if (!title_.empty()) {
        ui::banner(title_.c_str(), W / 2, H * 0.2f, 64 * s, s, 1.f, (float)GetTime());   // t231: the title's metal type
    }
    if (!done_ && steps_[idx_].type == "dialog") {
        const Step& st = steps_[idx_];
        Rectangle box{W * 0.05f, H * 0.66f, W * 0.9f, H * 0.3f};
        ui::panel(box, s);
        // portrait slot (placeholder frame until art exists)
        Rectangle por{box.x + 14 * s, box.y + 14 * s, box.height - 28 * s, box.height - 28 * s};
        const Texture2D* pt = assets_ ? assets_->texture(st.portrait) : nullptr;
        if (pt) DrawTexturePro(*pt, {0, 0, (float)pt->width, (float)pt->height}, por, {}, 0, WHITE);
        else { DrawRectangleRec(por, {30, 40, 70, 255}); DrawText("?", (int)(por.x + por.width / 2 - 10 * s), (int)(por.y + por.height / 2 - 20 * s), (int)(40 * s), GRAY); }
        int fs = (int)(24 * s);
        float tx = por.x + por.width + 16 * s;
        ui::captionL(st.speaker.c_str(), tx, box.y + 12 * s, (float)fs, s, ui::kAmber);
        std::string shown = st.text.substr(0, (std::size_t)std::fmin(chars_, (float)st.text.size()));
        ui::captionL(shown.c_str(), tx, box.y + 48 * s, 22 * s, s, RAYWHITE);
        if (chars_ >= st.text.size() && std::fmod(GetTime(), 1.0) < 0.6) ui::captionL("tap >", box.x + box.width - 80 * s, box.y + box.height - 34 * s, 20 * s, s, ui::kHint);
    }
    ui::captionL("SKIP >>", W - 140 * s, 20 * s, 24 * s, s, RAYWHITE, 0.7f);
    if (fade_ > 0) ui::backdrop(fade_);   // t231: fades go through the title's star field, never black
}
