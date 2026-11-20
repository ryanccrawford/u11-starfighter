// t235: Liftoff (level_07) and Planetfall (level_06) flight sound, driven every frame by the approach
// state that updateApproach() has just computed (phase timers, heat, shake, clouds, altitude, plume),
// so the audio follows exactly what the camera and the effects do. The sounds are procedural WAVs
// (tools/gen_plasma_flight_sfx.py) in single mixer slots (Audio::flight): nothing can stack, and
// Audio::engine(false) - pause, death, level end, menus - silences them.
#include "game.hpp"
#include "trench_run.hpp"   // t267: g_trenchAudio (moon descent hum / finale music dip)
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
float sm(float a, float b, float x) { x = (x - a) / (b - a); x = x < 0.f ? 0.f : x > 1.f ? 1.f : x; return x * x * (3.f - 2.f * x); }
float c01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
}  // namespace

void Game::updateFlightAudio(float /*dt*/) {
    static int lvl = -1; static float lastLt = 0.f; static bool ignited = false, whooshed = false;
    if (levelIdx != lvl || levelTime + 0.5f < lastLt) { lvl = levelIdx; ignited = false; whooshed = false; }   // a new run of a level
    lastLt = levelTime;
    Audio::Flight f;
    const LevelDef& L = db.levels()[levelIdx];
    if (appr.on && appr.liftoff) {
        // Liftoff (times / altitudes as in game.cpp: pad 0-5 s, climb from kLiftClimb 84 s, deck at 0.0007-0.003
        // radii, the plume back to normal by 0.09 radii, space beyond ~0.15)
        const float lt = levelTime;
        const float pad = sm(0.25f, 2.0f, lt) * (1.f - sm(4.0f, 7.0f, lt)) * c01(appr.liftPower / 1.35f);   // spool-up on the pad
        const float climb = c01((appr.flameBoost - 1.f) / 1.5f);                                            // the 2.5x punch-out plume
        const float air = sm(84.f, 87.f, lt) * (1.f - sm(0.010f, 0.08f, appr.alt));                         // thick air, thinning out
        const float buffet = c01(appr.shakeAmp / 0.35f);                                                     // the deck's camera buffeting
        f.roar = std::fmax(0.6f * pad, climb);
        f.roarRate = 0.85f + 0.2f * f.roar;
        f.wind = air * (0.45f + 0.55f * buffet);
        f.windRate = 0.8f + 0.4f * buffet;
        f.drone = 0.45f * sm(0.15f, 0.6f, appr.alt);                                                         // out in space
        f.ignition = !ignited && lt < 1.0f; ignited = true;                                                 // (a run started past the pad: none)
        f.whoosh = !whooshed && buffet > 0.5f; whooshed = whooshed || f.whoosh;                               // into the cloud deck
        f.engineDuck = 0.6f * std::fmax(f.roar, 0.5f * f.wind);
        f.musicDuck = std::fmax(f.roar, 0.7f * f.wind);
    } else if (appr.on && L.approachEnd > 0.f) {
        // Planetfall (authored timeline t, as in game.cpp: entry burn 80-100, peak heat ~90, clouds 100-118, overhead 118)
        const float t = appr.t, heat = c01(appr.heat), shk = c01(appr.shakeAmp);
        f.drone = 0.8f * (1.f - sm(78.f, 88.f, t));                                                          // the quiet approach
        f.entry = heat * (0.75f + 0.25f * shk);
        f.entryRate = 0.9f + 0.2f * heat;
        f.roar = c01(0.55f * heat + 0.45f * shk);
        f.roarRate = 0.75f + 0.15f * heat;
        f.wind = std::fmax(std::fmax(0.8f * sm(82.f, 92.f, t) * (1.f - sm(106.f, 114.f, t)), 0.75f * c01(appr.clouds)), 0.32f * sm(110.f, 118.f, t));
        f.windRate = 0.85f + 0.3f * heat + 0.1f * c01(appr.clouds);
        f.whoosh = !whooshed && appr.clouds > 0.3f; whooshed = whooshed || f.whoosh;                         // through the clouds
        f.engineDuck = 0.5f * heat;
        f.musicDuck = std::fmax(heat, 0.6f * c01(appr.clouds));
    } else if (L.id == "level_08" && g_trenchAudio.stamp > 0.0 && GetTime() - g_trenchAudio.stamp < 0.25) {
        // t267: Moon Base: no atmosphere: an engine / approach hum (drone + a low roar), no entry crackle, no wind
        f.drone = g_trenchAudio.drone; f.roar = g_trenchAudio.roar; f.roarRate = g_trenchAudio.roarRate;
        f.musicDuck = g_trenchAudio.musicDuck; f.engineDuck = g_trenchAudio.engineDuck;
    }
    audio.flight(f);
    static const bool log = getenv("SFX_LOG") != nullptr;
    if (log && appr.on && (int)(levelTime * 2.f) != (int)((levelTime - GetFrameTime()) * 2.f))
        fprintf(stderr, "FLIGHT lt=%.1f t=%.1f heat=%.2f shake=%.2f clouds=%.2f alt=%.5f plume=%.2f power=%.2f -> roar=%.2f wind=%.2f entry=%.2f drone=%.2f mduck=%.2f\n",
                levelTime, appr.t, appr.heat, appr.shakeAmp, appr.clouds, appr.alt, appr.flameBoost, appr.liftPower, f.roar, f.wind, f.entry, f.drone, f.musicDuck);
}
