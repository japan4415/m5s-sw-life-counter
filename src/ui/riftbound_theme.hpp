#pragma once

// Riftbound（1v1 得点カウンター）ファームウェアの色・寸法定義。
//
// 既存 theme.hpp（FaB 版）と同じ命名規約・コメントスタイルに従う。
// FaB 版とは独立した namespace で定義し、相互に干渉しない。
//
// 色覚差に依存しない方針 (docs/05-ui-ux.md) に従い、
// 色だけで情報を伝えず、アイコン・枠線・テキスト (+/- 符号) を併用する。

#include <cstdint>

#include "app_config.hpp"
#include "ui/theme_common.hpp"

namespace counter::ui::rift_theme {

using namespace counter::ui::theme_common;

// ============================================================
// ファームウェアバージョン（Riftbound 独自。FaB / EDH とは独立管理）
// ============================================================
constexpr const char* kFirmwareVersion = "0.1.0";

// ============================================================
// 色定義 (RGB565)
// ============================================================

// --- 勝利点到達警告 ---
// 勝利点（Duel = 8）に到達したことを示す。ゴールド（数字色）。
// 勝利点には獲得条件の制約があるため「到達」は目安であり、
// 色だけに頼らず枠線 + "!" テキストを併用する (docs/05-ui-ux.md)。
// FaB 版のライフ 0 警告（オレンジ 0xFB40）と区別するため黄系の色にする。
constexpr uint16_t kScoreTargetColor           = 0xF6A0;  // ゴールド（数字色）
constexpr uint16_t kScoreTargetBorderColor     = 0xF6A0;  // 枠線色
constexpr int32_t  kScoreTargetBorderThickness = 3;       // 枠線太さ (px)

// --- 中央分割帯 ---
constexpr int32_t  kDividerHeight = 2;       // 高さ (px)

// ============================================================
// 得点領域の矩形
// ============================================================
//
// リング内周 (r=165) の円内に全角が収まるようにする。
// 幾何は FaB 版 theme.hpp の数字領域と同一である
// （幅 180、高さ 120 のとき最遠角は中心から約 161.4px < 165）。
// 転送サイズ 180*120*2 = 43,200 bytes で、全画面転送に比べて約 1/10。
// 実測比例推定で約 4.5 ms となり、43 ms の描画予算に十分余裕がある。

constexpr int32_t kScoreRegionW = 180;
constexpr int32_t kScoreRegionH = 120;

// 上側プレイヤー領域（左上角座標）
// 中心は (234, 160) — 画面中心から 74px 上
constexpr int32_t kScoreTopX = (config::kDisplayWidth - kScoreRegionW) / 2;   // 144
constexpr int32_t kScoreTopY = 100;

// 下側プレイヤー領域（左上角座標）
// 中心は (234, 308) — 画面中心から 74px 下（上側と対称）
constexpr int32_t kScoreBottomX = kScoreTopX;                                  // 144
constexpr int32_t kScoreBottomY = config::kDisplayHeight - kScoreTopY - kScoreRegionH;  // 248

// ============================================================
// タッチロック表示
// ============================================================
//
// ロック中に画面中央に表示する鍵アイコンと "LOCK" テキストの定義。
// 幾何は FaB 版と同一（上下得点領域の隙間 28px に収まる設計）。

// --- 矩形 ---
constexpr int32_t kLockRegionW = 48;
constexpr int32_t kLockRegionH = 28;
constexpr int32_t kLockRegionX =
    (config::kDisplayWidth - kLockRegionW) / 2;      // 210
constexpr int32_t kLockRegionY =
    kScoreTopY + kScoreRegionH;                       // 220

// --- フォント ---
constexpr float kLockTextSize = 1.0f;  // "LOCK" ラベル (6x8 base x1.0)

// ============================================================
// フォントサイズ
// ============================================================
// M5GFX のデフォルトフォント (6x8 base) にスケールを掛ける。
// textSize=7 で 1 文字 42x56px、2 桁 "88" で 84px 幅
// → 180px 幅の領域に収まる。3 桁以上でも自動縮小する。

constexpr float kScoreFontSize    = 7.0f;  // メインの得点数字（4 桁以下）
constexpr float kDeltaFontSize    = 3.0f;  // 差分表示 (+/-N)
constexpr float kWarningFontSize  = 2.5f;  // 警告 "!" マーク

// 桁数に応じて得点数字のフォントサイズを自動縮小する。
// 6 * size * numChars <= kScoreRegionW を満たす最大の整数サイズを返し、
// kScoreFontSize を上限とする。
constexpr float scoreFontSizeForWidth(int numChars) {
    return static_cast<float>(
               static_cast<int>(
                   static_cast<float>(kScoreRegionW) /
                   (6.0f * static_cast<float>(numChars))))
           < kScoreFontSize
        ? static_cast<float>(
              static_cast<int>(
                  static_cast<float>(kScoreRegionW) /
                  (6.0f * static_cast<float>(numChars))))
        : kScoreFontSize;
}

// プレビュー表示時の scoreCanvas_ 内描画座標
// フォント 7.0 (高さ 56px) と 3.0 (高さ 24px) が重ならないよう配置する。
//   得点数字: 中心 y=35 → [7, 63]
//   差分テキスト: 中心 y=88 → [76, 100]
//   間隔 13px、上端余白 7px、下端余白 20px
constexpr int32_t kPreviewScoreCY  = 35;  // プレビュー時得点数字の中心 y
constexpr int32_t kPreviewDeltaCY = 88;  // プレビュー時差分テキストの中心 y

// ============================================================
// セットアップ画面
// ============================================================
// 上下プレイヤーの得点（開始時は 0）と操作説明を表示する全画面描画。
// Riftbound の得点は常に 0 から始まるため、FaB 版のような
// 開始ライフ調整・プリセット表示は存在しない。
// scoreCanvas_ (180x120) を上下に配置し、中央 68px の隙間に操作説明を描く。

// scoreCanvas_ の配置 y 座標（左上角）
// 上下プレイヤーが画面中心 (234) から各 94px ずつ等距離になるよう設計。
constexpr int32_t kSetupTopY    = 80;   // 上側: y[80, 200]
constexpr int32_t kSetupBottomY = 268;  // 下側: y[268, 388]

// scoreCanvas_ 内の描画座標
constexpr int32_t kSetupScoreNumCY = 30;  // 得点数字の中心 y
constexpr int32_t kSetupScoreLabelCY = 90;  // "SCORE" ラベルの中心 y

// 操作説明テキストの y 座標（画面座標、中央の隙間 y[200, 268] 内）
constexpr int32_t kSetupHintY1  = 224;  // "Ring: +/- Score"
constexpr int32_t kSetupStartY  = 256;  // "Hold B to START"

// フォント
constexpr float kSetupScoreLabelFontSize = 1.5f;  // "SCORE" ラベル

// 色
constexpr uint16_t kSetupPresetActiveColor   = 0x07FF;  // シアン（選択中プリセット）
constexpr uint16_t kSetupPresetInactiveColor = 0x4208;  // ダークグレー（非選択）
    // 感度設定画面のプリセット一覧で FaB 版と同じ色を使う。

// ============================================================
// メニュー画面
// ============================================================
// 項目は SetLife を除く 6 つ（Resume / History / Sensitivity / Rematch /
// About / Switch Game）。SetLife は得点制の Riftbound では意味を持たないため
// 表示しない（共通 MenuItem enum は 7 値固定。選択対象から外す）。
// 共通の kMenuFirstItemY (157) / kMenuItemSpacing (24) を使うと
// 末尾 y = 157 + 5*24 = 277 となり、6 項目でも円形画面に収まる
// （最遠項目 y=157 の中心距離 77px、半径 165 で利用可能幅約 292px）。

// 長押しプログレスの円弧（FaB 版と同一の幾何）
constexpr int32_t  kMenuArcInnerR     = 185;
constexpr int32_t  kMenuArcOuterR     = 205;
constexpr uint16_t kMenuArcTrackColor = 0x4208;  // ダークグレー（進捗背景トラック）

// フォント
constexpr float kMenuHoldFontSize    = 2.0f;  // 長押し中 "HOLD" ラベル

// 色
constexpr uint16_t kMenuArcColor      = 0x07FF;  // シアン（プログレス弧: 通常時）

// ============================================================
// 履歴画面
// ============================================================

// 色: プレイヤー識別は "TOP"/"BTM" のテキストラベルが主、色は補助。
// オレンジ/シアンの組み合わせは大半の色覚特性で識別可能（色覚差対応）。
constexpr uint16_t kHistoryTopColor    = 0x07FF;  // シアン（上プレイヤー）
constexpr uint16_t kHistoryBottomColor = 0xFB40;  // オレンジ（下プレイヤー）

}  // namespace counter::ui::rift_theme
