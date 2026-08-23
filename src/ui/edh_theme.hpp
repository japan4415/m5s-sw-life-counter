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
#include "ui/theme_common.hpp"

namespace counter::ui::edh_theme {

using namespace counter::ui::theme_common;

// ============================================================
// ファームウェアバージョン（EDH 独自。FaB とは独立管理）
// ============================================================
constexpr const char* kFirmwareVersion = "0.1.0";

// ============================================================
// 色定義 (RGB565)
// ============================================================

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

// --- 分割線 ---
// 4 扇形の境界を示す対角線。視覚的に明示しつつ控えめな色にする。
constexpr int32_t  kDividerWidth  = 2;       // 線の太さ (px)

// ============================================================
// 画面・レイアウト定数
// ============================================================

// 円形画面パラメータ（app_config.hpp の値を再参照）
constexpr int32_t kScreenRadius = 234;  // = kDisplayWidth / 2

// ============================================================
// 非重複キャンバス方式（実機調整前提）
// ============================================================
//
// 【設計】
// 旧方式 (234x234 の重なるキャンバス) は描画順の上書きで内容が消える問題
// があった。新方式では小さなキャンバスを互いに重ならない位置に配置する。
//
// 【キャンバスサイズの決定】
// ライフビュー: ラベル(size1.0≈12x8)+数字(size4.0≈72x32)+差分(size2.0≈48x16)
// CMD ダメージ: タイトル(8px)+3行(size1.5+2.0, 各16px)+ヒント(8px)≈80px
// → 100x80 で両方収まる。
//
// 【配置計算】
// 配置半径 d=105: 画面中心から各方向に 105px 離した位置にキャンバス中心を置く。
// キャンバス物理サイズ 100x80 の左上座標:
//   P1(上): (234-50, 234-105-40) = (184, 89)  右下=(284, 169)
//   P2(右): (234+105-50, 234-40) = (289, 194) 右下=(389, 274)
//   P3(下): (234-50, 234+105-40) = (184, 299) 右下=(284, 379)
//   P4(左): (234-105-50, 234-40) = (79, 194)  右下=(179, 274)
//
// 【非重複の検証】
//   P1 vs P2: P1右端 284 < P2左端 289 → 間隔 5px ✓
//   P1 vs P4: P4右端 179 < P1左端 184 → 間隔 5px ✓
//   P2 vs P3: P2下端 274 < P3上端 299 → 間隔 25px ✓
//   P3 vs P4: P4右端 179 < P3左端 184 → 間隔 5px ✓
//
// 【外周リング (半径165) との干渉チェック】
// 各キャンバスの角の画面中心からの距離:
//   P1角(184,89): sqrt(50^2+145^2)=sqrt(23525)≈153 < 165 ✓
//   P1角(284,89): sqrt(50^2+145^2)≈153 < 165 ✓
//   P2角(389,274): sqrt(155^2+40^2)=sqrt(25625)≈160 < 165 ✓
//   P2角(389,194): sqrt(155^2+40^2)≈160 < 165 ✓
//   全角が外周リング内。スライド操作と干渉しない。
//
// 【画面円 (半径234) の範囲内チェック】
//   最大距離 ≈ 160 < 234 ✓ 全角が画面内。

/// キャンバスの物理的幅 (px)。実機調整前提。
constexpr int32_t kSectorCanvasW = 100;

/// キャンバスの物理的高さ (px)。実機調整前提。
constexpr int32_t kSectorCanvasH = 80;

/// 配置半径 (px)。画面中心から各プレイヤー方向へこの距離離した位置に
/// キャンバスの中心を合わせる。実機調整前提。
constexpr int32_t kSectorPlacementR = 105;

// ============================================================
// ライフビューのレイアウト（キャンバス内座標、実機調整前提）
// ============================================================
// キャンバス論理座標の中心 (cw/2, ch/2) を基準にオフセットで配置する。
// 回転はキャンバス単位で適用されるため、論理座標で統一的に記述できる。

/// プレイヤーラベルの y オフセット（中心から上方向）。
constexpr int32_t kLabelOffsetY = -30;

/// ライフ数字の y オフセット（中心）。
constexpr int32_t kLifeOffsetY = 5;

/// 差分表示（+/-N）の y オフセット（中心から下方向）。
constexpr int32_t kDeltaOffsetY = 28;

/// "DEFEATED" テキストの y オフセット（中心から下方向）。
constexpr int32_t kDefeatedOffsetY = 33;

// --- ライフ表示の文字サイズ ---
// 100x80 キャンバスに収まるサイズ。実機調整前提。
constexpr float kLifeFontSize      = 4.0f;   // メインのライフ数字
constexpr float kLifeFontSizeSmall = 3.0f;   // 4 桁以上の場合
constexpr float kDeltaFontSize     = 2.0f;   // 差分表示 (+/-N)
constexpr float kPlayerLabelSize   = 1.0f;   // "P1" 等のラベル

// ============================================================
// 統率者ダメージビューのレイアウト（実機調整前提）
// ============================================================
// 3 対戦相手の一覧を 100x80 キャンバスに収める。
// オフセットはキャンバス中心 (cw/2, ch/2) からの相対値。

/// "CMD DMG" タイトルの y オフセット（中心から上方向）。
constexpr int32_t kCmdDmgTitleOffsetY = -32;

/// 3 対戦相手の一覧の開始 y オフセット（中心から上方向）。
constexpr int32_t kCmdDmgFirstItemOffsetY = -15;

/// 一覧の項目間隔 (px)。3 項目で -15, +3, +21。
constexpr int32_t kCmdDmgItemSpacing = 18;

/// ラベル ("P1" 等) の x オフセット（左端から）。
constexpr int32_t kCmdDmgLabelX = 8;

/// 選択マーカー ">" の x オフセット。
constexpr int32_t kCmdDmgMarkerX = 2;

/// ダメージ数値の x オフセット（右端から）。
constexpr int32_t kCmdDmgValueRightMargin = 4;

/// ヒントテキスト ("Tap opponent") の y オフセット（中心から下方向）。
constexpr int32_t kCmdDmgHintOffsetY = 33;

/// プレビュー差分表示の y オフセット（中心から下方向）。
constexpr int32_t kCmdDmgDeltaOffsetY = 33;

// --- 統率者ダメージビューの文字サイズ ---
constexpr float kCmdDmgFontSize       = 2.0f;  // 統率者ダメージ数字
constexpr float kCmdDmgLabelFontSize  = 1.0f;  // 被弾元ラベル ("P1", "P2" 等)
constexpr float kCmdDmgHintFontSize   = 1.0f;  // "Tap opponent" ヒント

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

// ============================================================
// 回転角（M5Canvas::setRotation() の値）
// ============================================================
// P1(上)=180°→rot 2, P2(右)=270°→rot 3, P3(下)=0°→rot 0, P4(左)=90°→rot 1
constexpr uint8_t kPlayerRotation[4] = {2, 3, 0, 1};

}  // namespace counter::ui::edh_theme
