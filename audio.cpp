#include "audio.hpp"
#include "entities.hpp"
#include "config.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

namespace {
constexpr int kRate = 22050;

// Simple synth: frequency sweep with optional noise mix and exponential decay.
Sound synth(float dur, float f0, float f1, float noise, float decay, bool square) {
    const int n = (int)(kRate * dur);
    auto* data = (short*)MemAlloc(n * sizeof(short));
    float phase = 0, lp = 0;
    for (int i = 0; i < n; i++) {
        float k = (float)i / n;
        float f = f0 * std::pow(f1 / f0, k);  // exponential sweep
        phase += f / kRate;
        float tone = square ? (std::fmod(phase, 1.f) < 0.5f ? 1.f : -1.f) : std::sin(phase * 2 * PI);
        float nz = frand(-1, 1);
        lp += (nz - lp) * 0.25f;                 // low-passed noise = rumble
        float v = tone * (1 - noise) + lp * noise * 2;
        float env = std::exp(-k * decay) * std::fmin(1.f, i / 60.f);
        data[i] = (short)(std::fmax(-1.f, std::fmin(1.f, v * env)) * 9000);
    }
    Wave w{(unsigned)n, kRate, 16, 1, data};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// Layered explosion: (1) sharp crack, (2) sub-bass body sweep with saturated 2nd/3rd harmonics so the
// low end still reads on small tablet speakers, (3) low-passed rumble with a falling cutoff, (4) sparse
// debris crackle in the tail. Soft-clipped (tanh) and peak-normalised, so it is loud but never clips.
Sound boom(float dur, float fHi, float fLo, float bodyTau, float rumbleTau, float debris, const char* dumpName) {
    const int n = (int)(kRate * dur);
    auto* data = (short*)MemAlloc(n * sizeof(short));
    std::vector<float> buf(n);
    float ph = 0, r1 = 0, r2 = 0, hp = 0, prevN = 0, d1 = 0, dEnv = 0, peak = 1e-6f;
    for (int i = 0; i < n; i++) {
        const float t = (float)i / kRate;
        // 1) crack: 1-pole high-passed noise burst, ~6 ms
        float nz = frand(-1, 1);
        hp = 0.7f * (hp + nz - prevN); prevN = nz;
        float crack = hp * std::exp(-t / 0.006f) * 0.9f;
        // 2) body: exponential pitch drop, 2 ms attack
        float f = fLo + (fHi - fLo) * std::exp(-t / 0.08f);
        ph += f / kRate;
        float w = 2 * PI * ph;
        float body = (std::sin(w) + 0.55f * std::sin(2 * w) + 0.3f * std::sin(3 * w)) * std::exp(-t / bodyTau) * std::fmin(1.f, t / 0.002f);
        body = std::tanh(body * 1.6f);                       // warm saturation = audible "punch"
        // 3) rumble: 2-pole low-pass noise, cutoff sweeps ~1.6 kHz -> 140 Hz
        float fc = 140 + 1500 * std::exp(-t / 0.12f);
        float a = 1 - std::exp(-2 * PI * fc / kRate);
        r1 += (frand(-1, 1) - r1) * a; r2 += (r1 - r2) * a;
        float rumble = r2 * 2.6f * std::exp(-t / rumbleTau) * std::fmin(1.f, t / 0.004f);
        // 4) debris: random clicks after 40 ms, band-limited by a 1-pole low-pass (~3 kHz)
        if (t > 0.04f && frand(0, 1) < debris * std::exp(-t / (rumbleTau * 1.2f))) dEnv = frand(0.3f, 1.f);
        dEnv *= 0.985f;
        d1 += (frand(-1, 1) * dEnv - d1) * 0.55f;
        float v = crack + 0.95f * body + 0.75f * rumble + 0.35f * d1;
        buf[i] = v; peak = std::fmax(peak, std::fabs(v));
    }
    for (int i = 0; i < n; i++) {
        float v = std::tanh(buf[i] / peak * 1.3f) / std::tanh(1.3f);  // soft clip, peak -> 1
        float fade = std::fmin(1.f, (n - 1 - i) / (0.02f * kRate));  // click-free end
        data[i] = (short)(v * fade * 29000);                        // ~ -1 dBFS
    }
    Wave wv{(unsigned)n, kRate, 16, 1, data};
    if (const char* dir = getenv("DUMPSFX")) ExportWave(wv, TextFormat("%s/%s.wav", dir, dumpName));
    Sound s = LoadSoundFromWave(wv);
    UnloadWave(wv);
    return s;
}

// ---- Designer-JSON synth ------------------------------------------------------------------
// Renders the web synth-designer format (assets/sfx/*.json: {duration, loop, volume, layers[]})
// with Web Audio semantics: freq -> endFreq glides exponentially over `duration`; osc voices are
// spread +/-detune cents; FM = sine modulator at freq*fmRatio adding fmDepth Hz; biquad filter
// (cutoff Hz; lowpass Q given in dB as Web Audio does, bandpass/highpass Q linear); ADSR with the
// note released at max(attack+decay, duration-release); lfoDepth = vibrato cents (osc) or
// +/-Hz on the filter cutoff (noise); drive = tanh; sampleRate<1 = sample-and-hold crush.
// Noise layers: the freq glide steers the filter (cutoff * f(t)/freq).
// loop:true -> sustain held from t=0 (no attack per loop), glide runs there-and-back so it never
// resets, and the seam is crossfaded, so the buffer repeats seamlessly.
constexpr int kDRate = 44100;
struct Biquad {
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(const std::string& type, float fc, float q) {
        fc = std::fmin(std::fmax(fc, 10.f), kDRate * 0.45f);
        float w = 2 * PI * fc / kDRate, cw = std::cos(w), sw = std::sin(w), al = sw / (2 * q), a0 = 1 + al;
        if (type == "bandpass") { b0 = al; b1 = 0; b2 = -al; }
        else if (type == "highpass") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; }
        else { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    float run(float x) { float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
};
float polyblep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}
// Returns interleaved stereo float samples in [-1,1] (pan honoured); loops are exactly duration*rate frames.
std::vector<float> renderDesigner(const nlohmann::json& j, bool& loop) {
    const float dur = j.value("duration", 0.5f), vol = j.value("volume", 1.f);
    loop = j.value("loop", false);
    const float xf = loop ? std::fmin(0.15f, dur * 0.25f) : 0;  // seam crossfade
    float len = dur;
    if (!loop) for (auto& L : j["layers"]) {
        float a = L.value("attack", 0.f), d = L.value("decay", 0.f), r = L.value("release", 0.f);
        len = std::fmax(len, L.value("start", 0.f) + std::fmax(a + d, dur - r) + r);
    }
    const int n = (int)((len + xf) * kDRate);
    std::vector<float> mix(2 * n, 0.f);  // interleaved stereo
    for (auto& L : j["layers"]) {
        const std::string type = L.value("type", "osc"), wave = L.value("wave", "sine"), ft = L.value("filter", "none");
        const float gain = L.value("gain", 1.f), st = L.value("start", 0.f), f0 = L.value("freq", 440.f), f1 = L.value("endFreq", f0);
        const float A = L.value("attack", 0.f), D = L.value("decay", 0.f), S = L.value("sustain", 1.f), R = L.value("release", 0.f);
        const int nv = std::max(1, L.value("voices", 1)); const float det = L.value("detune", 0.f), vint = L.value("voiceInterval", 0.f);
        const float fmR = L.value("fmRatio", 1.f), fmD = L.value("fmDepth", 0.f), fc = L.value("cutoff", 20000.f), res = L.value("resonance", 0.7f);
        const float lfoR = L.value("lfoRate", 0.f), lfoD = L.value("lfoDepth", 0.f), drive = L.value("drive", 0.f), crush = L.value("sampleRate", 1.f);
        const float pan = std::fmax(-1.f, std::fmin(1.f, L.value("pan", 0.f)));
        const float gL = std::cos((pan + 1) * PI / 4) * std::sqrt(2.f), gR = std::sin((pan + 1) * PI / 4) * std::sqrt(2.f);  // equal-power, unity at centre
        const bool noise = type == "noise", filt = ft == "lowpass" || ft == "bandpass" || ft == "highpass";
        const float q = ft == "lowpass" || ft == "highpass" ? std::pow(10.f, res / 20.f) : std::fmax(0.05f, res);
        const float off = std::fmax(A + D, dur - R);
        Biquad bq; std::vector<float> ph(nv), fmPh(nv, 0.f); for (auto& p : ph) p = frand(0, 1);
        float held = 0; int holdN = 0;
        for (int i = 0; i < n; i++) {
            float t = (float)i / kDRate - (loop ? 0 : st);
            if (t < 0) continue;
            float g;  // glide position 0..1
            if (loop) { float u = std::fmod(t, dur) / dur; g = u < 0.5f ? u * 2 : 2 - u * 2; }
            else g = std::fmin(1.f, t / dur);
            float f = f0 * std::pow(f1 / f0, g);
            float env;
            if (loop) env = S;
            else if (t < A) env = t / std::fmax(A, 1e-6f);
            else if (t < A + D) env = 1 - (1 - S) * (t - A) / std::fmax(D, 1e-6f);
            else if (t < off) env = S;
            else { float e0 = off < A + D ? 1 - (1 - S) * std::fmin(1.f, (off - A) / std::fmax(D, 1e-6f)) : S;
                   env = e0 * std::fmax(0.f, 1 - (t - off) / std::fmax(R, 1e-6f)); }
            if (env <= 0 && t > off) continue;
            float lfo = lfoD != 0 ? std::sin(2 * PI * lfoR * t) : 0;
            float v = 0;
            if (noise) v = frand(-1, 1);
            else for (int k = 0; k < nv; k++) {
                float cents = (nv > 1 ? -det + 2 * det * k / (nv - 1) : 0) + vint * 100 * k + lfo * lfoD;
                float fv = f * std::pow(2.f, cents / 1200);
                fmPh[k] += fv * fmR / kDRate; fmPh[k] -= std::floor(fmPh[k]);
                float inst = fv + fmD * std::sin(2 * PI * fmPh[k]);
                float dt = std::fabs(inst) / kDRate, p = ph[k];
                float s;
                if (wave == "sawtooth") s = 2 * p - 1 - polyblep(p, dt);
                else if (wave == "square") s = (p < 0.5f ? 1.f : -1.f) + polyblep(p, dt) - polyblep(std::fmod(p + 0.5f, 1.f), dt);
                else if (wave == "triangle") s = 1 - 4 * std::fabs(p - 0.5f);
                else s = std::sin(2 * PI * p);
                v += s;
                ph[k] += inst / kDRate; ph[k] -= std::floor(ph[k]);
            }
            if (!noise) v /= std::sqrt((float)nv);
            if (filt) {
                float c = noise ? fc * f / f0 : fc;
                if (noise && lfoD != 0) c += lfo * lfoD;
                if ((i & 15) == 0) bq.set(ft, c, q);
                v = bq.run(v);
            }
            if (drive > 0) v = std::tanh(v * (1 + drive * 10)) / std::tanh(1 + drive * 10);
            if (crush < 1) { if (holdN-- <= 0) { held = v; holdN = (int)(1 / std::fmax(crush, 0.01f)) - 1; } v = held; }
            mix[2 * i] += v * env * gain * gL; mix[2 * i + 1] += v * env * gain * gR;
        }
    }
    const int out = loop ? (int)(dur * kDRate) : n;
    std::vector<float> o(mix.begin(), mix.begin() + 2 * out);
    if (loop) {  // crossfade the rendered tail past the loop point into the head
        const int x = n - out;
        for (int i = 0; i < x; i++) { float a = (float)i / x; for (int c = 0; c < 2; c++) o[2 * i + c] = o[2 * i + c] * a + mix[2 * (out + i) + c] * (1 - a); }
    } else {     // click-free ends
        for (int i = 0; i < out && i < 64; i++) { o[2 * i] *= i / 64.f; o[2 * i + 1] *= i / 64.f; }
        for (int i = 0; i < 441 && i < out; i++) { o[2 * (out - 1 - i)] *= i / 441.f; o[2 * (out - 1 - i) + 1] *= i / 441.f; }
    }
    float peak = 1e-6f; for (float v : o) peak = std::fmax(peak, std::fabs(v));
    const float k = peak > 0.95f ? 0.95f / peak : 1.f;  // only ever pull down (keep designer levels)
    for (float& v : o) v *= k * vol;
    return o;
}
std::vector<float> loadDesigner(const char* path, bool& loop) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "SFX missing %s\n", path); return {}; }
    try { return renderDesigner(nlohmann::json::parse(f), loop); }
    catch (const std::exception& e) { fprintf(stderr, "SFX bad %s: %s\n", path, e.what()); return {}; }
}
Wave toWave(const std::vector<float>& v) {
    auto* d = (short*)MemAlloc(v.size() * sizeof(short));
    for (size_t i = 0; i < v.size(); i++) d[i] = (short)(std::fmax(-1.f, std::fmin(1.f, v[i])) * 32000);
    return Wave{(unsigned)(v.size() / 2), kDRate, 16, 2, d};
}
void dump(const std::vector<float>& v, const char* name, int repeats = 1) {
    const char* dir = getenv("DUMPSFX");
    if (!dir || v.empty()) return;
    std::vector<float> r; for (int k = 0; k < repeats; k++) r.insert(r.end(), v.begin(), v.end());
    Wave w = toWave(r); ExportWave(w, TextFormat("%s/%s.wav", dir, name)); UnloadWave(w);
}
// Background loops (engine, hum), mixed into one stereo stream on the audio thread.
// Main thread only writes the atomic targets; each loop ramps its own gain (click-free fades).
// t235: a loop can also play at a variable rate (linear interpolation; rate 1 = the plain sample walk) and
// a slot can be a one-shot (once: fire restarts it from the top, it stops at its end). One slot per sound,
// so a sound can never stack on itself.
struct Loop { std::vector<float> buf; double pos = 0; float gain = 0, fade = 0.3f, mix = 1, rcur = 1; bool once = false, playing = false;
              std::atomic<float> target{0}, rate{1}, level{0}; std::atomic<int> fire{0}; };
