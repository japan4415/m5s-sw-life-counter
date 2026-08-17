#include "haptics_m5.hpp"

#include <M5Unified.h>

#include "app_config.hpp"

namespace counter::infra {

// ============================================================
// 間引きパラメータ
// ============================================================

// ADR-17 は「案 A: 間引き」と「案 B: 感度引き下げ」を未決定としていた。
// 実機評価の結果、案 B（感度引き下げ）が採用された。
//
// kDegreesPerLife が 10.0f → 36.0f に変更されたことにより:
// - ライフ 1 段階あたり約 154 ms（36 / 233 * 1000）
// - 20 ms パルスでデューティ比約 13%（20 / 154）
// - 間引かなくても十分なクリック感が得られる
//
// 旧計算（感度 10 度/ライフ時）:
// - ライフ 1 段階あたり約 43 ms（10 / 233 * 1000）
// - 20 ms パルスでデューティ比約 47% → 連続振動になりクリック感が失われていた
//
// 注意: 感度を上げ直した場合（kDegreesPerLife を小さくした場合）は
// 間引きの再検討が必要である。デューティ比が 30% を超えるあたりから
// クリック感が失われ始める。

// 現在は無効。感度変更で間引きが不要になったため（上記コメント参照）。
constexpr bool     kStepThrottleEnabled   = false;

constexpr uint32_t kStepThrottleThreshold = 5;  // この段階数まで毎回鳴らす
constexpr uint32_t kStepThrottleInterval  = 2;  // 閾値超過後は N 段階に 1 回鳴らす

void Haptics::begin() {
    // M5.begin() が M5IOE1 の PWM 設定（GPIO9 の 12bit PWM）を
    // 含む全ハードウェアの初期化を済ませるため、ここでの追加初期化は不要。
    // setVibration(0) も不要: M5.begin() 直後はモーター停止状態。
}

void Haptics::pulse(uint32_t durationMs) {
    // 体感できない短すぎるパルスを防ぐ。
    // 実測で 20 ms 未満は体感困難（Phase 0 Step 5）。
    if (durationMs < config::kMinVibrationMs) {
        durationMs = config::kMinVibrationMs;
    }

    if (active_) {
        // 既に振動中の場合: 終了時刻を延長する。
        // 新しい durationMs で上書きし、開始時刻は現在の startMs_ を維持する。
        // これにより、短い間隔で pulse() が連続呼び出しされても
        // 振動が途切れずに延長される。
        //
        // 二重に setVibration() を呼んでも害はない（PWM 値の再設定のみ）が、
        // 既に同じ強度で動作中なので呼ばない。意図は終了時刻の延長のみ。
        uint32_t newEndMs = startMs_ + durationMs;
        uint32_t currentEndMs = startMs_ + durationMs_;
        if (newEndMs > currentEndMs) {
            durationMs_ = durationMs;
        }
    } else {
        // 新規開始。
        // 強度は 255 固定: 実測で 32〜192 は体感できず、
        // 255 のみ体感可能（Phase 0 Step 5）。
        M5.Power.setVibration(config::kVibrationLevel);
        active_     = true;
        startMs_    = 0;  // tick() で nowMs を受け取るまで仮値。次の tick() で確定する
        durationMs_ = durationMs;
    }
}

void Haptics::pulseStep() {
    ++stepCount_;

    // 間引きが無効の場合は毎段階パルスを鳴らす。
    if (!kStepThrottleEnabled) {
        pulse(config::kMinVibrationMs);
        return;
    }

    // --- 以下は kStepThrottleEnabled == true の場合のみ到達する ---

    // 間引き判定:
    // 最初の kStepThrottleThreshold 段階は毎回鳴らす。
    // 操作開始直後はフィードバックの応答性が重要なため。
    //
    // それ以降は kStepThrottleInterval 段階に 1 回だけ鳴らす。
    // 高速スライド時にデューティ比が高くなりすぎると
    // 個々のクリック感が失われ連続振動になるため（実測: 間引きなしで約 47%）。
    if (stepCount_ <= kStepThrottleThreshold) {
        // 最初の数段階: 毎回鳴らす
        pulse(config::kMinVibrationMs);
    } else {
        // 閾値超過: N 段階に 1 回だけ鳴らす
        uint32_t stepsAfterThreshold = stepCount_ - kStepThrottleThreshold;
        if (stepsAfterThreshold % kStepThrottleInterval == 0) {
            pulse(config::kMinVibrationMs);
        }
        // 鳴らさない回でもカウンタは進める（++stepCount_ は関数冒頭で実行済み）
    }
}

void Haptics::beginGesture() {
    // ジェスチャー開始時に間引きカウンタをリセットする。
    // 新しいジェスチャーごとに最初の数段階は毎回鳴るようにする。
    stepCount_ = 0;
}

void Haptics::tick(uint32_t nowMs) {
    if (!active_) {
        return;
    }

    // pulse() で開始された直後の最初の tick() で開始時刻を確定する。
    // pulse() 自体は nowMs を受け取らないため、tick() で補完する。
    if (startMs_ == 0) {
        startMs_ = nowMs;
    }

    // 経過時間が継続時間を超えたら停止する。
    // ここでしか setVibration(0) を呼ばない。
    if (nowMs - startMs_ >= durationMs_) {
        M5.Power.setVibration(0);
        active_ = false;
    }
}

bool Haptics::isActive() const {
    return active_;
}

}  // namespace counter::infra
