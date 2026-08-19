#pragma once

#include <cstdint>
#include <cstddef>

namespace counter::config {

// ============================================================
// Phase 0 実測で確定したパラメータ
// ============================================================

// 画面解像度 (px)
// M5.Display.width() / height() の実測値 (Step 2, 2026-08-17)
// 公式仕様は 466x466 だが実測を採用する
constexpr int32_t kDisplayWidth  = 468;
constexpr int32_t kDisplayHeight = 468;

// 画面中心座標 (px)
// 実測値 468 / 2 = 234
constexpr float kCenterX = 234.0f;
constexpr float kCenterY = 234.0f;

// 外周操作リング下限 (px)
// 実測 p0（最小タッチ半径）に一致 (Step 3, 2026-08-17, 826 サンプル)
// タッチ IC は表示領域外（最大 268px）の座標も返すため上限は設けない
constexpr float kRingInnerRadius = 165.0f;

// 振動強度 (0-255)
// 強度 32-192 は体感できず、255 のみ体感可能 (Step 5, 2026-08-17)
// 強度は設計変数にならない。255 固定とする
constexpr uint8_t kVibrationLevel = 255;

// 振動の最小体感時間 (ms)
// 強度 255 で 15ms はかろうじて、20ms で確実に体感できる (Step 5, 2026-08-17)
constexpr uint32_t kMinVibrationMs = 20;

// ============================================================
// Phase 1 実機操作で確定したパラメータ
// ============================================================

// 感度 (度/ライフ)
// Phase 1 実機操作で確定 (2026-08-17)
// 一周 360° = 10 ライフ。旧値 10.0f では一周 36 ライフとなり速すぎた
// 実測スイープ角速度 約 233°/s → 1 ライフあたり約 154 ms
constexpr float kDegreesPerLife = 36.0f;

// 感度プリセット（一周あたりのライフ変動量）
// 5 ライフ = 72.0 度/ライフ、10 ライフ = 36.0 度/ライフ、20 ライフ = 18.0 度/ライフ
// デフォルト（インデックス 1 = 10 ライフ）は既存動作 (kDegreesPerLife = 36.0f) と同一
constexpr uint8_t kSensitivityPresets[] = {5, 10, 20};
constexpr size_t kSensitivityPresetCount =
    sizeof(kSensitivityPresets) / sizeof(kSensitivityPresets[0]);
constexpr size_t kDefaultSensitivityIndex = 1;  // 10 ライフ（既存デフォルト）

// プリセットインデックスから度/ライフ値を計算する
constexpr float degreesPerLifeFromPreset(size_t index) {
    return 360.0f / static_cast<float>(kSensitivityPresets[index]);
}

// ============================================================
// 未検証の初期値（実機テストで調整する前提）
// ============================================================

// キャンセル半径 (px)
// 外周リング下限 165 との間に 20px のヒステリシスを確保する設計値
constexpr float kCancelRadius = 145.0f;

// 最低移動角 (度)
// Candidate -> Active 遷移の閾値。これ未満の移動は操作として扱わない
constexpr float kActivationAngleDeg = 6.0f;

// 角速度しきい値 (度/ms)
// 実測の快適操作時角速度 (約 0.233 度/ms) の約 3 倍に設定
// 値自体はまだ実機で検証していない初期値
constexpr float kMaxAngularSpeedDegPerMs = 0.7f;

// 履歴の保持件数
// リングバッファの容量。超過分は古いものから上書きされる
constexpr size_t kHistoryCapacity = 64;

}  // namespace counter::config
