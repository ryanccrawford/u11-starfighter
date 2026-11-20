#include <cstdlib>
#include "data.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

using nlohmann::json;
namespace fs = std::filesystem;

namespace {
Color color(const json& j, Color def) {
    if (!j.is_array() || j.size() < 3) return def;
    return {(unsigned char)j[0].get<int>(), (unsigned char)j[1].get<int>(), (unsigned char)j[2].get<int>(),
            (unsigned char)(j.size() > 3 ? j[3].get<int>() : 255)};
}

template <typename E>
E pick(const std::string& s, std::initializer_list<std::pair<const char*, E>> opts, E def) {
    for (auto& [name, v] : opts) if (s == name) return v;
    return def;
}

std::vector<fs::path> jsonFiles(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".json") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

Pattern patternType(const std::string& s) {
    return pick<Pattern>(s, {{"straight", Pattern::Straight}, {"sine", Pattern::Sine}, {"dive", Pattern::Dive},
        {"circle", Pattern::Circle}, {"path", Pattern::Path}, {"invaders", Pattern::Invaders}, {"boss", Pattern::Boss}}, Pattern::Straight);
}

EnemyWeapon weaponType(const json& w) {
    return pick<EnemyWeapon>(w.value("type", "none"), {{"none", EnemyWeapon::None}, {"aimed", EnemyWeapon::Aimed},
        {"spread", EnemyWeapon::Spread}, {"laser", EnemyWeapon::Laser}, {"missile", EnemyWeapon::Missile},
        {"spiral", EnemyWeapon::Spiral}, {"sweep", EnemyWeapon::Sweep}, {"needle", EnemyWeapon::Needle}, {"rotary", EnemyWeapon::Rotary}}, EnemyWeapon::None);
}

EnemyDef parseEnemy(const json& j, const std::string& id) {
    EnemyDef d;
    d.id = j.value("id", id);
    d.presentation.sprite = j.value("sprite", "sprites/blue_racer_pi3_sprites.json");
    d.presentation.model = j.value("model", std::string());
    if (j.contains("animations")) d.presentation.animationClips = j.at("animations").get<std::map<std::string, std::string>>();
    d.scale = j.value("scale", 1.0f); d.speed = j.value("speed", 130.0f); d.modelScale = j.value("modelScale", 1.0f);
    d.tint = color(j.value("tint", json()), WHITE);
    if (j.contains("engineTint")) d.engineTint = color(j["engineTint"], WHITE);
    d.hp = j.value("hp", 1); d.score = j.value("score", 100); d.boss = j.value("boss", false);
    if (j.contains("movement")) {
        const json& m = j["movement"];
        d.pattern = patternType(m.value("pattern", "straight"));
        d.amp = m.value("amplitude", 60.0f); d.freq = m.value("frequency", 2.0f);
        if (m.contains("points")) for (auto& p : m["points"]) d.path.push_back({p[0], p[1]});
    }
    if (j.contains("weapon")) {
        d.weapon = weaponType(j["weapon"]);
        d.fireInterval = j["weapon"].value("interval", 2.0f); d.shots = j["weapon"].value("shots", 1);
        d.shotSpeed = j["weapon"].value("speed", 1.0f); d.shotSize = j["weapon"].value("size", 1.0f);
        d.shotDamage = j["weapon"].value("damage", 0); d.telegraph = j["weapon"].value("telegraph", 0.0f);
        d.spinUp = j["weapon"].value("spinUp", 0.1f); d.burstGap = j["weapon"].value("burstGap", 0.09f);
    }
    d.name = j.value("name", d.id);
    for (auto& ph : j.value("phases", json::array())) {
        BossPhase p;
        p.hp = ph.value("hp", 1.0f);
        p.pattern = patternType(ph.value("pattern", "boss"));
        if (ph.contains("weapon")) {
            p.weapon = weaponType(ph["weapon"]);
            p.interval = ph["weapon"].value("interval", 1.0f); p.shots = ph["weapon"].value("shots", 5);
        }
        if (ph.contains("minions")) {
            p.minion = ph["minions"].value("enemy", ""); p.minionEvery = ph["minions"].value("every", 5.0f);
            p.minionCount = ph["minions"].value("count", 3);
        }
        d.phases.push_back(p);
    }
    if (j.contains("drops")) {
        d.dropChance = j["drops"].value("chance", 0.15f);
        json tbl = j["drops"].value("table", json::object());
        for (auto& [k, v] : tbl.items()) d.drops.push_back({k, v.get<float>()});
    }
    return d;
}

PlayerDef parsePlayer(const json& j, const std::string& id) {
    PlayerDef p;
    p.id = j.value("id", id);
    p.presentation.sprite = j.value("sprite", p.presentation.sprite);
    p.controller = pick<ControllerType>(j.value("controller", "flight"),
        {{"flight", ControllerType::Flight}, {"ground_side", ControllerType::GroundSide},
         {"ground_topdown", ControllerType::GroundTopDown}}, ControllerType::Flight);
    p.size = j.value("size", p.size); p.speed = j.value("speed", p.speed); p.hp = j.value("hp", p.hp);
    p.gravity = j.value("gravity", p.gravity); p.jump = j.value("jump", p.jump);
    p.walk = j.value("walk", p.walk); p.run = j.value("run", p.run);
    return p;
}

LevelDef parseLevel(const json& j, const std::string& id) {
    LevelDef L;
    L.id = id; L.name = j.value("name", id); L.music = j.value("music", "");
    L.boss = j.value("boss", "");
    L.duration = j.value("duration", L.boss.empty() ? 180.0f : 0.0f);  // non-boss levels: 3 minutes
    L.player = j.value("player", "red_fighter");
    L.terrain = j.value("terrain", "");
    L.approach = j.value("approach", false);
    L.approachStart = j.value("approach_start", 0.f);
    L.approachEnd = j.value("approach_end", 0.f);
    L.groundGuns = j.value("ground_guns", false);
    L.liftoff = j.value("liftoff", false);
    L.intro = j.value("intro", ""); L.outro = j.value("outro", "");
    L.mode = j.value("mode", "side_scroll") == "top_down" ? Mode::TopDown : Mode::SideScroll;
    for (const auto& point : j.value("spawn_points", json::array())) {
        LevelSpawnPoint spawn;
        spawn.id = point.at("id").get<std::string>();
        const auto& position = point.at("position");
        spawn.position = {position.at("x").get<float>(), position.at("y").get<float>(), position.at("z").get<float>()};
        L.spawnPoints.push_back(spawn);
    }
    L.clear = color(j.value("clear_color", json()), L.clear);
    if (j.contains("difficulty")) {
        L.hpMul = j["difficulty"].value("hp", 1.0f);
        L.fireMul = j["difficulty"].value("fire_rate", 1.0f);
        L.speedMul = j["difficulty"].value("speed", 1.0f);
    }
    for (auto& b : j.value("background", json::array())) {
        BgLayer l;
        l.type = pick<LayerType>(b.value("type", "stars"),
            {{"texture", LayerType::Texture}, {"stars", LayerType::Stars}, {"nebula", LayerType::Nebula},
             {"planet", LayerType::Planet}, {"asteroids", LayerType::Asteroids},
             {"planet3d", LayerType::Planet3D}, {"terrain3d", LayerType::Terrain3D}}, LayerType::Stars);
        l.texture = b.value("texture", ""); l.speed = b.value("speed", 50.0f);
        l.color = color(b.value("color", json()), WHITE); l.density = b.value("density", 100);
        l.y = b.value("y", 0.5f); l.size = b.value("size", 1.0f);
        l.x = b.value("x", 0.7f); l.spin = b.value("spin", 0.05f);
        l.cloudSpin = b.value("cloud_spin", 0.0f);
        l.model = b.value("model", ""); l.cloudModel = b.value("cloud_model", "");
        L.layers.push_back(l);
    }
    for (auto& w : j.value("waves", json::array())) {
        SpawnWave v;
        v.time = w.value("time", 0.0f); v.enemy = w.value("enemy", "grunt"); v.count = w.value("count", 1);
        v.formation = w.value("formation", "line"); v.y = w.value("y", 0.5f); v.spacing = w.value("spacing", 70.0f);
        v.amp = w.value("amplitude", -1.0f); v.freq = w.value("frequency", -1.0f);
        for (auto& p : w.value("path", json::array())) v.path.push_back({p[0], p[1]});
        L.waves.push_back(v);
    }
    std::sort(L.waves.begin(), L.waves.end(), [](auto& a, auto& b) { return a.time < b.time; });
    return L;
}
}  // namespace

