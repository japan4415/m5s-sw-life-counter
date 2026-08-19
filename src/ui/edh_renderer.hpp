#pragma once

// EDH（統率者戦）ファームウェアの描画層。
//
// 4 扇形のレイアウト描画を行う。各扇形の内容はプレイヤーの着席方向へ回転して描画する。
// M5GFX / M5Unified に依存する。
//
// 【描画方式と近似】
// 円形画面（468x468）を対角線（X 字）で 4 つの 90 度扇形に分割する。
// 正確な扇形クリッピングは計算コストが高いため、矩形キャンバスで近似する。
// 各扇形を回転済みの矩形キャンバスに描画し、pushSprite で画面上の対応位置に転送する。
// 対角線付近で矩形が重なるが、分割線を上書きして視覚的に隠す。
//
// 【制約】
// - 扇形の角（対角線交差部分）ではプレイヤー領域の端が若干見切れる
// - 回転描画の性能は未検証（全画面 22.4 FPS の制約あり）
// - 部分再描画（変化した扇形のみ更新）で性能を確保する方針

#include <M5Unified.h>

#include "domain/edh_life_change.hpp"
#include "domain/edh_match_state.hpp"
#include "app/edh_screen_state.hpp"

namespace counter::ui {

class EdhRenderer {
public:
    /// M5.Display の初期化が完了した後に呼ぶ。
    /// PSRAM 上に全画面 Canvas を確保する。
    void begin();

    /// 画面全体を描き直す。起動時やリマッチ時など全面再構築が必要なときに使う。
    /// 全画面転送を行うため、スライド中には呼ばないこと。
    void drawAll(const edh::MatchState& state,
                 const edh::app::EdhScreenState& screenState);

    /// 指定プレイヤーの扇形のみを再描画して部分転送する。
    /// スライド中に毎段階呼ばれる想定。
    /// previewDelta != 0 のときは確定後の値と差分の両方を表示する。
    /// isCommanderDmg: true の場合、統率者ダメージのプレビューとして描画する。
    void drawPlayerSector(const edh::MatchState& state,
                          const edh::app::EdhScreenState& screenState,
                          uint8_t playerIndex,
                          int32_t previewDelta,
                          bool isCommanderDmg = false);

    /// タッチロック状態の表示を更新する。
    void drawLockState(const edh::MatchState& state);

    /// セットアップ画面を描画する。
    void drawSetup(const edh::app::EdhScreenState& sc);

    /// メニュー画面を描画する。
    void drawMenu(const edh::app::EdhScreenState& sc,
                  uint8_t batteryPercent, bool charging);

    /// 履歴画面を描画する。
    void drawHistory(const edh::MatchState& state);

    /// About 画面を描画する。
    void drawAbout();

    /// 感度設定画面を描画する。
    void drawSensitivity(const edh::app::EdhScreenState& sc);

    /// 長押しの進捗を部分再描画で表示する。
    void drawHoldProgress(uint8_t percent);

private:
    M5Canvas canvas_{&M5.Display};       // 全画面バッファ（PSRAM 上に確保）
    M5Canvas sectorCanvas_{&M5.Display}; // 扇形描画用テンプキャンバス
    bool canvasReady_ = false;           // 全画面 Canvas の確保に成功したか
    bool sectorReady_ = false;           // 扇形 Canvas の確保に成功したか

    uint8_t lastHoldPercent_ = 0;

    /// 扇形キャンバスにプレイヤーのライフビューを描画する（転送はしない）。
    /// rotation: M5Canvas::setRotation() に渡す値 (0..3)。
    void renderLifeView(const edh::MatchState& state,
                        uint8_t playerIndex,
                        int32_t previewDelta,
                        uint8_t rotation);

    /// 扇形キャンバスに統率者ダメージビューを描画する。
    void renderCmdDamageView(const edh::MatchState& state,
                             const edh::app::EdhScreenState& screenState,
                             uint8_t playerIndex,
                             int32_t previewDelta,
                             uint8_t rotation);

    /// 扇形キャンバスを画面の適切な位置に転送する。
    void pushSectorToScreen(LovyanGFX* target, uint8_t playerIndex);

    /// 4 本の対角分割線を描画する。
    void drawDividers(LovyanGFX* target);

    /// バッテリーアイコンを描画する（メニュー画面用）。
    void drawBatteryIcon(LovyanGFX* target, uint8_t percent, bool charging);

    /// ロック表示を中央に描画する。
    void drawLockIcon(LovyanGFX* target);

    /// ロック領域をクリアする。
    void clearLockRegion(LovyanGFX* target);
};

}  // namespace counter::ui
