#pragma once

#include <cstdint>

#include "edh_life_change.hpp"
#include "edh_match_state.hpp"

namespace counter::edh {

// 新しい試合を開始する。4 人に初期ライフを設定し、統率者ダメージを 0 クリアする。
void startMatch(MatchState& state, uint32_t startingLife = 40);

// 通常ライフ操作。ライフを変更して履歴に積む。
// delta == 0 のときは履歴を積まない。
// ライフは 0 でクランプする。
LifeChange applyLifeChange(MatchState& state, uint8_t playerIndex,
                           int16_t delta, uint32_t uptimeMs);

// 統率者ダメージ操作。統率者ダメージを変更し、ライフ連動を適用して履歴に積む。
// delta はダメージ変更量（正 = ダメージ増加 = ライフ減少、負 = ダメージ減少 = ライフ増加）。
// delta == 0 のときは履歴を積まない。
// 統率者ダメージは 0〜99 でクランプし、ライフ連動はクランプ後の実際の変化量で行う。
LifeChange applyCommanderDamage(MatchState& state, uint8_t playerIndex,
                                uint8_t sourceIndex, int16_t delta,
                                uint32_t uptimeMs);

// 直前の変更を取り消す。
// lifeBefore / cmdDmgBefore への直接復元で行う。
// 履歴が空なら false を返し状態を変えない。
bool undoLast(MatchState& state);

// 同じ開始ライフでやり直す。
void rematch(MatchState& state);

// 敗北判定。ライフ 0 以下、またはいずれかの被弾元からの統率者ダメージが 21 以上。
bool isDefeated(const MatchState& state, uint8_t playerIndex);

}  // namespace counter::edh
