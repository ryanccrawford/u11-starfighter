#include "pc_display.hpp"

#if defined(_WIN32)

#include "raylib.h"
#include "rlgl.h"
#include "external/glad.h"

#include <algorithm>
#include <cstdio>

namespace {

int gReqAA = 4;
int gMaxAA = 0;
int gEffAA = 0;
int gFpsLimit = 144;
bool gVsync = true;
bool gInFrame = false;
bool gReady = false;

int gW = 0, gH = 0;
unsigned gFbo = 0, gColorRbo = 0, gDepthRbo = 0, gResolveFbo = 0, gResolveTex = 0;
int gResolveW = 0, gResolveH = 0;

const int kAAOpts[] = {0, 2, 4, 8, 16};
const int kFpsOpts[] = {30, 60, 75, 90, 120, 144, 0};

bool allowedAA(int v) {
    for (int o : kAAOpts) if (o == v) return true;
    return false;
}
bool allowedFps(int v) {
    for (int o : kFpsOpts) if (o == v) return true;
    return false;
}

int clampAA(int requested) {
    if (requested < 2) return 0;
    int cap = gMaxAA;
    if (cap < 2) return 0;
    if (requested > cap) requested = cap;
    // Snap down to a supported option so we never request an odd sample count.
    int best = 0;
    for (int o : kAAOpts) if (o <= requested) best = o;
    return best;
}

void destroyFbo() {
    if (gResolveFbo) { glDeleteFramebuffers(1, &gResolveFbo); gResolveFbo = 0; }
    if (gResolveTex) { glDeleteTextures(1, &gResolveTex); gResolveTex = 0; }
    gResolveW = gResolveH = 0;
    if (gFbo) { glDeleteFramebuffers(1, &gFbo); gFbo = 0; }
    if (gColorRbo) { glDeleteRenderbuffers(1, &gColorRbo); gColorRbo = 0; }
    if (gDepthRbo) { glDeleteRenderbuffers(1, &gDepthRbo); gDepthRbo = 0; }
    gW = gH = 0;
    gEffAA = 0;
}

bool tryCreateFbo(int w, int h, int samples) {
    unsigned fbo = 0, color = 0, depth = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);

    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (color) glDeleteRenderbuffers(1, &color);
        if (depth) glDeleteRenderbuffers(1, &depth);
        return false;
    }
    destroyFbo();
    gFbo = fbo;
    gColorRbo = color;
    gDepthRbo = depth;
    gW = w;
    gH = h;
    gEffAA = samples;
    return true;
}

void ensureFbo(int w, int h) {
    const int want = clampAA(gReqAA);
    if (want < 2) {
        if (gFbo) destroyFbo();
        gEffAA = 0;
        return;
    }
    if (gFbo && gW == w && gH == h && gEffAA == want) return;

    int samples = want;
    bool ok = false;
    while (samples >= 2) {
        if (tryCreateFbo(w, h, samples)) { ok = true; break; }
        fprintf(stderr, "PC display: %dx MSAA FBO failed at %dx%d, stepping down\n", samples, w, h);
        int next = 0;
        for (int o : kAAOpts) if (o < samples) next = o;
        samples = next;
    }
    if (!ok) {
        destroyFbo();
        fprintf(stderr, "PC display: MSAA unavailable, drawing to the backbuffer\n");
        return;
    }
    fprintf(stderr, "PC display: MSAA %dx FBO %dx%d (requested %dx, max %dx)\n",
            gEffAA, w, h, gReqAA, gMaxAA);
}

}  // namespace

void pcDisplayInit() {
    gMaxAA = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &gMaxAA);
    if (gMaxAA < 0) gMaxAA = 0;
    gReady = true;
    gEffAA = clampAA(gReqAA);
    fprintf(stderr, "PC display: GL_MAX_SAMPLES=%d, AA default request=%dx effective=%dx, vsync=%d, fps cap=%d\n",
            gMaxAA, gReqAA, gEffAA, gVsync ? 1 : 0, gFpsLimit);
}

void pcDisplayShutdown() {
    destroyFbo();
    gInFrame = false;
    gReady = false;
}

void pcDisplaySetAA(int requestedSamples) {
    gReqAA = allowedAA(requestedSamples) ? requestedSamples : 4;
}

