#pragma once

#include <cstdint>

#include "life_change.hpp"
#include "match_state.hpp"

namespace counter::domain {

// ライフを変更して履歴に積む。
// requestedDelta == 0 のときは履歴を積まず、appliedDelta = 0 の LifeChange を返す。
// docs/06 の applyLifeChange の意味論に従い、int64_t でオーバーフロー回避する。
LifeChange applyLifeChange(MatchState& state, PlayerId player,
                           int32_t requestedDelta, uint32_t uptimeMs);

// 直前の変更を取り消す。
// 差分の逆適用ではなく、履歴に記録された before 値へ直接復元する。
// これによりクランプが発生した変更でも正確に元の状態へ戻せる。
// 履歴が空なら false を返し状態を変えない。
bool undoLast(MatchState& state);

// 上下プレイヤーを入れ替える（ライフと開始ライフの両方）。
void swapSides(MatchState& state);

// 新しい試合を開始する。履歴をクリアし sequence を 0 に初期化する。
void startMatch(MatchState& state, uint32_t topStartingLife,
                uint32_t bottomStartingLife);

// 同じ開始ライフでやり直す。
void rematch(MatchState& state);

}  // namespace counter::domain
