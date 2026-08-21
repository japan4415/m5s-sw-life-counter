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

// ============================================================
// 内側タップ判定のしきい値（実機調整前提の設計値）
// ============================================================
// 内側タップ = 短時間で touch start -> end、移動量が小さいこと。
// これらの値は実機テストで調整する前提。

/// タップと判定する最大移動量 (px)。指のブレを吸収するために余裕を持たせた値。
constexpr int16_t kTapMaxMovePx = 20;

/// タップと判定する最大継続時間 (ms)。この時間以内の touch start -> end をタップとみなす。
/// 実機のタッチセンサは中央値 15ms 間隔・最大 270ms の欠測がある
/// (docs/01-hardware.md)。物理的な 200ms のタップが欠測により 470ms として
/// 観測されうるため、300ms では不足する。600ms は欠測を考慮しつつ、
/// 長押し操作との誤判定を避けるバランス。
constexpr uint32_t kTapMaxDurationMs = 600;

// ============================================================
// EDH 用の外周スライド開始角度の判定
// ============================================================
// FaB 版 (lib/counter_core) の isValidStartAngle() は上下 2 分割用で、
// 0° (右端) と 180° (左端) 付近を禁止する。EDH の 4 分割では P2(右) と
// P4(左) の操作領域がその禁止領域と重なるため、EDH 専用の判定が必要。
//
// EDH では 4 扇形の境界（45°/135°/225°/315° = 対角線）付近を不感帯とし、
// 各扇形の中心角付近からの開始のみを許可する。

/// 扇形境界付近の不感帯の半幅 (度)。実機調整前提。
/// 例: 15° の場合、45° ± 15° = 30°〜60° が不感帯となる。
constexpr float kEdhDeadZoneHalfWidthDeg = 15.0f;

/// EDH の 4 分割レイアウト用の外周スライド開始角度判定。
/// 4 扇形の境界（対角線）付近の不感帯にあるタッチ開始を拒否する。
///
/// @param angleDeg angleDegrees() が返す [0, 360) の角度
/// @return true: 開始を許可する、false: 不感帯のため拒否する
bool isValidStartAngleEdh(float angleDeg);

// ============================================================
// 座標回転ヘルパー（GestureDetector の FaB 禁止領域を迂回するため）
// ============================================================
// GestureDetector (lib/counter_core) は内部で FaB 用の isValidStartAngle()
// を呼ぶ。EDH の P2/P4 は FaB の禁止領域に該当するため、座標を 90° 回転
// させて GestureDetector に渡すことで迂回する。回転は角度差分を保存するため、
// ライフ変化量の計算には影響しない。

/// 画面座標 (x, y) を中心 (234, 234) を軸に 90° 反時計回りに回転する。
/// 結果を outX, outY に書き込む。
void rotateCCW90(int16_t x, int16_t y, int16_t& outX, int16_t& outY);

/// P2/P4 セクターで座標回転が必要かを返す。
/// P1(0), P3(2) は FaB の許可領域と重なるため回転不要。
/// P2(1), P4(3) は FaB の禁止領域に該当するため回転が必要。
inline bool needsCoordinateRotation(uint8_t sectorIndex) {
    return sectorIndex == 1 || sectorIndex == 3;
}

}  // namespace counter::edh
