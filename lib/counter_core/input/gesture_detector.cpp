#include "input/gesture_detector.hpp"

#include <cmath>

#include "app_config.hpp"
#include "input/touch_zone.hpp"

namespace counter::input {

void GestureDetector::reset() {
    state_          = GestureState::Idle;
    player_         = PlayerId::Top;
    prevAngle_      = 0.0f;
    accumDeg_       = 0.0f;
    prevStep_       = 0;
    stepChanged_    = false;
    rejectedStart_  = false;
    prevMs_         = 0;
}

void GestureDetector::onTouchDown(int16_t x, int16_t y, uint32_t nowMs) {
    // 前回のジェスチャーが残っていても安全にリセットして新規開始する
    reset();

    const float radius = radiusFromCenter(x, y);

    // 外周リング上でなければ操作として認めない。Idle のまま。
    if (!isOnRing(radius)) {
        return;
    }

    const PlayerId player = selectPlayer(y);
    const float angle     = angleDegrees(x, y);

    // 開始許可領域のチェック。画面左右の端（各 20 度の禁止領域）からは
    // 操作を開始させない。上下どちらのプレイヤーを操作しようとしているか
    // 曖昧な位置で誤って操作が始まることを防ぐため。
    if (!isValidStartAngle(angle, player)) {
        rejectedStart_ = true;
        return;  // Idle のまま。呼び出し側は consumeRejectedStart() で拒否を検知する。
    }

    state_     = GestureState::Candidate;
    // 対象プレイヤーはタッチ開始地点の y 座標で固定する。
    // スライド中に上下境界を越えても変えない（誤操作防止）。
    player_    = player;
    prevAngle_ = angle;
    accumDeg_  = 0.0f;
    prevStep_  = 0;
    prevMs_    = nowMs;
}

void GestureDetector::onTouchMove(int16_t x, int16_t y, uint32_t nowMs) {
    // Idle: onTouchDown でリング外に触れた場合。動きを追跡する必要がない。
    // Cancelled: キャンセル済み。指を離すまで確定しない。
    if (state_ == GestureState::Idle || state_ == GestureState::Cancelled) {
        return;
    }

    // キャンセル判定: 指が中央方向へ引き込まれた場合。
    // Candidate でも Active でも、半径 kCancelRadius（145px）未満で即キャンセル。
    const float radius = radiusFromCenter(x, y);
    if (isInCancelZone(radius)) {
        state_       = GestureState::Cancelled;
        stepChanged_ = false;
        return;
    }

    const float currentAngle = angleDegrees(x, y);
    const float deltaDeg     = normalizeDeltaDegrees(currentAngle - prevAngle_);
    const uint32_t deltaMs   = nowMs - prevMs_;

    // deltaMs == 0 のサンプルはゼロ除算を避けて無視する。
    // 同一タイムスタンプのサンプルは角速度を計算できないため、
    // 前回角・前回時刻ともに更新せずスキップする。
    if (deltaMs == 0) {
        return;
    }

    // 異常な角速度のサンプルを除外する（ADR-14）。
    //
    // Phase 0 Step 3 実測（2026-08-17）でサンプル間隔の中央値 15ms（約 66Hz）
    // に対し、最大 270ms の欠測が確認されている。
    //
    // 旧設計の絶対角度しきい値（30度/サンプル）では、欠測後の正当な操作
    // （例: 快適操作時の角速度 233度/秒 x 270ms = 約 63度ジャンプ）を
    // 誤って除外してしまう。時間あたりの角速度（度/ms）で判定することで、
    // 欠測からの復帰を正しく扱える。
    //
    // しきい値 0.7度/ms（700度/秒）は実測の快適操作時角速度（約 0.233度/ms）
    // の約 3 倍。まだ実機で検証していない初期値であり調整しうる。
    const float angularSpeed =
        std::abs(deltaDeg) / static_cast<float>(deltaMs);
    if (angularSpeed > config::kMaxAngularSpeedDegPerMs) {
        // 累積には加えないが、前回角と前回時刻は更新する。
        // これにより次のサンプルで正常な差分計算に復帰できる。
        prevAngle_ = currentAngle;
        prevMs_    = nowMs;
        return;
    }

    // 角度を累積する。normalizeDeltaDegrees による境界またぎ補正済み。
    accumDeg_  += deltaDeg;
    prevAngle_  = currentAngle;
    prevMs_     = nowMs;

    // Candidate -> Active: 累積角度の絶対値がしきい値以上で有効化。
    // kActivationAngleDeg（6度）はタップ誤操作を防ぐための最低移動角。
    if (state_ == GestureState::Candidate) {
        if (std::abs(accumDeg_) >= config::kActivationAngleDeg) {
            state_ = GestureState::Active;
        }
    }

    // ライフ段階を計算する。時計回り（accumDeg_ 正）が増加方向。
    // static_cast<int32_t> はゼロ方向への切り捨て。
    // 例: accumDeg_ = -15 -> steps = -1、accumDeg_ = 15 -> steps = 1
    const int32_t currentStep =
        static_cast<int32_t>(accumDeg_ / config::kDegreesPerLife);

    // 段階が変わったかを記録する（Haptics の振動フィードバック判定用）。
    // consumeStepChanged() で 1 回だけ読み取れる。
    if (currentStep != prevStep_) {
        stepChanged_ = true;
        prevStep_    = currentStep;
    }
}

GestureResult GestureDetector::onTouchUp(uint32_t /*nowMs*/) {
    GestureResult result{};
    result.committed = false;
    result.player    = player_;
    result.deltaLife = 0;

    if (state_ == GestureState::Active) {
        const int32_t steps =
            static_cast<int32_t>(accumDeg_ / config::kDegreesPerLife);
        if (steps != 0) {
            result.committed = true;
            result.deltaLife = steps;
        }
        // steps == 0: Active だが方向反転で結果的に 0 段階。確定しない。
    }
    // Candidate: 最低移動角に達しなかった。確定しない。
    // Cancelled: 中央引き込みでキャンセル済み。確定しない。

    // ジェスチャー終了。次のタッチに備えて Idle に戻す。
    state_       = GestureState::Idle;
    stepChanged_ = false;

    return result;
}

GestureState GestureDetector::state() const {
    return state_;
}

GesturePreview GestureDetector::preview() const {
    GesturePreview p{};
    p.active    = (state_ == GestureState::Active);
    p.player    = player_;
    // プレビューは Active 状態でのみ意味がある。
    // Candidate ではまだプレビューを表示しない（最低移動角に達していない）。
    p.deltaLife = p.active
                      ? static_cast<int32_t>(accumDeg_ / config::kDegreesPerLife)
                      : 0;
    return p;
}

bool GestureDetector::consumeStepChanged() {
    // ワンショット: 1 回 true を返したら次は false になる。
    // Haptics が段階変化ごとに 1 回だけ振動パルスを出すために使う。
    const bool changed = stepChanged_;
    stepChanged_ = false;
    return changed;
}

bool GestureDetector::consumeRejectedStart() {
    // ワンショット: consumeStepChanged() と同じ消費型パターン。
    // 呼び出し側（InputController）が開始禁止領域での警告振動（20ms x 1回）
    // を 1 回だけ鳴らすために使う。
    const bool rejected = rejectedStart_;
    rejectedStart_ = false;
    return rejected;
}

}  // namespace counter::input
