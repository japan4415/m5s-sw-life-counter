#pragma once

#include <cstdint>

#include "domain/life_change.hpp"

namespace counter::input {

/// 中心からの半径を返す
float radiusFromCenter(int16_t x, int16_t y);

/// 中心からの角度を度で返す。画面座標系（y が下向き）のため、
/// ユーザーから見た時計回りで値が増加する（Phase 0 実測で確定）。
/// 戻り値は [0, 360) の範囲。
float angleDegrees(int16_t x, int16_t y);

/// 外周リング上か。上限は設けない（Phase 0 実測により ADR-13 で決定）。
bool isOnRing(float radius);

/// 中央へ引き込まれたか（キャンセル判定）
bool isInCancelZone(float radius);

/// タッチ開始地点から操作対象プレイヤーを決める
PlayerId selectPlayer(int16_t y);

/// 角度差を (-180, 180] に畳む。境界またぎの補正。
float normalizeDeltaDegrees(float delta);

/// タッチ開始地点として許可される領域か。
/// 画面左右の端（各 20 度）は上下どちらのプレイヤーか曖昧なため開始を禁じる。
bool isValidStartAngle(float degrees, PlayerId player);

}  // namespace counter::input
