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
    // 履歴容量 16 件。FaB 版 (64 件) より小さいのは、EDH の LifeChange が
    // 統率者ダメージフィールドを含み 1 件あたりのサイズが大きいため。
    // NVS パーティション (20KB) に 16 スロット分を収めるための制約。
    // 16 件 = 4 人 × 4 ラウンド分のUndoに相当し、実用上十分。
    counter::domain::RingBuffer<LifeChange, 16> history;
};

}  // namespace counter::edh
