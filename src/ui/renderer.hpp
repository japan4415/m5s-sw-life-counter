#pragma once

// 描画層。M5GFX / M5Unified に依存する。
// PSRAM 上の全画面 Canvas をバッファとして使用し、
// 変更のあった部分矩形のみをディスプレイに転送する。
//
// なぜ全画面転送を避けるのか:
//   外周スライド中、ライフ表示は約 43 ms ごとに更新される
//   （感度 10 度/ライフ、スイープ角速度 233 度/秒の実測値に基づく）。
//   全画面 (468x468) 転送は 44.6 ms かかり、この予算を超過する。
//   数字領域 (180x120) の転送は約 4.5 ms で、十分な余裕がある。
//   (Phase 0 Step 8 実測、ADR-15 決定)

#include <M5Unified.h>

#include "app/screen_state.hpp"
#include "domain/life_change.hpp"
#include "domain/match_state.hpp"

namespace counter::ui {

class Renderer {
public:
    /// M5.Display の初期化が完了した後に呼ぶ。
    /// PSRAM 上に全画面 Canvas (468x468, 16bit) を確保する。
    /// 確保に失敗した場合は直接描画にフォールバックし、シリアルに報告する。
    void begin();

    /// 画面全体を描き直す。起動時やリマッチ時など全面再構築が必要なときに使う。
    /// 全画面転送 (44.6 ms) を行うため、スライド中には呼ばないこと。
    void drawAll(const domain::MatchState& state);

    /// 指定プレイヤーの数字領域のみを再描画して部分転送する。
    /// スライド中に毎段階呼ばれる想定。転送は約 4.5 ms で収まる。
    /// previewDelta != 0 のときは確定後の値と差分の両方を表示する。
    void drawLife(const domain::MatchState& state, PlayerId player,
                  int32_t previewDelta);

    /// 操作中のプレイヤー側の外周リングを強調し、非対象側を暗くする。
    /// on=true でハイライト、on=false で通常に戻す。
    /// リング部分のみを更新する（数字領域には触れない）。
    void drawRingHighlight(PlayerId player, bool on);

    /// タッチロック状態の表示を更新する。
    /// state.touchLocked が true のとき中央に鍵アイコンを描画し、
    /// 外周リングを暗転させる。false のとき元の表示に戻す。
    /// 部分再描画のみで全画面転送 (44.6 ms) を回避する。
    void drawLockState(const domain::MatchState& state);

    // --- Phase 2 Wave 2: 画面遷移対応 ---
    // 以下の 4 メソッドは全画面転送 (44.6 ms) を行う。
    // ScreenState::consumeDirty() で変化を検出したときだけ呼ぶ想定。
    // 毎ループ呼ばないこと（ただし drawMenu の長押し進捗は例外。後述）。

    /// セットアップ画面。上下の開始ライフ設定と操作説明を表示する。
    void drawSetup(const app::ScreenState& sc);

    /// ゲームメニュー画面。7 項目の一覧と選択状態を表示する。
    /// batteryPercent: M5.Power.getBatteryLevel() の値 (0〜100)。
    /// charging: M5.Power.isCharging() の値。満充電時に false を返すため
    ///   「USB 未接続」とは断定できない（docs/01 実測）。
    /// 進捗表示は drawHoldProgress() に一本化した。
    void drawMenu(const app::ScreenState& sc,
                  uint8_t batteryPercent, bool charging);

    /// 履歴画面。直近のライフ変更を一覧表示する。
    void drawHistory(const domain::MatchState& state);

    /// About 画面。ファームウェア情報を表示する。
    void drawAbout();

    /// 感度設定画面。現在の感度プリセットと選択肢を表示する。
    void drawSensitivity(const app::ScreenState& sc);

    /// 長押しの進捗を部分再描画で表示する。percent は 0〜100。
    /// 0 を渡すと消去して元の表示に戻す。
    /// どの画面の上にも重ねられるよう、外周リング内 (r=185..205) に
    /// 円弧とテキストを描画する。部分再描画のみで全画面転送を行わない。
    ///
    /// 注意: drawHoldProgress() 呼び出し後に drawMenu() 等の全画面メソッドを
    /// 呼ぶと進捗表示は上書きされる。全画面メソッドは画面全体を再描画するため、
    /// 進捗弧も含めて塗りつぶされる。アプリ層が表示の一貫性を制御する前提。
    void drawHoldProgress(uint8_t percent);

private:
    M5Canvas canvas_{&M5.Display};      // 全画面バッファ（PSRAM 上に確保）
    M5Canvas lifeCanvas_{&M5.Display};  // 数字領域用テンプキャンバス（SRAM 優先）
    bool canvasReady_ = false;          // 全画面 Canvas の確保に成功したか

    // drawHoldProgress(0) で元のリング表示を復元するために
    // 現在のリング色を追跡する。全画面メソッド (drawSetup 等) は
    // リングを描かないため kBgColor に設定する。
    uint16_t ringTopColor_    = 0x0000;  // 背景色 (= theme::kBgColor) で初期化
    uint16_t ringBottomColor_ = 0x0000;  // begin() の前は画面未描画なので黒で正しい

    // drawHoldProgress() の差分描画に使う前回進捗値。
    // 0 は「進捗表示なし」を意味する。全画面メソッド (drawAll 等) が
    // 呼ばれたときに 0 にリセットする。全画面転送で弧が消えるため、
    // 次回の drawHoldProgress() はトラックから描き直す必要がある。
    uint8_t lastHoldPercent_ = 0;

    /// 両プレイヤーのリング弧を target に描画する
    void drawRings(LovyanGFX* target, uint16_t topColor, uint16_t bottomColor);

    /// lifeCanvas_ にライフ情報を描画する（転送はしない）。
    /// isTop が true のとき 180 度回転座標系で描画する。
    void renderLifeRegion(uint32_t life, int32_t previewDelta, bool isTop);

    /// ライフ 0 の警告枠線を描画する
    void drawLifeZeroBorder(LovyanGFX* target, int32_t w, int32_t h);

    /// ロック領域に鍵アイコンと "LOCK" テキストを描画する
    void drawLockIcon(LovyanGFX* target);

    /// ロック領域をクリアし分割帯を復元する
    void clearLockRegion(LovyanGFX* target);

    /// セットアップ画面のライフ領域を lifeCanvas_ に描画する。
    /// ライフ数字とプリセット一致表示を含む。
    void renderSetupLifeRegion(uint32_t life, bool isTop);
};

}  // namespace counter::ui
