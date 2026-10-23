#pragma once

#include <cstdint>

#include "domain/life_change.hpp"  // counter::PlayerId を共用する

namespace counter::riftbound {

// counter::PlayerId を counter::riftbound からも非修飾で参照できるようにする。
// 1v1 のため EDH 版のような playerIndex 数値ではなく FaB 版と同じ
// Top / Bottom の 2 値 enum をそのまま使う。
using counter::PlayerId;

struct ScoreChange {
    uint32_t sequence;       // 単調増加する通し番号
    PlayerId player;         // 対象プレイヤー
    int32_t  requestedDelta; // 要求された変化量（スライド操作量）
    int32_t  appliedDelta;   // 実際に適用された変化量（クランプ後の差分）。
                             // History 画面での表示に使う。Undo は before からの
                             // 復元で行うため appliedDelta は Undo では参照しない。
    uint32_t before;         // 変更前の得点
    uint32_t after;          // 変更後の得点
    uint32_t uptimeMs;       // 変更時点のシステム稼働時間
};

}  // namespace counter::riftbound
