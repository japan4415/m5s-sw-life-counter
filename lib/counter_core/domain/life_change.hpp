#pragma once

#include <cstdint>

namespace counter {

enum class PlayerId : uint8_t { Top, Bottom };

}  // namespace counter

namespace counter::domain {

// counter::PlayerId を counter::domain からも非修飾で参照できるようにする。
// input 層など他の名前空間からは counter::PlayerId として参照する。
using counter::PlayerId;

struct LifeChange {
    uint32_t sequence;
    PlayerId player;
    int32_t  requestedDelta;
    int32_t  appliedDelta;
    uint32_t before;
    uint32_t after;
    uint32_t uptimeMs;
};

}  // namespace counter::domain
