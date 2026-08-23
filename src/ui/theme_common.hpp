#pragma once

// FaB (theme.hpp) と EDH (edh_theme.hpp) に同名・同型・同値で存在した
// 定数の安全サブセット。両ヘッダから移設して集約したもの。
// 各ヘッダは namespace 冒頭で本 namespace を using するため、
// theme:: / edh_theme:: 修飾による参照側コードは変更不要である。
//
// 同名でも値が異なる定数 (kFirmwareVersion / kLifeFontSize / kDeltaFontSize /
// kLockRegionW / kLockRegionH / kLockRegionX / kLockRegionY / kLockTextSize /
// kSetupHintY1 / kSetupHintY2 / kSetupHintY3) およびバリアント固有の定数は
// 各ヘッダに残置している。
//
// 注記: kLockIconColor / kMenuConfirmColor / kMenuNormalColor /
// kMenuSelectedColor の行末コメントは EDH 由来文言と異なる（FaB 側正本を採用）。

#include <cstdint>

#include "app_config.hpp"

namespace counter::ui::theme_common {

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

// --- 外周リング ---
constexpr uint16_t kRingNormalColor    = 0x2104;  // ダークグレー（通常）
constexpr uint16_t kRingHighlightColor = 0x4A69;  // 明るいグレー（操作中）
constexpr uint16_t kRingDimColor       = 0x0841;  // 極暗グレー（非操作側）

// --- 中央分割帯 ---
constexpr uint16_t kDividerColor  = 0x2945;  // ダークグレー

// ============================================================
// リング寸法
// ============================================================

// タッチ判定のリング内周と一致させる (config::kRingInnerRadius = 165)
constexpr int32_t kRingInnerR = static_cast<int32_t>(config::kRingInnerRadius);
// 表示円 (半径 234) から 2px 内側に設定し、丸型画面の縁が綺麗に見えるようにする
constexpr int32_t kRingOuterR = 232;

// ============================================================
// タッチロック表示の色
// ============================================================

// 鍵アイコンの形状で状態を伝え、色だけに頼らない
// (docs/05-ui-ux.md の色覚差対応方針)
constexpr uint16_t kLockIconColor   = 0xC618;  // 明るめグレー（鍵アイコン・テキスト）
constexpr uint16_t kRingLockedColor = 0x0841;  // 極暗グレー（ロック時リング）
    // kRingDimColor と同値だが、意味が異なるため独立定義する。
    // ロック時の暗転度合いを将来変更する場合にここだけ修正すればよい。

// ============================================================
// 共通 UI 要素
// ============================================================

// 操作説明・ヒントテキスト用の色。すべての画面で共通に使う。
constexpr uint16_t kHintTextColor = 0x8410;  // グレー

// ============================================================
// セットアップ画面（共通項目）
// ============================================================

// フォント
constexpr float kSetupLifeFontSize   = 7.0f;  // ライフ数字
constexpr float kSetupHintFontSize   = 1.5f;  // 操作説明（通常行）
constexpr float kSetupStartFontSize  = 2.0f;  // "Hold B to START"（強調）

// 色
constexpr uint16_t kSetupStartColor          = 0x07FF;  // シアン（START 強調）

// ============================================================
// メニュー画面（共通項目）
// ============================================================

// メニュー項目の配置
constexpr int32_t kMenuFirstItemY  = 157;  // 最初の項目の中心 y
constexpr int32_t kMenuItemSpacing = 24;   // 項目間の y 間隔

// 操作説明・確認メッセージの y 座標
constexpr int32_t kMenuConfirmMsgY = 330;  // "Hold B to confirm"
constexpr int32_t kMenuHintY       = 350;  // "A=Next  B=Select  A+B=Close"

// フォント
constexpr float kMenuItemFontSize    = 2.0f;  // メニュー項目名
constexpr float kMenuHintFontSize    = 1.0f;  // 操作説明
constexpr float kMenuConfirmFontSize = 1.5f;  // 確認メッセージ

// 色
constexpr uint16_t kMenuSelectedColor = 0x07FF;  // シアン（選択中項目）
constexpr uint16_t kMenuNormalColor   = 0x8410;  // グレー（非選択項目）
constexpr uint16_t kMenuConfirmColor  = 0xFB40;  // オレンジ（確認待ち項目・弧）

// ============================================================
// バッテリー表示（メニュー画面）— 電池アイコン
// ============================================================
// M5GFX の標準フォントは絵文字に非対応のため、電池アイコンを図形で描く。
// 鍵アイコン (drawLockIcon()) と同じ図形描画方針。
//
// メニュー項目の先頭 (y=157) の上方に配置する。
// 中心 (234) からの距離 = |234 - 138| = 96 < 165 → リング内周の内側に収まる。
// 長押しテキスト (y=39) とは 99px の距離があり十分離れている。
//
// レイアウト: [!] [電池アイコン] [間隔] [87%]（"!" は 20% 以下警告時のみ）
// アイコン＋数値の合計幅は最大約 81px (警告 "!" 付き "100%") で、
// y=138 で使える横幅 268px (2*sqrt(165^2 - 96^2)) に十分収まる。
//
// 衝突チェック:
//   アイコン上端: y = 138 - 7 = 131。長押しテキスト下端 y≈47 との隙間 84px。
//   アイコン下端: y = 138 + 7 = 145。先頭項目上端 y = 157 - 8 = 149 との隙間 4px。

constexpr int32_t kBatteryY = 138;  // バッテリー表示の中心 y

// --- 電池アイコンの寸法 ---
constexpr int32_t kBatteryIconBodyW = 26;  // 本体矩形の幅 (px)
constexpr int32_t kBatteryIconBodyH = 14;  // 本体矩形の高さ (px)
constexpr int32_t kBatteryIconTermW = 3;   // 端子（右端突起）の幅 (px)
constexpr int32_t kBatteryIconTermH = 7;   // 端子の高さ (px)
constexpr int32_t kBatteryIconPad   = 2;   // 枠線と残量バーの間隔 (px)
constexpr int32_t kBatteryIconGap   = 4;   // アイコンとパーセント数値の間隔 (px)

// --- 枠線の太さ ---
// 警告時に太くすることで、色だけでなく形の変化でも低残量を伝える
// (docs/05-ui-ux.md の色覚差対応方針)。
// 残量バーが短いこと自体が形の情報になるが、枠線太さの変化で補強する。
constexpr int32_t kBatteryBorderNormal  = 1;  // 通常時
constexpr int32_t kBatteryBorderWarning = 2;  // 20% 以下の警告時

// --- フォント ---
constexpr float kBatteryPercentFontSize  = 1.5f;  // パーセント数値
constexpr float kBatteryWarnMarkFontSize = 1.5f;  // 警告 "!" マーク

// --- 色 ---
// 通常はグレーで控えめに、20% 以下でオレンジに切り替えて警告する。
// 色だけに頼らず "!" 記号・太枠線・残量バーの短さを併用する
// (docs/05-ui-ux.md 色覚差対応方針)。
constexpr uint16_t kBatteryNormalColor  = 0x8410;  // グレー（通常）
constexpr uint16_t kBatteryWarningColor = 0xFB40;  // オレンジ（20% 以下の警告）
// 充電中の稲妻マーク。白色で残量バーの上に描画し、
// 背景 (黒) と残量バー (グレー/オレンジ) のどちらの上でも視認できるようにする。
constexpr uint16_t kBatteryChargeBoltColor = 0xFFFF;  // 白

// --- しきい値 ---
// docs/09-power-and-tournament.md: 20% 以下で警告、10% 以下で定期警告振動。
// 振動はこの作業では実装しない。表示上の警告しきい値のみ定義する。
constexpr uint8_t kBatteryWarningThreshold = 20;

// ============================================================
// 長押し進捗表示（画面共通）
// ============================================================
//
// すべての画面で使える部分再描画の長押し進捗弧。
// 外周リング (r=165..232) 内の r=185..205 に配置する。
//
// 配置の根拠:
//   画面上の非衝突領域を全探索した結果、リング内が唯一の実用的な配置位置。
//   - 外縁 r=232..234 は 2px 幅で視認不能
//   - コンテンツ領域 (r<165) はライフ数字・メニュー項目・ヒントと衝突
//   リング領域と一時的に重なるが、percent=0 で元のリング表示を復元する。
//
// 転送サイズ推定:
//   ring band r=185..205 のピクセル数 ≈ pi(205^2 - 185^2) ≈ 24,504 px
//   転送量 ≈ 24,504 x 2 = 49,008 bytes ≈ 5 ms (実測比例推定)
//   43 ms の描画予算に十分余裕がある。

constexpr int32_t  kHoldArcInnerR     = 185;
constexpr int32_t  kHoldArcOuterR     = 205;
constexpr uint16_t kHoldArcTrackColor = 0x4208;  // ダークグレー（進捗背景トラック）
    // 旧値 0x1082 が AMOLED 黒と区別困難だったため 0x4208 (RGB 66,66,66) に設定。
    // kMenuArcTrackColor と同値だが、メニュー専用とは独立の定義。
constexpr uint16_t kHoldArcColor      = 0x07FF;  // シアン（進捗弧）

// ============================================================
// 履歴画面
// ============================================================

constexpr int32_t kHistoryTitleY     = 130;  // "HISTORY" タイトル
constexpr int32_t kHistoryFirstItemY = 160;  // 最初の履歴項目の中心 y
constexpr int32_t kHistorySpacing    = 18;   // 項目間の y 間隔
constexpr int32_t kHistoryFooterY    = 350;  // "B: Back"
constexpr size_t  kHistoryMaxVisible = 8;    // 画面に収まる最大件数

constexpr float kHistoryTitleFontSize  = 2.0f;
constexpr float kHistoryItemFontSize   = 1.5f;
constexpr float kHistoryFooterFontSize = 1.0f;

constexpr uint16_t kHistoryTitleColor  = 0xFFFF;  // 白
constexpr uint16_t kHistoryEmptyColor  = 0x4208;  // ダークグレー（履歴なし）

// ============================================================
// About 画面
// ============================================================

constexpr int32_t kAboutTitleY   = 200;  // "Life Counter"
constexpr int32_t kAboutVersionY = 240;  // "v1.2.0"
constexpr int32_t kAboutFooterY  = 350;  // "B: Back"

constexpr float kAboutTitleFontSize   = 2.5f;
constexpr float kAboutVersionFontSize = 2.0f;
constexpr float kAboutFooterFontSize  = 1.0f;

constexpr uint16_t kAboutTitleColor   = 0xFFFF;  // 白
constexpr uint16_t kAboutVersionColor = 0x8410;  // グレー

// ============================================================
// 感度設定画面
// ============================================================
// 一周あたりのライフ変動量をプリセットから選択する画面。
// About 画面に近いレイアウトだが、プリセット選択 UI を持つ。
//
// 垂直配置:
//   タイトル     y=175  (中心から 59px 上)
//   数値         y=220  (中心から 14px 上)
//   ラベル       y=255  (中心から 21px 下)
//   プリセット   y=290  (中心から 56px 下)
//   ヒント       y=350  (中心から 116px 下)
//
// 最も遠い要素 (y=350): 距離 116px。
// 半径 165 の円で d=116 のとき利用可能横幅 = 2*sqrt(165^2 - 116^2) ≈ 234px。
// "A: Change  B: OK" ≈ 18 chars * 6px = 108px < 234px → 収まる。

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

constexpr uint16_t kSensitivityTitleColor = 0xFFFF;  // 白
constexpr uint16_t kSensitivityValueColor = 0xFFFF;  // 白
constexpr uint16_t kSensitivityLabelColor = 0x8410;  // グレー

}  // namespace counter::ui::theme_common