constexpr int kLoops = 14;
Loop gLoops[kLoops];  // 0 = engine, 1 = hum, 2 = flamethrower, 3 = plasma lance, 4 = plasma-on-hull sizzle,
                      // t235: 5 = plasma strain, 6 = overheat burst (once), 7 = cooling hiss (once), 8 = rocket roar,
                      // 9 = wind, 10 = entry plasma, 11 = space drone, 12 = Liftoff ignition (once), 13 = cloud whoosh (once)
const char* const kLoopNames[kLoops] = {"engine", "hum", "flame", "plasma_beam", "plasma_sizzle", "plasma_strain", "plasma_overheat",
                                        "plasma_cooling", "rocket_roar", "wind", "entry_plasma", "space_drone", "liftoff_ignition", "cloud_whoosh"};
std::atomic<float> gEngineDuck{0};
FILE* gTap = nullptr;   // AUDIO_TAP=<file>: raw s16le stereo 44.1 kHz copy of the loop mixer (capture runs)
bool sfxLog() { static const bool on = getenv("SFX_LOG") != nullptr; return on; }
void setTarget(int i, float v) {   // main thread: target + (SFX_LOG) start / stop transitions
    const float old = gLoops[i].target.load(std::memory_order_relaxed);
    if (sfxLog() && (old > 0.01f) != (v > 0.01f)) fprintf(stderr, "SFX %.2f %s %s (target %.2f)\n", GetTime(), kLoopNames[i], v > 0.01f ? "ON" : "OFF", v);
    gLoops[i].target.store(v, std::memory_order_relaxed);
}
void fireOnce(int i) {
    if (sfxLog()) fprintf(stderr, "SFX %.2f %s FIRE\n", GetTime(), kLoopNames[i]);
    gLoops[i].target.store(1.f); gLoops[i].fire.store(1);
}
std::vector<float> loadWavSfx(const char* path) {   // t235: a 44.1 kHz stereo float copy of a WAV, empty if missing
    if (!FileExists(path)) { fprintf(stderr, "SFX missing %s\n", path); return {}; }
    Wave w = LoadWave(path);
    if (!w.data || !w.frameCount) { fprintf(stderr, "SFX bad %s\n", path); return {}; }
    WaveFormat(&w, 44100, 32, 2);
    float* f = LoadWaveSamples(w);
    std::vector<float> v(f, f + (size_t)w.frameCount * 2);
    UnloadWaveSamples(f); UnloadWave(w);
    return v;
}
constexpr float kEngineMix = 0.45f, kHumMix = 0.6f;  // sit under the SFX
constexpr float kFlameMix = 0.72f, kSizzleMix = 0.28f;
}  // namespace

