#pragma once

#include <cstdint>

namespace counter::edh {

constexpr uint8_t kPlayerCount = 4;
constexpr uint8_t kSourceNone  = 0xFF;  // 通常ライフ操作を示す（統率者ダメージではない）

struct LifeChange {
    uint32_t sequence;      // 単調増加する通し番号
    uint8_t  playerIndex;   // 対象プレイヤー (0〜3)
    uint8_t  sourceIndex;   // 被弾元プレイヤー (0〜3)。kSourceNone = 通常ライフ操作
    int16_t  delta;         // 実際に適用された変化量（クランプ後の差分）。
                            // 通常操作: 実際のライフ変化量。統率者操作: 実際のダメージ変化量。
                            // History 画面での表示に使う。Undo は lifeBefore/cmdDmgBefore
                            // からの復元で行うため delta は参照しない。
    uint32_t lifeBefore;    // 変更前のライフ値
    uint32_t lifeAfter;     // 変更後のライフ値
    uint8_t  cmdDmgBefore;  // 変更前の統率者ダメージ（通常操作時は 0）
    uint8_t  cmdDmgAfter;   // 変更後の統率者ダメージ（通常操作時は 0）
    uint32_t uptimeMs;      // 変更時点のシステム稼働時間
};

}  // namespace counter::edh
