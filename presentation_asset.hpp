#pragma once
#include <map>
#include <string>

// An entity's presentation is data, not its gameplay identity. Paths refer to existing asset
// loaders; named clips and cues can be consumed by the relevant animation/audio systems.
struct PresentationAsset {
    std::string sprite;
    std::string model;
    std::map<std::string, std::string> animationClips;
    std::map<std::string, std::string> soundCues;
};