void Database::load(const std::string& root) {
    enemies_.clear(); levels_.clear(); players_.clear();
    for (auto& p : jsonFiles(fs::path(root) / "players")) {
        try {
            std::ifstream f(p);
            PlayerDef d = parsePlayer(json::parse(f), p.stem().string());
            players_[d.id] = d;
        } catch (const std::exception& e) { fprintf(stderr, "DATA bad player %s: %s\n", p.c_str(), e.what()); }
    }
    for (auto& p : jsonFiles(fs::path(root) / "enemies")) {
        try {
            std::ifstream f(p);
            EnemyDef d = parseEnemy(json::parse(f), p.stem().string());
            enemies_[d.id] = d;
        } catch (const std::exception& e) { fprintf(stderr, "DATA bad enemy %s: %s\n", p.c_str(), e.what()); }
    }
    for (auto& p : jsonFiles(fs::path(root) / "levels")) {
        try {
            std::ifstream f(p);
            levels_.push_back(parseLevel(json::parse(f), p.stem().string()));
        } catch (const std::exception& e) { fprintf(stderr, "DATA bad level %s: %s\n", p.c_str(), e.what()); }
    }
    campaign_.clear();
    try {
        const char* campFile = "campaign.json";  // (former campaign_3d.json)
        std::ifstream f(fs::path(root) / campFile);
        if (f) for (auto& ch : json::parse(f).value("chapters", json::array()))
            for (auto& st : ch.value("steps", json::array()))
                campaign_.push_back({ch.value("title", ""), st.value("level", ""), st.value("cutscene", "")});
    } catch (const std::exception& e) { fprintf(stderr, "DATA bad campaign: %s\n", e.what()); }
    if (levels_.empty()) {  // never leave the game without something to play
        LevelDef L; L.id = "fallback"; L.name = "Fallback"; L.layers.push_back({});
        for (int i = 0; i < 6; i++) L.waves.push_back({i * 6.f, "grunt", 5, "line", 0.5f, 70, -1, -1});
        levels_.push_back(L);
    }
    if (campaign_.empty())  // default campaign: every level in file order
        for (auto& L : levels_) campaign_.push_back({"", L.id, ""});
    fallback_.id = "fallback";
    fprintf(stderr, "DATA %zu enemies, %zu levels\n", enemies_.size(), levels_.size());
}

const EnemyDef& Database::enemy(const std::string& id) const {
    auto it = enemies_.find(id);
    if (it != enemies_.end()) return it->second;
    fprintf(stderr, "DATA unknown enemy '%s' (fallback)\n", id.c_str());
    return fallback_;
}

const PlayerDef& Database::player(const std::string& id) const {
    auto it = players_.find(id);
    return it != players_.end() ? it->second : defaultPlayer_;
}

int Database::levelIndex(const std::string& id) const {
    for (std::size_t i = 0; i < levels_.size(); i++) if (levels_[i].id == id) return (int)i;
    return -1;
}
