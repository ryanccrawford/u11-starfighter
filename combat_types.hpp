#pragma once
#include <cstdint>

// Shared vocabulary for ships, weapons and level objects. Neutral objects are not
// automatically hostile; a future relationship policy may define other allegiances.
enum class Faction { Player, Enemy, Neutral };
enum class WorldObjectType { Ship, Turret, Door, Scenery };
enum class TargetState { Inactive, Active, Disabled, Destroyed };

// IDs must be assigned by the owning object registry; zero means no registered owner.
using ObjectId = std::uint64_t;
constexpr ObjectId kNoObject = 0;
struct Ownership {
    ObjectId owner = kNoObject;
    Faction faction = Faction::Neutral;
};
struct TargetProfile {
    WorldObjectType type = WorldObjectType::Scenery;
    TargetState state = TargetState::Inactive;
    bool destructible = false;
    float threat = 0.f;
};
