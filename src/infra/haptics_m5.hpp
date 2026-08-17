#pragma once

#include <cstdint>

namespace counter::infra {

/// M5Stack StopWatch の振動モーターを非ブロッキングで制御する。
///
/// 内部で M5.Power.setVibration() を使用する。
/// - 強度は常に config::kVibrationLevel (255) 固定。
///   実測で 32〜192 は体感できず、255 のみ体感可能なため（Phase 0 Step 5）。
/// - パルスの停止は tick() が行う。delay() は一切使用しない。
///   メインループの非ブロッキング設計を維持するため。
class Haptics {
public:
    /// 初期化。M5.begin() の後に呼ぶこと。
    void begin();

    /// 指定時間だけ振動を開始する。
    /// durationMs が kMinVibrationMs 未満なら切り上げる（それ未満は体感できないため）。
    /// 既に振動中の場合は終了時刻を延長する。
    void pulse(uint32_t durationMs);

    /// ライフ 1 段階ぶんの振動パルス。
    /// 間引きロジックを内蔵するが、現在は kStepThrottleEnabled = false
    /// により無効化されている（ADR-17 案 B 採用のため）。
    /// beginGesture() で初期化されたカウンタは将来の再有効化に備えて維持する。
    void pulseStep();

    /// ジェスチャー開始時に呼ぶ。間引きカウンタを初期化する。
    void beginGesture();

    /// 毎ループ呼ぶ。経過時間を確認し、パルスが終了していたら振動を停止する。
    void tick(uint32_t nowMs);

    /// 現在振動中かどうかを返す。
    bool isActive() const;

private:
    // パルス管理
    uint32_t startMs_    = 0;     // 振動開始時刻 (ms)
    uint32_t durationMs_ = 0;     // 振動継続時間 (ms)
    bool     active_     = false;  // 現在振動中か

    // 間引き管理
    // ADR-17 案 B（感度引き下げ）が実機評価で採用されたため、
    // 間引きは現在無効（kStepThrottleEnabled = false）。
    // カウンタは将来の再有効化に備えて維持する。
    uint32_t stepCount_ = 0;  // ジェスチャー開始からの段階数
};

}  // namespace counter::infra
