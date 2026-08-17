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

private:
    M5Canvas canvas_{&M5.Display};      // 全画面バッファ（PSRAM 上に確保）
    M5Canvas lifeCanvas_{&M5.Display};  // 数字領域用テンプキャンバス（SRAM 優先）
    bool canvasReady_ = false;          // 全画面 Canvas の確保に成功したか

    /// 両プレイヤーのリング弧を target に描画する
    void drawRings(LovyanGFX* target, uint16_t topColor, uint16_t bottomColor);

    /// lifeCanvas_ にライフ情報を描画する（転送はしない）。
    /// isTop が true のとき 180 度回転座標系で描画する。
    void renderLifeRegion(uint32_t life, int32_t previewDelta, bool isTop);

    /// ライフ 0 の警告枠線を描画する
    void drawLifeZeroBorder(LovyanGFX* target, int32_t w, int32_t h);
};

}  // namespace counter::ui
