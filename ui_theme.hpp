// U11 Starfighter menu / overlay theme (t231): the look of the t228 title, shared by every other
// menu, title card and overlay. Oxanium type (metal banners, amber headings, soft captions), the
// title's still star field + light nebula as the backdrop, and keyboard focus for the tap buttons.
#pragma once
#include "raylib.h"
#include <vector>

namespace ui {
// fonts: display = Oxanium at banner size (space .. Z), text = Oxanium for captions / body (full ASCII)
const Font& display();
const Font& text();

// sky: the title's nebula texture (Render3D::nebulaTexture) + the soft glow sprite; set once per frame by
// any screen that has them (cheap pointer copies). Without a nebula the backdrop is stars only.
void setSky(const Texture2D* nebula, const Texture2D* glow);
// full-screen backdrop: the title's still far stars + light nebula (rendered once per screen size,
// then one quad per frame) and a faint warm sunrise glow at the title's sun. alpha < 1 = over a scene.
void backdrop(float alpha = 1.f);

// typography (s = UI scale, a = alpha)
void bold(const Font& f, const char* t, Vector2 p, float size, float spacing, Color c, float weight);
Vector2 measure(const Font& f, const char* t, float size, float spacing);
// brushed-metal title like "U11". fx = sky-lit upper half + sun glint (scissor: only outside the scaled
// gameplay render target); tint = the steel colour (GAME OVER: a red steel)
void banner(const char* t, float cx, float y, float size, float s, float a = 1.f, float time = 0.f, bool fx = true,
            Color tint = {150, 164, 184, 255});
void heading(const char* t, float cx, float y, float size, float s, float a = 1.f);                    // amber, spaced, with rule ("STARFIGHTER")
void caption(const char* t, float cx, float y, float size, float s, Color c, float a = 1.f);         // centred Oxanium caption
void captionL(const char* t, float x, float y, float size, float s, Color c, float a = 1.f);         // left-aligned
constexpr Color kAmber{255, 172, 72, 255}, kCaption{190, 205, 230, 255}, kHint{150, 165, 190, 255};
void dim(float W, float H, float a);   // overlay over gameplay: navy, never black
void panel(Rectangle r, float s, float a = 1.f);   // dark glass panel with the button outline colour

// keyboard focus for immediate-mode tap buttons: arrows / WASD move to the nearest button in that
// direction (last frame's layout), Enter / Space / keypad Enter press it; the mouse hovering a button
// moves the focus there. Buttons still work by tap exactly as before.
struct Nav {
    int focus = -1, activate = -1, preferred = 0;
    std::vector<Rectangle> prev, cur; std::vector<bool> prevEn, curEn;
    void begin(bool keys = true, bool space = true);
    bool item(Rectangle r, bool enabled, float s);   // after the button: draws the ring; true = pressed by key
    void end();
};
void focusRing(Rectangle r, float s);
bool backPressed();      // Esc / Backspace
bool confirmPressed();   // Enter / keypad Enter

bool titleMusicReady(float titleRevealTime);   // (title.cpp) the title scene is up: start the theme without a load stall
}  // namespace ui
