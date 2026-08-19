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

}  // namespace counter::edh
