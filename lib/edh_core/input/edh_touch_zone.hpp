#pragma once

#include <cstdint>

namespace counter::edh {

/// 中心からの相対座標で 4 扇形（P1=上, P2=右, P3=下, P4=左）のインデックスを返す。
/// 戻り値: 0=P1(上), 1=P2(右), 2=P3(下), 3=P4(左)
/// 境界（|dx| == |dy|）は上下の扇形を優先する（設計決定。コメント参照）。
uint8_t selectSector(int16_t x, int16_t y);

/// 外周リング上か（kRingInnerRadius 以上）
bool isOnOuterRing(int16_t x, int16_t y);

/// 内側領域か（kRingInnerRadius 未満）
bool isInInnerZone(int16_t x, int16_t y);

/// キャンセル領域か（kCancelRadius 未満）
bool isInCancelZone(int16_t x, int16_t y);

}  // namespace counter::edh