void Audio::engineCallback(void* buf, unsigned frames) {
    auto* o = (short*)buf;
    float tgt[kLoops], rt[kLoops];
    for (int k = 0; k < kLoops; k++) {   // per block: targets, rates, one-shot triggers
        Loop& L = gLoops[k];
        tgt[k] = L.target.load(std::memory_order_relaxed); rt[k] = L.rate.load(std::memory_order_relaxed);
        if (L.once && L.fire.exchange(0)) { L.pos = 0; L.playing = true; L.gain = 0; }
    }
    const float educk = 1.f - gEngineDuck.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < frames; i++) {
        float l = 0, r = 0;
        for (int k = 0; k < kLoops; k++) {
            Loop& L = gLoops[k];
            if (L.buf.empty() || (L.once && !L.playing)) continue;
            const float step = 1.f / (L.fade * kDRate);
            L.gain += L.gain < tgt[k] ? std::fmin(step, tgt[k] - L.gain) : -std::fmin(step, L.gain - tgt[k]);
            if (L.gain <= 0.f && tgt[k] <= 0.f) { if (L.once) L.playing = false; continue; }   // silent: skip (a loop keeps its place)
            const size_t n = L.buf.size() / 2, i0 = (size_t)L.pos;
            const float fr = (float)(L.pos - (double)i0);
            const size_t i1 = i0 + 1 < n ? i0 + 1 : (L.once ? i0 : 0);
            const float g = L.gain * L.mix * (k == 0 ? educk : 1.f);
            l += (L.buf[2 * i0] + (L.buf[2 * i1] - L.buf[2 * i0]) * fr) * g;
            r += (L.buf[2 * i0 + 1] + (L.buf[2 * i1 + 1] - L.buf[2 * i0 + 1]) * fr) * g;
            L.rcur += (rt[k] - L.rcur) * 0.0005f;   // ~50 ms glide: pitch moves never step
            L.pos += L.rcur;
            if (L.pos >= (double)n) { if (L.once) { L.playing = false; L.gain = 0.f; } else L.pos -= (double)n; }
        }
        o[2 * i] = (short)(std::fmax(-1.f, std::fmin(1.f, l)) * 32000);
        o[2 * i + 1] = (short)(std::fmax(-1.f, std::fmin(1.f, r)) * 32000);
    }
    for (int k = 0; k < kLoops; k++) gLoops[k].level.store(gLoops[k].once && !gLoops[k].playing ? 0.f : gLoops[k].gain, std::memory_order_relaxed);
    if (gTap) fwrite(o, 4, frames, gTap);
}

