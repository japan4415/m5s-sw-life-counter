#pragma once

// 色・寸法の定義を集約する。
//
// 色覚差に依存しない方針 (docs/05-ui-ux.md) に従い、
// 色だけで情報を伝えず、アイコン・枠線・テキスト (+/- 符号) を併用する。

#include <cstdint>

#include "app_config.hpp"
#include "ui/theme_common.hpp"

namespace counter::ui::theme {

using namespace counter::ui::theme_common;

// ============================================================
// 色定義 (RGB565)
// ============================================================

// --- ライフ 0 警告 ---
// 色だけに頼らず枠線 + "!" テキストを併用する (docs/05-ui-ux.md)
constexpr uint16_t kLifeZeroColor           = 0xFB40;  // オレンジ（数字色）
constexpr uint16_t kLifeZeroBorderColor     = 0xFB40;  // 枠線色
constexpr int32_t  kLifeZeroBorderThickness = 3;       // 枠線太さ (px)

// --- 中央分割帯 ---
constexpr int32_t  kDividerHeight = 2;       // 高さ (px)

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
// タッチロック表示
// ============================================================
//
// ロック中に画面中央に表示する鍵アイコンと "LOCK" テキストの定義。
// 上側ライフ領域の下端 (y=220) と下側ライフ領域の上端 (y=248) の
// 隙間 28px にちょうど収まるよう設計してある。
// これにより drawLife() の部分転送とロック表示が互いに干渉せず、
// 再描画順序によって表示が壊れることがない。
//
// 領域中心 (234, 234) から最も遠い角:
//   sqrt(24^2 + 14^2) = sqrt(576 + 196) ≈ 27.8 < 165
// → リング内周の内側に収まり、リング描画とも重ならない。

// --- 矩形 ---
constexpr int32_t kLockRegionW = 48;
constexpr int32_t kLockRegionH = 28;
constexpr int32_t kLockRegionX =
    (config::kDisplayWidth - kLockRegionW) / 2;      // 210
constexpr int32_t kLockRegionY =
    kLifeTopY + kLifeRegionH;                         // 220

// --- フォント ---
constexpr float kLockTextSize = 1.0f;  // "LOCK" ラベル (6x8 base x1.0)

// ============================================================
// フォントサイズ
// ============================================================
// M5GFX のデフォルトフォント (6x8 base) にスケールを掛ける。
// textSize=7 で 1 文字 42x56px、4 桁 "9999" で 168px 幅
// → 180px 幅の領域に収まる。
// 5 桁以上では lifeFontSizeForWidth() で自動縮小する
// （5 桁→6.0、6 桁→5.0 など）。

constexpr float kLifeFontSize    = 7.0f;  // メインのライフ数字（4 桁以下）
constexpr float kDeltaFontSize   = 3.0f;  // 差分表示 (+/-N)
constexpr float kWarningFontSize = 2.5f;  // 警告 "!" マーク

// 桁数に応じてライフ数字のフォントサイズを自動縮小する。
// 6 * size * numChars <= kLifeRegionW を満たす最大の整数サイズを返し、
// kLifeFontSize を上限とする。
constexpr float lifeFontSizeForWidth(int numChars) {
    return static_cast<float>(
               static_cast<int>(
                   static_cast<float>(kLifeRegionW) /
                   (6.0f * static_cast<float>(numChars))))
           < kLifeFontSize
        ? static_cast<float>(
              static_cast<int>(
                  static_cast<float>(kLifeRegionW) /
                  (6.0f * static_cast<float>(numChars))))
        : kLifeFontSize;
}

// プレビュー表示時の lifeCanvas_ 内描画座標
// フォント 7.0 (高さ 56px) と 3.0 (高さ 24px) が重ならないよう配置する。
//   ライフ数字: 中心 y=35 → [7, 63]
//   差分テキスト: 中心 y=88 → [76, 100]
//   間隔 13px、上端余白 7px、下端余白 20px
constexpr int32_t kPreviewLifeCY  = 35;  // プレビュー時ライフ数字の中心 y
constexpr int32_t kPreviewDeltaCY = 88;  // プレビュー時差分テキストの中心 y

// ============================================================
// ファームウェアバージョン
// ============================================================
// drawAbout() で表示する。リリース時にここを更新する。
constexpr const char* kFirmwareVersion = "1.2.0";

// ============================================================
// セットアップ画面
// ============================================================
// 上下プレイヤーの開始ライフと操作説明を表示する全画面描画。
// lifeCanvas_ (180x120) を上下に配置し、中央 68px の隙間に操作説明を描く。

// lifeCanvas_ の配置 y 座標（左上角）
// 上下プレイヤーが画面中心 (234) から各 94px ずつ等距離になるよう設計。
//   上側中心: 80 + 60 = 140 → 中心からの距離 = 94px
//   下側中心: 268 + 60 = 328 → 中心からの距離 = 94px
constexpr int32_t kSetupTopY    = 80;   // 上側: y[80, 200]
constexpr int32_t kSetupBottomY = 268;  // 下側: y[268, 388]

// lifeCanvas_ 内の描画座標
constexpr int32_t kSetupLifeNumCY = 35;  // ライフ数字の中心 y
constexpr int32_t kSetupPresetCY  = 90;  // プリセット表示の中心 y

// 操作説明テキストの y 座標（画面座標、中央の隙間 y[200, 268] 内）
constexpr int32_t kSetupHintY1 = 212;  // "Ring: +/- Life"
constexpr int32_t kSetupHintY2 = 234;  // "A: 20/40 toggle"
constexpr int32_t kSetupHintY3 = 256;  // "Hold B to START"

// フォント
constexpr float kSetupPresetFontSize = 1.5f;  // プリセット "20" "40"

// 色
constexpr uint16_t kSetupPresetActiveColor   = 0x07FF;  // シアン（一致プリセット）
constexpr uint16_t kSetupPresetInactiveColor = 0x4208;  // ダークグレー（不一致）

// ============================================================
// メニュー画面
// ============================================================
// 6 項目を画面中央に縦並べし、選択中の項目を強調する。
// 長押し確認時は外周に円弧プログレスを表示する。
// 画面中心 (234) を基準に上寄り配置:
//   先頭 y=157 (距離 77)、末尾 y=277 (距離 43)。
//
// なぜ 24px 間隔か:
//   6 項目 × 24px 間隔で末尾 y = 157 + 5*24 = 277。
//   確認メッセージ (y=330) との隙間 53px を十分に確保できる。
//   ADR-22 で 8 項目時に 22px へ詰めた経緯があるが、Sleep 廃止 (ADR-24)・
//   New Game 統合 (ADR-25, #15)・Swap Sides 削除 (ADR-26, #16) を経て
//   5 項目になり 24px を維持。#38 で Sensitivity を追加し 6 項目。
//
// 円形画面への収まり検証 (半径 165 の内側に全項目が収まること):
//   最も中心から遠い項目 (y=157) の中心距離 = 77px。
//   半径 165 の円で d=77 のとき利用可能横幅 = 2*sqrt(165^2 - 77^2) ≈ 292px。
//   最長項目 "> Sensitivity" ≈ 13文字 * 12px = 156px < 292px → 収まる。
//
// 衝突チェック:
//   バッテリー (y=138, icon h=14px): 下端 y=145。先頭上端 y=149。隙間 4px。
//   末尾 (y=277, textSize=2.0, h=16px): 下端 y=285。確認 (y=330) 上端 y=324。隙間 39px。

// 長押しプログレスの円弧
// 外周リング (r=165..232) 内で、丸型 AMOLED の可視領域 (半径 234) から
// 十分な余裕を持たせた位置に配置する。
// Phase 0 実測では表示は半径 234 まで有効だが、円形ディスプレイの
// 外縁付近は輝度低下が起きうるため内側に寄せる。
// テキスト領域 (最遠 r≈141) とは 44px 以上の隙間がある。
constexpr int32_t  kMenuArcInnerR     = 185;
constexpr int32_t  kMenuArcOuterR     = 205;
constexpr uint16_t kMenuArcTrackColor = 0x4208;  // ダークグレー（進捗背景トラック）
    // 旧値 0x1082 (RGB≈16,16,16) は AMOLED の黒背景と区別困難だったため、
    // 0x4208 (RGB≈66,66,66) に変更。進捗弧 (シアン/オレンジ) との
    // 輝度差は十分に保たれる。

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

}  // namespace counter::ui::theme
