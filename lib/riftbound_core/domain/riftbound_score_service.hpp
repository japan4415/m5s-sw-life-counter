#pragma once

#include <cstdint>

#include "riftbound_score_change.hpp"
#include "riftbound_match_state.hpp"

namespace counter::riftbound {

// 得点を変更して履歴に積む。
// requestedDelta == 0 のときは履歴を積まず、appliedDelta = 0 の ScoreChange を返す。
// 得点は 0 でクランプする。勝利点は上限ではない。
// FaB 版 life_service.cpp の applyLifeChange と同じ意味論に従い、
// int64_t でオーバーフロー回避する。
ScoreChange applyScoreChange(MatchState& state, PlayerId player,
                             int32_t requestedDelta, uint32_t uptimeMs);

// 直前の変更を取り消す。
// 差分の逆適用ではなく、履歴に記録された before 値へ直接復元する。
// これによりクランプが発生した変更でも正確に元の状態へ戻せる。
// 履歴が空なら false を返し状態を変えない。
bool undoLast(MatchState& state);

// 新しい試合を開始する。両プレイヤーの得点を 0 にし、
// victoryScore を既定値（Duel = 8）に設定する。履歴をクリアし sequence を 0 に初期化する。
// Riftbound の得点は常に 0 から始まるため、開始値の引数は存在しない
// （FaB 版 startMatch の開始ライフ引数に相当するものが不要）。
void startMatch(MatchState& state);

// 得点 0 でやり直す。victoryScore は維持する。
void rematch(MatchState& state);

// 勝利点に到達したか。score >= victoryScore で true。
// 注意: Riftbound の勝利点（8 点目）には制約があり、どの 8 点でも勝利する
// わけではない（最終点はバトルフィールド保持中、または全バトルフィールド
// 制圧のターンに獲得する必要がある）。true はあくまで「到達」の目安であり、
// ルール裁定はプレイヤーが行う。
bool hasReachedVictory(const MatchState& state, PlayerId player);

}  // namespace counter::riftbound
