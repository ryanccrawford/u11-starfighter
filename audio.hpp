#pragma once
#include "hit_feedback.hpp"
#include "raylib.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Game SFX and music. Falls back silently if no audio device.
enum class Sfx {
    Laser, Missile, Hit, Explosion, BigExplosion, Pickup, PlayerHit, LaserHit, EnemyExplosion, ShieldDeflect,
    RapidPickup, WeaponPickup, ShieldPickup, RepairPickup, WeaponOverheat, WeaponCooldown,
    BossCharge, BossBoom,   // t251: Warden attack charge-up whine, Warden death mega-boom (procedural)
    Count
};

class Audio {
public:
    void init();
    void shutdown();
    void play(Sfx s, float volume = 1.f, float pitchJitter = 0.08f);
    std::uint64_t laserFire(float volume = 0.8f);  // starts one bolt's flight sound; 0 means unavailable
    void stopLaser(std::uint64_t token);           // stops only the bolt instance represented by token
    bool ok() const { return ok_; }
    const char* backend() const { return backend_; }
    void missileLaunch();        // player missile salvo (assets/sfx/missile_launch.json), cooldown-capped
    void hitLaser();             // player bolt hit an enemy: rate-limited designer sound (assets/sfx/laser_damage.json)
    void shieldDeflect();        // one independently-voiced shield surface surge per intercepted contact
    void flamethrower(bool on);  // continuous shader-beam roar; fades without clicks
    void plasma(bool on);        // plasma lance hum (procedural loop); fades without clicks
    void sizzle(bool on);        // plasma-on-hull sizzle (procedural loop); fades without clicks
    void engine(bool on);        // gameplay engine loop (assets/sfx/engine_noise.json); fades ~0.3 s in/out
    void music(const std::string& path);  // assets-relative MP3/OGG, looped; "" = fade out (1.5 s)
    void updateMusic(float dt);           // every frame: feeds the stream, runs the fades
    void hum(bool on);           // play-session background hum (assets/sfx/background_hum.json); fades ~0.5 s
    // t235: plasma lance layers and the Liftoff / Planetfall flight sound (procedural WAVs in assets/sfx,
    // tools/gen_plasma_flight_sfx.py). All of them are single slots in the loop mixer, so nothing stacks;
    // plasma(false) silences the lance layers, engine(false) (pause, death, level end, menus) all of them.
    void plasmaLance(bool firing, float heat, float lock);  // beam loop (pitch + strain rise with heat), overheat burst + cooling hiss
    struct Flight { float roar = 0, roarRate = 1, wind = 0, windRate = 1, entry = 0, entryRate = 1, drone = 0, engineDuck = 0, musicDuck = 0;
                    bool ignition = false, whoosh = false; };
    void flight(const Flight& f);  // every frame of an approach level (Game::updateFlightAudio)

private:
    static constexpr int kVoices = 8;  // aliases per sound so effects can overlap, including dense shield impacts
    static constexpr int kLaserVoices = 64;  // 0.52 s WAV, up to 5 bolts every 0.05 s = 55 concurrent
    bool ok_ = false;
    const char* backend_ = "off";
    std::array<Sound, (int)Sfx::Count> base_{};
    std::array<std::array<Sound, kVoices>, (int)Sfx::Count> voices_{};
    std::array<int, (int)Sfx::Count> next_{};
    std::array<Sound, kLaserVoices> laserVoices_{};
    std::array<std::uint64_t, kLaserVoices> laserVoiceGenerations_{};
    std::uint64_t laserGeneration_ = 0;
    int laserNext_ = 0;
    bool laserLoaded_ = false;
    double lastHit_ = -1, lastMissile_ = -1;
    HitSoundGate hitGate_;
    Music mus_{}; bool musLoaded_ = false; std::string musPath_, musWant_; float musGain_ = 0;
    float duck_ = 0, duckTgt_ = 0;  // t235: music ducks (never below 65 %) under the flight roar
    bool engineOn_ = false;         // t235: engine(on) of this frame; off (death, pause, menus) mutes the lance / flight layers
    AudioStream engStream_{};
    bool engLoaded_ = false;
    static void engineCallback(void* buf, unsigned frames);
};
