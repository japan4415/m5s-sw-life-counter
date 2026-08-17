#pragma once

// 色・寸法の定義を集約する。
//
// 色覚差に依存しない方針 (docs/05-ui-ux.md) に従い、
// 色だけで情報を伝えず、アイコン・枠線・テキスト (+/- 符号) を併用する。

#include <cstdint>

#include "app_config.hpp"

namespace counter::ui::theme {

// ============================================================
// 色定義 (RGB565)
// ============================================================

// --- 背景 ---
constexpr uint16_t kBgColor = 0x0000;  // 黒

// --- ライフ数字 ---
constexpr uint16_t kLifeColor        = 0xFFFF;  // 白（通常時）
constexpr uint16_t kPreviewLifeColor = 0xBDF7;  // 明るいグレー（プレビュー中）

// --- 差分表示 ---
// 色覚差対応: オレンジとシアンは輝度差が大きく、
// 大半の色覚特性 (1 型・2 型・3 型) で識別できる。
// さらに "+"/"-" 符号のテキスト情報で冗長に方向を示す。
constexpr uint16_t kDeltaDecreaseColor = 0xFB40;  // 暖色オレンジ（減少）
constexpr uint16_t kDeltaIncreaseColor = 0x07FF;  // 寒色シアン（増加）

// --- ライフ 0 警告 ---
// 色だけに頼らず枠線 + "!" テキストを併用する (docs/05-ui-ux.md)
constexpr uint16_t kLifeZeroColor           = 0xFB40;  // オレンジ（数字色）
constexpr uint16_t kLifeZeroBorderColor     = 0xFB40;  // 枠線色
constexpr int32_t  kLifeZeroBorderThickness = 3;       // 枠線太さ (px)

// --- 外周リング ---
constexpr uint16_t kRingNormalColor    = 0x2104;  // ダークグレー（通常）
constexpr uint16_t kRingHighlightColor = 0x4A69;  // 明るいグレー（操作中）
constexpr uint16_t kRingDimColor       = 0x0841;  // 極暗グレー（非操作側）

// --- 中央分割帯 ---
constexpr uint16_t kDividerColor  = 0x2945;  // ダークグレー
constexpr int32_t  kDividerHeight = 2;       // 高さ (px)

// ============================================================
// リング寸法
// ============================================================

// タッチ判定のリング内周と一致させる (config::kRingInnerRadius = 165)
constexpr int32_t kRingInnerR = static_cast<int32_t>(config::kRingInnerRadius);
// 表示円 (半径 234) から 2px 内側に設定し、丸型画面の縁が綺麗に見えるようにする
constexpr int32_t kRingOuterR = 232;

// ============================================================
// 数字領域の矩形
// ============================================================
//
// リング内周 (r=165) の円内に全角が収まるようにする。
// これにより drawRingHighlight() と drawLife() が互いに干渉せず、
// 独立して部分転送できる。
//
// 幅 180、高さ 120 のとき、中心 (234, 234) から最も遠い角:
//   sqrt(90^2 + 134^2) = sqrt(8100 + 17956) ≈ 161.4 < 165
// → リング内周の内側に収まる。
//
// 仕様は 200x120 を目安としているが、リング非重複の制約により
// 180x120 を採用した。転送サイズ 180*120*2 = 43,200 bytes で、
// 全画面転送 (438,048 bytes, 44.6 ms) に比べて約 1/10。
// 実測比例推定で約 4.5 ms となり、43 ms の描画予算に十分余裕がある。

constexpr int32_t kLifeRegionW = 180;
constexpr int32_t kLifeRegionH = 120;

// 上側プレイヤー領域（左上角座標）
// 中心は (234, 160) — 画面中心から 74px 上
constexpr int32_t kLifeTopX = (config::kDisplayWidth - kLifeRegionW) / 2;   // 144
constexpr int32_t kLifeTopY = 100;

// 下側プレイヤー領域（左上角座標）
// 中心は (234, 308) — 画面中心から 74px 下（上側と対称）
constexpr int32_t kLifeBottomX = kLifeTopX;                                  // 144
constexpr int32_t kLifeBottomY = config::kDisplayHeight - kLifeTopY - kLifeRegionH;  // 248

// ============================================================
// フォントサイズ
// ============================================================
// M5GFX のデフォルトフォント (6x8 base) にスケールを掛ける。
// textSize=5 で 1 文字約 30x40px、3 桁 "999" で約 90px 幅
// → 180px 幅の領域に十分収まる。

constexpr float kLifeFontSize    = 5.0f;  // メインのライフ数字
constexpr float kDeltaFontSize   = 3.0f;  // 差分表示 (+/-N)
constexpr float kWarningFontSize = 2.5f;  // 警告 "!" マーク

}  // namespace counter::ui::theme