void Audio::engine(bool on) {
    gLoops[0].target = on ? 1.f : 0.f;
    engineOn_ = on;   // (called before the weapons / flight updates of the frame: they read it)
    if (!on) {   // t235: no engine (pause, death, level end, menus): the lance and the flight layers go quiet too
        plasma(false);
        for (int k = 8; k < kLoops; k++) if (gLoops[k].target.load(std::memory_order_relaxed) > 0.f) setTarget(k, 0.f);
        gEngineDuck = 0.f; duckTgt_ = 0.f;
    }
}
void Audio::hum(bool on) { gLoops[1].target = on ? (musLoaded_ && !musWant_.empty() ? 0.5f : 1.f) : 0.f; }  // hum ducks under music
void Audio::flamethrower(bool on) { gLoops[2].target = on ? 1.f : 0.f; }
void Audio::plasma(bool on) {   // (t235: the beam itself is driven by plasmaLance(); off = every lance layer, incl. the one-shots)
    setTarget(3, on ? 1.f : 0.f);
    if (!on) for (int k : {5, 6, 7}) if (gLoops[k].target.load(std::memory_order_relaxed) > 0.f) setTarget(k, 0.f);
}
void Audio::sizzle(bool on) { setTarget(4, on && engineOn_ ? 1.f : 0.f); }
void Audio::plasmaLance(bool firing, float heat, float lock) {
    static bool lockWas = false;
    if (!engineOn_) { plasma(false); lockWas = lock > 0.f; return; }   // the ship is down (respawn): no lance sound
    heat = std::fmax(0.f, std::fmin(1.f, heat));
    setTarget(3, firing ? 1.f : 0.f);
    gLoops[3].rate = 1.f + 0.22f * heat;                                 // the hum climbs ~3.5 semitones toward overheat
    const float k = std::fmax(0.f, std::fmin(1.f, (heat - 0.35f) / 0.65f));
    setTarget(5, firing ? k * k * (3.f - 2.f * k) : 0.f);               // the strain whine joins from ~35 % heat
    gLoops[5].rate = 1.f + 0.12f * heat;
    const bool locked = lock > 0.f;
    if (locked && !lockWas) { fireOnce(6); fireOnce(7); }               // lockout: alarm + vent burst, then the cooling hiss
    if (!locked && gLoops[7].target.load(std::memory_order_relaxed) > 0.f) setTarget(7, 0.f);   // (never outlasts the lockout)
    lockWas = locked;
}
void Audio::flight(const Flight& fIn) {
    const Flight f = engineOn_ ? fIn : Flight{};   // the ship is down (respawn): the flight layers fade out with the engine
    auto c01 = [](float v) { return std::fmax(0.f, std::fmin(1.f, v)); };
    setTarget(8, c01(f.roar)); gLoops[8].rate = f.roarRate;
    setTarget(9, c01(f.wind)); gLoops[9].rate = f.windRate;
    setTarget(10, c01(f.entry)); gLoops[10].rate = f.entryRate;
    setTarget(11, c01(f.drone));
    if (f.ignition) fireOnce(12);
    if (f.whoosh) fireOnce(13);
    gEngineDuck = std::fmin(0.8f, c01(f.engineDuck));
    duckTgt_ = c01(f.musicDuck);
}