void pcDisplaySetVsync(bool on) { gVsync = on; }

void pcDisplaySetFpsLimit(int fps) {
    gFpsLimit = allowedFps(fps) ? fps : 144;
}

void pcDisplayApplyTiming() {
    const int refresh = pcDisplayMonitorRefresh();
    if (gVsync) {
        SetWindowState(FLAG_VSYNC_HINT);
        const int cap = (gFpsLimit <= 0) ? refresh : std::min(gFpsLimit, refresh);
        SetTargetFPS(cap > 0 ? cap : 60);
    } else {
        ClearWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(gFpsLimit);  // 0 = uncapped
    }
    fprintf(stderr, "PC display: vsync=%d fps_limit=%d refresh=%d target_fps=%d\n",
            gVsync ? 1 : 0, gFpsLimit, refresh,
            gVsync ? ((gFpsLimit <= 0) ? refresh : std::min(gFpsLimit, refresh)) : gFpsLimit);
}

int  pcDisplayRequestedAA() { return gReqAA; }
int  pcDisplayEffectiveAA() { return gReady ? clampAA(gReqAA) : gReqAA; }
int  pcDisplayMaxAA() { return gMaxAA; }
bool pcDisplayVsync() { return gVsync; }
int  pcDisplayFpsLimit() { return gFpsLimit; }

int pcDisplayMonitorRefresh() {
    int hz = GetMonitorRefreshRate(GetCurrentMonitor());
    return hz > 0 ? hz : 60;
}

bool pcDisplayActive() { return gInFrame && gFbo != 0 && gEffAA >= 2; }

void pcDisplayBeginFrame() {
    if (!gReady) return;
    const int w = GetScreenWidth(), h = GetScreenHeight();
    if (w < 2 || h < 2) return;
    ensureFbo(w, h);
    if (!gFbo) { gInFrame = false; return; }
    rlDrawRenderBatchActive();
    rlEnableFramebuffer(gFbo);
    glViewport(0, 0, w, h);
    gInFrame = true;
}

void pcDisplayRebind() {
    if (!pcDisplayActive()) return;
    rlEnableFramebuffer(gFbo);
    glViewport(0, 0, gW, gH);
}

void pcDisplayEndFrame() {
    if (!gInFrame) return;
    gInFrame = false;
    if (!gFbo) return;
    rlDrawRenderBatchActive();
    if (!gResolveFbo) glGenFramebuffers(1, &gResolveFbo);
    // Two-step resolve: MSAA FBO -> single-sample texture, then that texture -> backbuffer.
    // A direct MSAA blit can fail silently depending on the default framebuffer's size.
    if (!gResolveTex || gResolveW != gW || gResolveH != gH) {
        if (gResolveTex) glDeleteTextures(1, &gResolveTex);
        gResolveTex = 0;
        glGenTextures(1, &gResolveTex);
        glBindTexture(GL_TEXTURE_2D, gResolveTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gW, gH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gResolveW = gW; gResolveH = gH;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, gResolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gResolveTex, 0);
    const GLenum resolveStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    while (glGetError() != GL_NO_ERROR) {}
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gResolveFbo);
    glBlitFramebuffer(0, 0, gW, gH, 0, 0, gW, gH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    const GLenum e1 = glGetError();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gResolveFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, gW, gH, 0, 0, gW, gH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    const GLenum e2 = glGetError();
    if (resolveStatus != GL_FRAMEBUFFER_COMPLETE || e1 || e2) {
        fprintf(stderr, "PC display: resolve failed status=0x%x errors=%u,%u fbo=%dx%d\n",
                (unsigned)resolveStatus, (unsigned)e1, (unsigned)e2, gW, gH);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rlDisableFramebuffer();
}

void pcDisplayResolveToTexture(unsigned texId, int x, int y, int w, int h) {
    if (!texId || w <= 0 || h <= 0) return;
    if (!gResolveFbo) glGenFramebuffers(1, &gResolveFbo);
    if (pcDisplayActive()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gFbo);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gResolveFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    if (gInFrame && gFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
        rlEnableFramebuffer(gFbo);
        glViewport(0, 0, gW, gH);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        rlDisableFramebuffer();
    }
}

#endif
