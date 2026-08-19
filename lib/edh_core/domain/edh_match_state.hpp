#pragma once

#include <cstdint>

#include "edh_life_change.hpp"
#include "domain/match_state.hpp"  // counter::domain::RingBuffer を共用する

namespace counter::edh {

struct PlayerState {
    uint32_t startingLife;                      // 既定 40
    uint32_t life;                              // 0 でクランプ
    uint8_t  commanderDamageFrom[kPlayerCount]; // 自番号は未使用。0〜99 でクランプ
};

struct MatchState {
    uint16_t schemaVersion;
    PlayerState players[kPlayerCount];
    bool     active;
    bool     touchLocked;
    uint32_t nextSequence;
    counter::domain::RingBuffer<LifeChange, 64> history;
};

}  // namespace counter::edh
