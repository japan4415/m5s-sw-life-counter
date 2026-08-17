#include "input/touch_zone.hpp"

#include <cmath>

#include "app_config.hpp"

namespace counter::input {

namespace {
// M_PI は POSIX 拡張であり C++17 で保証されないため、自前で定義する。
// Native テスト環境（macOS/Linux）とターゲット（ESP32-S3）の両方で
// ビルドできることを保証するため。
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegPerRad = 180.0f / kPi;

// 開始許可領域の角度境界（docs/05-ui-ux.md で定義）。
// angleDegrees() が返す [0, 360) の座標系において:
//   0° = 画面右端、90° = 画面下端、180° = 画面左端、270° = 画面上端
// 画面左右の端は上下どちらのプレイヤーを操作しようとしているか曖昧なため、
// 各 20 度ずつの開始無効領域を設けて誤操作を防止する。
//
// これらの定数は app_config.hpp ではなくここに置く。
// app_config.hpp は別エージェント担当のため編集できない。
// 未検証のパラメータであり、実機テストで調整する前提。
constexpr float kTopStartMinDeg    = 200.0f;  // 上側許可領域の開始角
constexpr float kTopStartMaxDeg    = 340.0f;  // 上側許可領域の終了角
constexpr float kBottomStartMinDeg =  20.0f;  // 下側許可領域の開始角
constexpr float kBottomStartMaxDeg = 160.0f;  // 下側許可領域の終了角
}  // namespace

float radiusFromCenter(int16_t x, int16_t y) {
    const float dx = static_cast<float>(x) - config::kCenterX;
    const float dy = static_cast<float>(y) - config::kCenterY;
    return std::sqrt(dx * dx + dy * dy);
}

float angleDegrees(int16_t x, int16_t y) {
    const float dx = static_cast<float>(x) - config::kCenterX;
    const float dy = static_cast<float>(y) - config::kCenterY;
    // atan2(dy, dx) は画面座標系（y 軸下向き）では時計回りが正の方向になる。
    // 数学的な標準座標系（y 軸上向き）とは逆だが、画面座標ではこれが自然。
    // Phase 0 Step 3 実測（2026-08-17、826 サンプル）で一周あたり
    // +363.6 度 / -372.3 度を記録し、符号反転が不要であることを確認済み。
    float deg = std::atan2(dy, dx) * kDegPerRad;
    // [0, 360) に正規化する。開始許可領域の角度判定（200-340度 / 20-160度）
    // に合わせるため。
    if (deg < 0.0f) {
        deg += 360.0f;
    }
    return deg;
}

bool isOnRing(float radius) {
    // 外周リング下限は Phase 0 Step 3 実測で確定した p0（最小タッチ半径）。
    // 上限は設けない。タッチ IC が表示領域外（半径 234px 超、最大 268px）の
    // 座標も返すため（ADR-13 で決定）。
    return radius >= config::kRingInnerRadius;
}

bool isInCancelZone(float radius) {
    // リング下限 165px との間に 20px のヒステリシスを持たせている。
    // ヒステリシスにより、リング境界付近でのチャタリングを防止する。
    return radius < config::kCancelRadius;
}

PlayerId selectPlayer(int16_t y) {
    // タッチ開始地点の y 座標でプレイヤーを決定する。
    // y < center = 上側プレイヤー（対戦相手）、y >= center = 下側プレイヤー（自分）。
    // 操作開始後はプレイヤーを固定し、スライド中に上下境界を越えても変えない。
    // この関数は onTouchDown でのみ呼び出される前提。
    return static_cast<float>(y) < config::kCenterY
               ? PlayerId::Top
               : PlayerId::Bottom;
}

float normalizeDeltaDegrees(float delta) {
    // 360度/0度の境界をまたぐ角度差を (-180, 180] に正規化する。
    // 例: 350度 → 10度 への時計回り移動は、生の差分が -340度 になるが、
    // 正規化により +20度 として正しく扱える。
    //
    // Phase 0 Step 3 実測でサンプル間隔の最大 270ms の欠測が確認されている。
    // 欠測後の 1 サンプルで大きな角度変化が発生するため、境界またぎの
    // 正規化は正しい回転方向を復元するために不可欠である。
    while (delta > 180.0f) {
        delta -= 360.0f;
    }
    while (delta <= -180.0f) {
        delta += 360.0f;
    }
    return delta;
}

bool isValidStartAngle(float degrees, PlayerId player) {
    // 画面左右の端（各 20 度）は上下どちらのプレイヤーを操作しようとしているか
    // 曖昧なため、開始を禁止する。許可領域は上下それぞれ 140 度の弧。
    //
    // angleDegrees() の [0, 360) 座標系に直接対応する:
    //   上側プレイヤー: 200° ~ 340°（上半円、左端から時計回りに上端を経て右端へ）
    //   下側プレイヤー:  20° ~ 160°（下半円、右端から時計回りに下端を経て左端へ）
    //   禁止領域: 160° ~ 200°（左端付近）、340° ~ 20°（右端付近、0° をまたぐ）
    if (player == PlayerId::Top) {
        return degrees >= kTopStartMinDeg && degrees <= kTopStartMaxDeg;
    }
    return degrees >= kBottomStartMinDeg && degrees <= kBottomStartMaxDeg;
}

}  // namespace counter::input
