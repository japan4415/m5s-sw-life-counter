#include "input/edh_touch_zone.hpp"

#include <cmath>

#include "app_config.hpp"  // counter::config::kCenterX/Y, kRingInnerRadius, kCancelRadius

namespace counter::edh {

uint8_t selectSector(int16_t x, int16_t y) {
    const float dx = static_cast<float>(x) - counter::config::kCenterX;
    const float dy = static_cast<float>(y) - counter::config::kCenterY;

    const float absDx = std::abs(dx);
    const float absDy = std::abs(dy);

    // 仕様書「タッチの扇形判定」に従う。
    // |dy| > |dx| の場合は上下の扇形、それ以外は左右の扇形。
    //
    // 境界（|dx| == |dy|）の扱い:
    // |dx| == |dy| は else 分岐（左右）に入る。しかし実用上、
    // 完全に |dx| == |dy| となるタッチは稀であり、ユーザー体験への影響は無視できる。
    // 将来的に不感帯を設ける場合は、absDy と absDx の差が閾値未満のときに
    // 入力を無視する方式を検討する。
    if (absDy > absDx) {
        // 上下の扇形
        if (dy < 0.0f) {
            return 0;  // P1 (上)
        } else {
            return 2;  // P3 (下)
        }
    } else {
        // 左右の扇形
        if (dx > 0.0f) {
            return 1;  // P2 (右)
        } else {
            return 3;  // P4 (左)
        }
    }
}

bool isOnOuterRing(int16_t x, int16_t y) {
    const float dx = static_cast<float>(x) - counter::config::kCenterX;
    const float dy = static_cast<float>(y) - counter::config::kCenterY;
    const float radius = std::sqrt(dx * dx + dy * dy);
    return radius >= counter::config::kRingInnerRadius;
}

bool isInInnerZone(int16_t x, int16_t y) {
    return !isOnOuterRing(x, y);
}

bool isInCancelZone(int16_t x, int16_t y) {
    const float dx = static_cast<float>(x) - counter::config::kCenterX;
    const float dy = static_cast<float>(y) - counter::config::kCenterY;
    const float radius = std::sqrt(dx * dx + dy * dy);
    return radius < counter::config::kCancelRadius;
}

// ============================================================
// EDH 用のスライド開始角度判定
// ============================================================

bool isValidStartAngleEdh(float angleDeg) {
    // EDH の 4 扇形は対角線（45°/135°/225°/315°）で区切られる。
    // 対角線付近からスライドを開始すると、どの扇形を操作しようとしているか
    // 曖昧なため、境界 ± kEdhDeadZoneHalfWidthDeg の不感帯を設ける。
    //
    // 不感帯の角度範囲（kEdhDeadZoneHalfWidthDeg = 15° の場合）:
    //   30°〜 60° (45° ± 15°)   — P1/P2 境界
    //  120°〜150° (135° ± 15°)  — P2/P3 境界
    //  210°〜240° (225° ± 15°)  — P3/P4 境界
    //  300°〜330° (315° ± 15°)  — P4/P1 境界
    //
    // [0, 360) に正規化された angleDeg を前提とする。

    // 4 つの境界角度に対してチェックする
    constexpr float kBoundaryAngles[] = {45.0f, 135.0f, 225.0f, 315.0f};

    for (float boundary : kBoundaryAngles) {
        float diff = angleDeg - boundary;
        // [-180, 180) に正規化
        if (diff > 180.0f) diff -= 360.0f;
        if (diff <= -180.0f) diff += 360.0f;

        if (std::abs(diff) < kEdhDeadZoneHalfWidthDeg) {
            return false;  // 不感帯内 → 開始を拒否する
        }
    }

    return true;  // いずれの不感帯にも該当しない → 開始を許可する
}

// ============================================================
// 座標回転ヘルパー
// ============================================================

void rotateCCW90(int16_t x, int16_t y, int16_t& outX, int16_t& outY) {
    // 中心 (234, 234) を軸に 90° 反時計回りに回転する。
    // 回転行列: [cos90, sin90; -sin90, cos90] = [0, 1; -1, 0]
    //   dx' =  dy
    //   dy' = -dx
    // 結果: outX = center + dy, outY = center - dx
    const int16_t cx = 234;
    const int16_t cy = 234;
    const int16_t dx = x - cx;
    const int16_t dy = y - cy;
    outX = static_cast<int16_t>(cx + dy);
    outY = static_cast<int16_t>(cy - dx);
}

}  // namespace counter::edh
