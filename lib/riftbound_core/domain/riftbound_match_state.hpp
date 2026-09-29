#pragma once

#include <cstddef>
#include <cstdint>

#include "riftbound_score_change.hpp"
#include "domain/match_state.hpp"  // counter::domain::RingBuffer を共用する

namespace counter::riftbound {

constexpr uint8_t kPlayerCount = 2;

// 勝利点の既定値。Riftbound の Duel（1v1）は 8 点で勝利する。
// 2v2 の 11 点へ将来拡張する場合に備え、MatchState のフィールドとして保持する。
constexpr uint8_t kDefaultVictoryScore = 8;

struct PlayerState {
    // 得点。バトルフィールドの征服 + 保持で加算していく。
    // 0 でクランプする（得点が負になることはない）。
    // 勝利点は上限ではない -- 本機は記録補助であり、到達表示は目安。
    // ルール上 8 点で試合は終わるが、Undo や入力修正の余地を残すため
    // 勝利点を超える値も許容する（FaB 版の「開始ライフは上限ではない」と同じ思想）。
    uint32_t score;
};

struct MatchState {
    uint16_t schemaVersion;
    PlayerState players[kPlayerCount];
    // 勝利点。Duel は 8。startMatch() で kDefaultVictoryScore に設定する。
    // Rematch でも維持される（将来 2v2 = 11 への拡張に備えたフィールド）。
    uint8_t  victoryScore;
    bool     active;
    bool     touchLocked;
    uint32_t nextSequence;
    // 履歴容量は FaB 版と同じ 64 件。ScoreChange は FaB の LifeChange と
    // 同程度のサイズ（28 バイト）であり、NVS レコード上限内に収まる。
    counter::domain::RingBuffer<ScoreChange, 64> history;
};

// PlayerId を配列添字に変換するヘルパ。
// players[0] = Top, players[1] = Bottom（docs/06 の定義に合わせる）。
// counter::domain::toIndex と同型だが、riftbound 名前空間内で
// 非修飾呼び出しできるよう独自に定義する。
inline size_t toIndex(PlayerId id) {
    return static_cast<size_t>(id);
}

}  // namespace counter::riftbound