constexpr float kMusicMix = 0.55f;  // under the SFX, over the hum/engine
void Audio::music(const std::string& path) {
    if (!ok_) return;
    musWant_ = path;
    if (path.empty() || path == musPath_) return;
    if (musLoaded_ && musGain_ > 0.01f) return;  // let the old song fade out first (updateMusic swaps)
    if (musLoaded_) { StopMusicStream(mus_); UnloadMusicStream(mus_); musLoaded_ = false; }
    const std::string full = path[0] == '/' ? path : "assets/" + path;
    mus_ = LoadMusicStream(full.c_str());
    musPath_ = path;
    if (mus_.stream.buffer == nullptr || mus_.frameCount == 0) { fprintf(stderr, "MUSIC missing %s\n", full.c_str()); return; }
    mus_.looping = true;
    musLoaded_ = true; musGain_ = 0;
    SetMusicVolume(mus_, 0);
    PlayMusicStream(mus_);
    fprintf(stderr, "MUSIC %s %.1fs (looping)\n", full.c_str(), GetMusicTimeLength(mus_));
}
void Audio::updateMusic(float dt) {
    if (!ok_) return;
    if (sfxLog()) {   // t235: 2 Hz levels of the lance / flight slots and the music gain
        static double next = 0;
        if (GetTime() >= next) {
            next = GetTime() + 0.5;
            std::string m;
            for (int k = 3; k < kLoops; k++) { const float g = gLoops[k].level.load(std::memory_order_relaxed); if (g > 0.005f) m += TextFormat(" %s=%.2f", kLoopNames[k], g); }
            fprintf(stderr, "SFXLV %.1f music=%.2f duck=%.2f eng=%.2f%s\n", GetTime(), musLoaded_ ? musGain_ * (1.f - 0.35f * duck_) : 0.f, duck_,
                    gLoops[0].level.load(std::memory_order_relaxed), m.c_str());
        }
    }
    if (!musLoaded_) { if (!musWant_.empty() && musWant_ != musPath_) music(musWant_); return; }
    const bool want = !musWant_.empty() && musWant_ == musPath_;
    musGain_ = want ? std::min(1.f, musGain_ + dt / 1.0f) : std::max(0.f, musGain_ - dt / 1.5f);
    duck_ += (duckTgt_ - duck_) * std::fmin(1.f, dt * 3.f);   // t235: under the flight roar, never below 65 %
    SetMusicVolume(mus_, musGain_ * kMusicMix * cfg::dev.musicLevel * (1.f - 0.35f * duck_));
    UpdateMusicStream(mus_);
    if (!want && musGain_ <= 0) {  // faded out: free it (next level's song loads on demand)
        StopMusicStream(mus_); UnloadMusicStream(mus_); musLoaded_ = false; musPath_.clear();
        if (!musWant_.empty()) music(musWant_);
    }
}

