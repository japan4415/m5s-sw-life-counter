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

// --- 色 ---
// 鍵アイコンの形状で状態を伝え、色だけに頼らない
// (docs/05-ui-ux.md の色覚差対応方針)
constexpr uint16_t kLockIconColor   = 0xC618;  // 明るめグレー（鍵アイコン・テキスト）
constexpr uint16_t kRingLockedColor = 0x0841;  // 極暗グレー（ロック時リング）
    // kRingDimColor と同値だが、意味が異なるため独立定義する。
    // ロック時の暗転度合いを将来変更する場合にここだけ修正すればよい。

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
// 共通 UI 要素
// ============================================================

// 操作説明・ヒントテキスト用の色。すべての画面で共通に使う。
constexpr uint16_t kHintTextColor = 0x8410;  // グレー

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
constexpr float kSetupLifeFontSize   = 7.0f;  // ライフ数字
constexpr float kSetupPresetFontSize = 1.5f;  // プリセット "20" "40"
constexpr float kSetupHintFontSize   = 1.5f;  // 操作説明（通常行）
constexpr float kSetupStartFontSize  = 2.0f;  // "Hold B to START"（強調）

// 色
constexpr uint16_t kSetupPresetActiveColor   = 0x07FF;  // シアン（一致プリセット）
constexpr uint16_t kSetupPresetInactiveColor = 0x4208;  // ダークグレー（不一致）
constexpr uint16_t kSetupStartColor          = 0x07FF;  // シアン（START 強調）

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

// メニュー項目の配置
constexpr int32_t kMenuFirstItemY  = 157;  // 最初の項目の中心 y
constexpr int32_t kMenuItemSpacing = 24;   // 項目間の y 間隔

// 操作説明・確認メッセージの y 座標
constexpr int32_t kMenuConfirmMsgY = 330;  // "Hold B to confirm"
constexpr int32_t kMenuHintY       = 350;  // "A=Next  B=Select  A+B=Close"

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
constexpr float kMenuItemFontSize    = 2.0f;  // メニュー項目名
constexpr float kMenuHintFontSize    = 1.0f;  // 操作説明
constexpr float kMenuConfirmFontSize = 1.5f;  // 確認メッセージ
constexpr float kMenuHoldFontSize    = 2.0f;  // 長押し中 "HOLD" ラベル

// 色
constexpr uint16_t kMenuSelectedColor = 0x07FF;  // シアン（選択中項目）
constexpr uint16_t kMenuNormalColor   = 0x8410;  // グレー（非選択項目）
constexpr uint16_t kMenuConfirmColor  = 0xFB40;  // オレンジ（確認待ち項目・弧）
constexpr uint16_t kMenuArcColor      = 0x07FF;  // シアン（プログレス弧: 通常時）

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

// 色: プレイヤー識別は "TOP"/"BTM" のテキストラベルが主、色は補助。
// オレンジ/シアンの組み合わせは大半の色覚特性で識別可能（色覚差対応）。
constexpr uint16_t kHistoryTitleColor  = 0xFFFF;  // 白
constexpr uint16_t kHistoryTopColor    = 0x07FF;  // シアン（上プレイヤー）
constexpr uint16_t kHistoryBottomColor = 0xFB40;  // オレンジ（下プレイヤー）
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

}  // namespace counter::ui::theme
