#pragma once

// EDH（統率者戦）ファームウェアの色・寸法定義。
//
// 既存 theme.hpp（FaB 版）と同じ命名規約・コメントスタイルに従う。
// FaB 版とは独立した namespace で定義し、相互に干渉しない。
//
// 色覚差に依存しない方針 (docs/05-ui-ux.md) に従い、
// 色だけで情報を伝えず、テキストラベル・枠線・位置を併用する。

#include <cstdint>

#include "app_config.hpp"

namespace counter::ui::edh_theme {

// ============================================================
// ファームウェアバージョン（EDH 独自。FaB とは独立管理）
// ============================================================
constexpr const char* kFirmwareVersion = "0.1.0";

// ============================================================
// 色定義 (RGB565)
// ============================================================

// --- 背景 ---
constexpr uint16_t kBgColor = 0x0000;  // 黒

// --- 4 プレイヤーのテーマカラー ---
// 色覚差対応: 輝度差が大きく、主要な色覚特性 (1 型・2 型・3 型) で
// 4 色を区別できる組み合わせを選定。さらにプレイヤー位置（上右下左）の
// テキストラベルで冗長に識別を補助する。
// 色は実機調整前提。AMOLED での見え方を確認して修正しうる。
constexpr uint16_t kPlayerColor[4] = {
    0x07FF,  // P1 (上): シアン
    0xFBE0,  // P2 (右): オレンジ
    0x07E0,  // P3 (下): グリーン
    0xF81F,  // P4 (左): マゼンタ
};

// --- ライフ数字 ---
constexpr uint16_t kLifeColor        = 0xFFFF;  // 白（通常時）
constexpr uint16_t kPreviewLifeColor = 0xBDF7;  // 明るいグレー（プレビュー中）

// --- 差分表示 ---
constexpr uint16_t kDeltaDecreaseColor = 0xFB40;  // 暖色オレンジ（減少）
constexpr uint16_t kDeltaIncreaseColor = 0x07FF;  // 寒色シアン（増加）

// --- 敗北表示 ---
// ライフ 0 以下または統率者ダメージ 21 以上の敗北を示すグレーアウト。
// 色だけでなく "DEFEATED" テキストを併用する（色覚差対応）。
constexpr uint16_t kDefeatedBgColor   = 0x2104;  // ダークグレー（扇形背景）
constexpr uint16_t kDefeatedTextColor = 0x8410;  // グレー（数字色。敗北時に白→グレー）

// --- 統率者ダメージ警告 ---
// 21 以上の統率者ダメージは敗北条件。色 + 太字相当 + テキストで強調する。
constexpr uint16_t kCmdDmgWarningColor = 0xFB40;  // オレンジ（21 以上）
// 選択中の被弾元ハイライト
constexpr uint16_t kCmdDmgSelectedColor = 0xFFFF;  // 白（選択中）
constexpr uint16_t kCmdDmgNormalColor   = 0x8410;  // グレー（通常）

// --- 外周リング ---
constexpr uint16_t kRingNormalColor    = 0x2104;  // ダークグレー（通常）
constexpr uint16_t kRingHighlightColor = 0x4A69;  // 明るいグレー（操作中）
constexpr uint16_t kRingDimColor       = 0x0841;  // 極暗グレー（非操作側）

// --- タッチロック ---
constexpr uint16_t kLockIconColor   = 0xC618;  // 明るめグレー
constexpr uint16_t kRingLockedColor = 0x0841;  // 極暗グレー（ロック時リング）

// --- 共通 UI ---
constexpr uint16_t kHintTextColor = 0x8410;  // グレー

// --- 分割線 ---
// 4 扇形の境界を示す対角線。視覚的に明示しつつ控えめな色にする。
constexpr uint16_t kDividerColor  = 0x2945;  // ダークグレー
constexpr int32_t  kDividerWidth  = 2;       // 線の太さ (px)

// ============================================================
// 画面・レイアウト定数
// ============================================================

// 円形画面パラメータ（app_config.hpp の値を再参照）
constexpr int32_t kScreenRadius = 234;  // = kDisplayWidth / 2

// --- 扇形の描画領域 ---
// 各扇形は画面中心から対角方向に広がる 90 度の領域。
// 描画は矩形キャンバスで近似し、pushSprite で転送する。
// 【近似方式と制約】
// 扇形を正確にクリッピングするのはコスト高なため、矩形キャンバスで近似する。
// 矩形は扇形の外接矩形のサブセットとし、対角線の交差部分はキャンバス間で
// 重なるため、描画順序（P3→P4→P2→P1）で後描画が上書きする。
// この近似により対角線付近で若干のにじみが発生するが、分割線で視覚的に隠す。

// 扇形キャンバスサイズ（実機調整前提の初期値）
// 画面中心からの扇形描画に使う矩形サイズ。
// 中心を含む正方形で、各扇形は半分を実質的に使用する。
constexpr int32_t kSectorCanvasW = 234;  // 画面半径
constexpr int32_t kSectorCanvasH = 234;  // 画面半径

// 各扇形のキャンバス左上座標（画面座標）
// P1(上): 左上(0,0) から右下(468,234) の矩形の上半分
// P2(右): 左上(234,0) から右下(468,468) の右半分
// P3(下): 左上(0,234) から右下(468,468) の下半分
// P4(左): 左上(0,0) から右下(234,468) の左半分
// 注: 実際の描画位置は setRotation + pushSprite で制御する

// --- ライフ表示の文字サイズ ---
// 扇形 1 つあたりの面積は FaB 版の約 1/4。
// 文字サイズは実機での 60-80cm からの視認性を検証して調整する前提。
constexpr float kLifeFontSize      = 5.0f;   // メインのライフ数字（実機調整前提）
constexpr float kLifeFontSizeSmall = 4.0f;   // 4 桁以上の場合
constexpr float kDeltaFontSize     = 2.5f;   // 差分表示 (+/-N)
constexpr float kPlayerLabelSize   = 1.0f;   // "P1" 等のラベル

// --- 統率者ダメージビュー ---
constexpr float kCmdDmgFontSize       = 3.0f;  // 統率者ダメージ数字
constexpr float kCmdDmgLabelFontSize  = 1.5f;  // 被弾元ラベル ("P1", "P2" 等)
constexpr float kCmdDmgHintFontSize   = 1.0f;  // "Select source" ヒント

// ============================================================
// リング寸法
// ============================================================
constexpr int32_t kRingInnerR = static_cast<int32_t>(config::kRingInnerRadius);
constexpr int32_t kRingOuterR = 232;

// ============================================================
// タッチロック表示（画面中央）
// ============================================================
constexpr int32_t kLockRegionW = 60;
constexpr int32_t kLockRegionH = 40;
constexpr int32_t kLockRegionX = (config::kDisplayWidth - kLockRegionW) / 2;
constexpr int32_t kLockRegionY = (config::kDisplayHeight - kLockRegionH) / 2;
constexpr float kLockTextSize = 1.5f;

// ============================================================
// セットアップ画面
// ============================================================
constexpr int32_t kSetupLifeY     = 180;  // ライフ数字の中心 y
constexpr int32_t kSetupHintY1    = 240;  // "Ring: +/- Life"
constexpr int32_t kSetupHintY2    = 265;  // "A: Life presets"
constexpr int32_t kSetupHintY3    = 300;  // "Hold B to START"
constexpr float kSetupLifeFontSize  = 7.0f;
constexpr float kSetupHintFontSize  = 1.5f;
constexpr float kSetupStartFontSize = 2.0f;
constexpr uint16_t kSetupStartColor = 0x07FF;  // シアン

// ============================================================
// メニュー画面（FaB と同構成の 6 項目）
// ============================================================
constexpr int32_t kMenuFirstItemY  = 157;
constexpr int32_t kMenuItemSpacing = 24;
constexpr int32_t kMenuConfirmMsgY = 330;
constexpr int32_t kMenuHintY       = 350;

constexpr float kMenuItemFontSize    = 2.0f;
constexpr float kMenuHintFontSize    = 1.0f;
constexpr float kMenuConfirmFontSize = 1.5f;

constexpr uint16_t kMenuSelectedColor = 0x07FF;  // シアン
constexpr uint16_t kMenuNormalColor   = 0x8410;  // グレー
constexpr uint16_t kMenuConfirmColor  = 0xFB40;  // オレンジ

// ============================================================
// 長押し進捗（画面共通）
// ============================================================
constexpr int32_t  kHoldArcInnerR     = 185;
constexpr int32_t  kHoldArcOuterR     = 205;
constexpr uint16_t kHoldArcTrackColor = 0x4208;
constexpr uint16_t kHoldArcColor      = 0x07FF;

// ============================================================
// バッテリー表示（メニュー画面）
// ============================================================
constexpr int32_t kBatteryY = 138;
constexpr int32_t kBatteryIconBodyW = 26;
constexpr int32_t kBatteryIconBodyH = 14;
constexpr int32_t kBatteryIconTermW = 3;
constexpr int32_t kBatteryIconTermH = 7;
constexpr int32_t kBatteryIconPad   = 2;
constexpr int32_t kBatteryIconGap   = 4;
constexpr int32_t kBatteryBorderNormal  = 1;
constexpr int32_t kBatteryBorderWarning = 2;
constexpr float kBatteryPercentFontSize  = 1.5f;
constexpr float kBatteryWarnMarkFontSize = 1.5f;
constexpr uint16_t kBatteryNormalColor  = 0x8410;
constexpr uint16_t kBatteryWarningColor = 0xFB40;
constexpr uint16_t kBatteryChargeBoltColor = 0xFFFF;
constexpr uint8_t  kBatteryWarningThreshold = 20;

// ============================================================
// 履歴画面
// ============================================================
constexpr int32_t kHistoryTitleY     = 130;
constexpr int32_t kHistoryFirstItemY = 160;
constexpr int32_t kHistorySpacing    = 18;
constexpr int32_t kHistoryFooterY    = 350;
constexpr size_t  kHistoryMaxVisible = 8;

constexpr float kHistoryTitleFontSize  = 2.0f;
constexpr float kHistoryItemFontSize   = 1.5f;
constexpr float kHistoryFooterFontSize = 1.0f;

constexpr uint16_t kHistoryTitleColor = 0xFFFF;
constexpr uint16_t kHistoryEmptyColor = 0x4208;

// ============================================================
// About 画面
// ============================================================
constexpr int32_t kAboutTitleY   = 200;
constexpr int32_t kAboutVersionY = 240;
constexpr int32_t kAboutFooterY  = 350;

constexpr float kAboutTitleFontSize   = 2.5f;
constexpr float kAboutVersionFontSize = 2.0f;
constexpr float kAboutFooterFontSize  = 1.0f;

constexpr uint16_t kAboutTitleColor   = 0xFFFF;
constexpr uint16_t kAboutVersionColor = 0x8410;

// ============================================================
// 感度設定画面
// ============================================================
constexpr int32_t kSensitivityTitleY   = 175;
constexpr int32_t kSensitivityValueY   = 220;
constexpr int32_t kSensitivityLabelY   = 255;
constexpr int32_t kSensitivityPresetY  = 290;
constexpr int32_t kSensitivityHintY    = 350;

constexpr float kSensitivityTitleFontSize  = 2.0f;
constexpr float kSensitivityValueFontSize  = 5.0f;
constexpr float kSensitivityLabelFontSize  = 1.5f;
constexpr float kSensitivityPresetFontSize = 2.0f;
constexpr float kSensitivityHintFontSize   = 1.0f;

constexpr uint16_t kSensitivityTitleColor = 0xFFFF;
constexpr uint16_t kSensitivityValueColor = 0xFFFF;
constexpr uint16_t kSensitivityLabelColor = 0x8410;

// ============================================================
// 回転角（M5Canvas::setRotation() の値）
// ============================================================
// P1(上)=180°→rot 2, P2(右)=270°→rot 3, P3(下)=0°→rot 0, P4(左)=90°→rot 1
constexpr uint8_t kPlayerRotation[4] = {2, 3, 0, 1};

}  // namespace counter::ui::edh_theme
