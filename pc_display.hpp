#pragma once

// PC-only display settings: MSAA (multisampled FBO, resolved by blit), VSync, and an FPS cap.
// Changeable at runtime without restarting. The tablet / ARM64 build does not use this path.

#if defined(_WIN32)

void pcDisplayInit();
void pcDisplayShutdown();

void pcDisplaySetAA(int requestedSamples);  // 0, 2, 4, 8, or 16
void pcDisplaySetVsync(bool on);
void pcDisplaySetFpsLimit(int fps);         // 0 = unlimited; else 30/60/75/90/120/144
void pcDisplayApplyTiming();

int  pcDisplayRequestedAA();
int  pcDisplayEffectiveAA();  // min(requested, GL_MAX_SAMPLES), 0 if off
int  pcDisplayMaxAA();
bool pcDisplayVsync();
int  pcDisplayFpsLimit();
int  pcDisplayMonitorRefresh();

void pcDisplayBeginFrame();  // bind the MSAA FBO after BeginDrawing
void pcDisplayEndFrame();    // resolve to the backbuffer before EndDrawing
void pcDisplayRebind();      // restore the MSAA FBO after an offscreen pass
bool pcDisplayActive();

// Copy a lower-left GL rectangle of the current target into texId (for heat-mirage sampling).
void pcDisplayResolveToTexture(unsigned texId, int x, int y, int w, int h);

#endif