void Audio::missileLaunch() {
    // auto-launch every kMissileInterval (0.6 s) while ammo lasts; the sound is 1.7 s, so a 0.5 s retrigger
    // cooldown keeps it to <= 3 overlapping (4-voice ring, oldest reused)
    const double now = GetTime();
    if (now - lastMissile_ < 0.5) return;
    lastMissile_ = now;
    play(Sfx::Missile, 0.55f, 0.04f);
}

void Audio::hitLaser() {
    // Was a 1/12 s gate on a 0.5 s sound with 5 voices: at the 10 volleys/s fire rate most repeat
    // hits were dropped or cut off. Now up to 24/s with gentle ducking (never below 55%), 8 voices,
    // and a wider pitch spread so consecutive hits read as separate impacts.
    float vol = 0.8f;
    if (!hitGate_.allow(GetTime(), 0.8f, vol)) return;
    lastHit_ = GetTime();
    play(Sfx::LaserHit, vol, 0.09f);
}

void Audio::shieldDeflect() {
    // Deliberately no rate gate: each projectile contact owns its buzz/surge, and the alias ring
    // keeps a burst from cutting off the immediately preceding surface wave.
    play(Sfx::ShieldDeflect, 0.48f, 0.025f);
}
namespace {
}  // namespace

void Audio::init() {
    if (getenv("NOAUDIO")) return;
    InitAudioDevice();
    ok_ = IsAudioDeviceReady();
    backend_ = ok_ ? "miniaudio" : "unavailable";
    if (!ok_) return;
    base_[(int)Sfx::Laser] = LoadSound("assets/sfx/laser_fire.wav");
    laserLoaded_ = IsSoundValid(base_[(int)Sfx::Laser]);
    if (!laserLoaded_) fprintf(stderr, "AUDIO required asset invalid: assets/sfx/laser_fire.wav\n");
    base_[(int)Sfx::Missile] = synth(0.35f, 200, 900, 0.6f, 3, false);
    base_[(int)Sfx::Hit] = synth(0.05f, 600, 300, 0.5f, 6, true);
    base_[(int)Sfx::Explosion] = boom(0.9f, 150, 48, 0.16f, 0.22f, 0.010f, "explosion");
    base_[(int)Sfx::BigExplosion] = boom(2.0f, 120, 36, 0.40f, 0.55f, 0.016f, "big_explosion");
    // t251: the Warden's death: a long, deep boom with a slow rumble tail; and its readable charge-up whine
    base_[(int)Sfx::BossBoom] = boom(5.5f, 85, 22, 1.1f, 2.4f, 0.03f, "boss_boom");
    {
        const int n = (int)(kRate * 0.9f);
        auto* data = (short*)MemAlloc(n * sizeof(short));
        float ph = 0, ph2 = 0;
        for (int i = 0; i < n; i++) {
            const float k = (float)i / n, f = 160.f * std::pow(5.5f, k);   // rising sweep 160 Hz -> 880 Hz
            ph += f / kRate; ph2 += (f * 1.503f) / kRate;
            const float trem = 0.75f + 0.25f * std::sin(2 * PI * (6.f + 20.f * k) * i / kRate);
            const float env = std::fmin(1.f, k / 0.15f) * std::fmin(1.f, (1.f - k) / 0.06f) * (0.35f + 0.65f * k);
            const float v = (std::sin(2 * PI * ph) + 0.35f * std::sin(2 * PI * ph2)) * trem * env;
            data[i] = (short)(std::tanh(v * 1.2f) * 12000);
        }
        Wave w{(unsigned)n, kRate, 16, 1, data};
        if (const char* dir = getenv("DUMPSFX")) ExportWave(w, TextFormat("%s/boss_charge.wav", dir));
        base_[(int)Sfx::BossCharge] = LoadSoundFromWave(w);
        UnloadWave(w);
    }
    base_[(int)Sfx::PlayerHit] = synth(0.3f, 400, 60, 0.4f, 4, true);
    bool lp = false;
    auto lh = loadDesigner("assets/sfx/laser_damage.json", lp);
    if (!lh.empty()) { dump(lh, "laser_damage"); Wave w = toWave(lh); base_[(int)Sfx::LaserHit] = LoadSoundFromWave(w); UnloadWave(w); }
    else base_[(int)Sfx::LaserHit] = synth(0.05f, 600, 300, 0.5f, 6, true);
    auto ml = loadDesigner("assets/sfx/missile_launch.json", lp);  // player missile launch (replaces the synth whoosh)
    if (!ml.empty()) { dump(ml, "missile_launch"); UnloadSound(base_[(int)Sfx::Missile]); Wave w = toWave(ml); base_[(int)Sfx::Missile] = LoadSoundFromWave(w); UnloadWave(w); }
    auto pc = loadDesigner("assets/sfx/pickup_collect.json", lp);
    if (!pc.empty()) { dump(pc, "pickup_collect"); Wave w = toWave(pc); base_[(int)Sfx::Pickup] = LoadSoundFromWave(w); UnloadWave(w); }
    if (!IsSoundValid(base_[(int)Sfx::Pickup])) fprintf(stderr, "AUDIO required asset invalid: assets/sfx/pickup_collect.json\n");
    auto sd = loadDesigner("assets/sfx/shield_deflect.json", lp);
    if (!sd.empty()) { dump(sd, "shield_deflect"); Wave w = toWave(sd); base_[(int)Sfx::ShieldDeflect] = LoadSoundFromWave(w); UnloadWave(w); }
    if (!IsSoundValid(base_[(int)Sfx::ShieldDeflect])) fprintf(stderr, "AUDIO required asset invalid: assets/sfx/shield_deflect.json\n");
    auto ex = loadDesigner("assets/sfx/enemy_explosion.json", lp);
    if (!ex.empty()) { dump(ex, "enemy_explosion"); Wave w = toWave(ex); base_[(int)Sfx::EnemyExplosion] = LoadSoundFromWave(w); UnloadWave(w); }
    else base_[(int)Sfx::EnemyExplosion] = boom(0.9f, 150, 48, 0.16f, 0.22f, 0.010f, "explosion");
    const char* designerSounds[] = {
        "rapid_pickup", "weapon_pickup", "shield_pickup", "repair_pickup", "weapon_overheat", "weapon_cooldown"
    };
    const Sfx designerIds[] = {
        Sfx::RapidPickup, Sfx::WeaponPickup, Sfx::ShieldPickup, Sfx::RepairPickup, Sfx::WeaponOverheat, Sfx::WeaponCooldown
    };
    for (int i = 0; i < 6; i++) {
        auto data = loadDesigner(TextFormat("assets/sfx/%s.json", designerSounds[i]), lp);
        if (!data.empty()) { dump(data, designerSounds[i]); Wave w = toWave(data); base_[(int)designerIds[i]] = LoadSoundFromWave(w); UnloadWave(w); }
    }
    gLoops[0].buf = loadDesigner("assets/sfx/engine_noise.json", lp); gLoops[0].fade = 0.3f; gLoops[0].mix = kEngineMix;
    gLoops[1].buf = loadDesigner("assets/sfx/background_hum.json", lp); gLoops[1].fade = 0.5f; gLoops[1].mix = kHumMix;
    gLoops[2].buf = loadDesigner("assets/sfx/flamethrower_loop.json", lp); gLoops[2].fade = 0.08f; gLoops[2].mix = kFlameMix;
    dump(gLoops[0].buf, "engine_noise", 3);    // 3 loops (9 s) to audit the seams
    dump(gLoops[1].buf, "background_hum", 3);  // 3 loops (12 s)
    dump(gLoops[2].buf, "flamethrower_loop", 3);
    {   // plasma lance hum (procedural, seamless 2 s loop): detuned saws + a crackle, 23 Hz throb
        const int n = kDRate * 2; std::vector<float>& b = gLoops[3].buf; b.resize(2 * n);
        uint32_t h = 22695477u; float lp1 = 0, lp2 = 0;
        for (int i = 0; i < n; i++) {
            const float t = i / (float)kDRate;
            auto saw = [&](float f) { float x = t * f; return 2.f * (x - std::floor(x)) - 1.f; };
            h = h * 1664525u + 1013904223u; const float nz = ((h >> 8) & 0xFFFF) / 32768.f - 1.f;
            lp1 += 0.08f * (nz - lp1); lp2 += 0.02f * (lp1 - lp2);
            const float throb = 0.75f + 0.25f * std::sin(2 * PI * 23.f * t);
            const float base = 0.30f * saw(110.f) + 0.22f * saw(110.5f) + 0.12f * saw(220.f) + 0.06f * std::sin(2 * PI * 880.f * t);
            const float v = (base * throb + 0.9f * lp2) * 0.42f;
            b[2 * i] = v; b[2 * i + 1] = v * 0.96f + 0.04f * lp1;
        }
        gLoops[3].fade = 0.06f; gLoops[3].mix = 0.55f;
        dump(gLoops[3].buf, "plasma_loop", 2);
    }
    {   // plasma-on-hull sizzle: filtered noise and small crackles with a short loop-edge fade
        const int n = kDRate; std::vector<float>& b = gLoops[4].buf; b.resize(2 * n);
        uint32_t h = 3499211612u; float lp = 0.f, crackle = 0.f;
        const int fadeN = (int)(0.035f * kDRate);
        for (int i = 0; i < n; i++) {
            h = h * 1664525u + 1013904223u;
            const float noise = ((h >> 8) & 0xFFFF) / 32768.f - 1.f;
            lp += 0.18f * (noise - lp);
            crackle *= 0.996f;
            if ((h & 0x1FFFu) < 18u) crackle += 0.55f;
            const float edge = std::min(1.f, std::min(i / (float)fadeN, (n - 1 - i) / (float)fadeN));
            const float hiss = (noise - lp) * 0.24f + lp * 0.08f + crackle * 0.22f;
            const float v = hiss * std::max(0.f, edge) * 0.22f;
            b[2 * i] = v; b[2 * i + 1] = v * 0.92f;
        }
        gLoops[4].fade = 0.045f; gLoops[4].mix = kSizzleMix;
    }
    {   // t235: procedural WAVs (tools/gen_plasma_flight_sfx.py); the lance hum / sizzle above stay as the fallback
        struct W { int slot; const char* file; float fade, mix; bool once; };
        const W ws[] = {{3, "plasma_beam_loop", 0.07f, 0.90f, false}, {4, "plasma_sizzle_loop", 0.045f, 0.70f, false},
                        {5, "plasma_strain_loop", 0.25f, 0.80f, false}, {6, "plasma_overheat", 0.005f, 0.85f, true},
                        {7, "plasma_cooling", 0.12f, 0.80f, true}, {8, "rocket_roar_loop", 0.25f, 0.90f, false},
                        {9, "wind_loop", 0.40f, 0.80f, false}, {10, "entry_plasma_loop", 0.30f, 0.85f, false},
                        {11, "space_drone_loop", 1.00f, 0.70f, false}, {12, "liftoff_ignition", 0.005f, 0.90f, true},
                        {13, "cloud_whoosh", 0.005f, 0.90f, true}};
        for (const W& w : ws) {
            std::vector<float> v = loadWavSfx(TextFormat("assets/sfx/%s.wav", w.file));
            if (v.empty()) continue;
            Loop& L = gLoops[w.slot]; L.buf.swap(v); L.fade = w.fade; L.mix = w.mix; L.once = w.once;
        }
    }
    if (const char* tap = getenv("AUDIO_TAP")) { gTap = fopen(tap, "wb"); if (gTap) setvbuf(gTap, nullptr, _IONBF, 0); }
    bool anyLoop = false; for (const Loop& L : gLoops) anyLoop |= !L.buf.empty();
    if (anyLoop) {
        SetAudioStreamBufferSizeDefault(1024);
        engStream_ = LoadAudioStream(kDRate, 16, 2);
        SetAudioStreamCallback(engStream_, engineCallback);
        PlayAudioStream(engStream_);
        engLoaded_ = true;
    }
    for (int i = 0; i < (int)Sfx::Count; i++) {
        if (i == (int)Sfx::Laser || !IsSoundValid(base_[i])) continue;
        for (auto& v : voices_[i]) v = LoadSoundAlias(base_[i]);
    }
    if (laserLoaded_)
        for (auto& v : laserVoices_) v = LoadSoundAlias(base_[(int)Sfx::Laser]);
}

void Audio::shutdown() {
    if (!ok_) return;
    if (laserLoaded_) {
        for (auto& v : laserVoices_) UnloadSoundAlias(v);
    }
    for (int i = 0; i < (int)Sfx::Count; i++) {
        if (i != (int)Sfx::Laser && IsSoundValid(base_[i]))
            for (auto& v : voices_[i]) UnloadSoundAlias(v);
        if (IsSoundValid(base_[i])) UnloadSound(base_[i]);
    }
    if (musLoaded_) { StopMusicStream(mus_); UnloadMusicStream(mus_); musLoaded_ = false; }
    if (engLoaded_) { StopAudioStream(engStream_); UnloadAudioStream(engStream_); engLoaded_ = false; }
    if (gTap) { fclose(gTap); gTap = nullptr; }
    CloseAudioDevice();
    ok_ = false;
    laserLoaded_ = false;
}

void Audio::play(Sfx s, float volume, float pitchJitter) {
    if (!ok_) return;
    if (s == Sfx::Laser) { laserFire(volume); return; }
    int i = (int)s;
    if (!IsSoundValid(base_[i])) return;
    Sound& v = voices_[i][next_[i]];
    next_[i] = (next_[i] + 1) % kVoices;
    const bool boomy = s == Sfx::Explosion || s == Sfx::BigExplosion || s == Sfx::EnemyExplosion || s == Sfx::BossBoom;  // only ever pitch booms down (up = tinny)
    SetSoundPitch(v, boomy ? 1 - frand(0, pitchJitter) : 1 + frand(-pitchJitter, pitchJitter));
    SetSoundVolume(v, volume);
    PlaySound(v);
}

std::uint64_t Audio::laserFire(float volume) {
    if (!ok_ || !laserLoaded_) return 0;
    const int index = laserNext_;
    laserNext_ = (laserNext_ + 1) % kLaserVoices;
    // Generation 0 is reserved for "no sound". At 200 bolt sounds/second, the 58-bit
    // generation space lasts more than 45 million years before wrapping.
    laserGeneration_++;
    if (laserGeneration_ == 0 || laserGeneration_ >= (std::uint64_t{1} << 58)) laserGeneration_ = 1;
    laserVoiceGenerations_[index] = laserGeneration_;
    Sound& voice = laserVoices_[index];
    SetSoundPitch(voice, 1.f);
    SetSoundVolume(voice, volume);
    PlaySound(voice);
    return (laserGeneration_ << 6) | (std::uint64_t)index;
}

void Audio::stopLaser(std::uint64_t token) {
    if (!ok_ || !laserLoaded_ || token == 0) return;
    const int index = (int)(token & 0x3fu);
    const std::uint64_t generation = token >> 6;
    if (laserVoiceGenerations_[index] != generation) return;
    laserVoiceGenerations_[index] = 0;
    StopSound(laserVoices_[index]);
}
