#pragma once

// 描画フレームワーク層。
//
// FaB (renderer.cpp) と EDH (edh_renderer.cpp) の両描画層で
// 同一形式だった全画面描画の定型処理を集約した非メンバ関数群。
// テーマ定数 (theme.hpp / edh_theme.hpp / theme_common.hpp) には依存せず、
// 背景色などバリアント固有の値はすべて引数で受け取る。
//
// beginFullScreenDraw() が lastHoldPercent を 0 にリセットする理由:
//   全画面転送で長押し進捗弧は消えるため、次回 drawHoldProgress() は
//   トラックから描き直す必要がある。読み出しは drawHoldProgress() のみ
//   なので、リセット位置が prologue 内でも挙動は変わらない。

#include <M5Unified.h>

namespace counter::ui {

/// 描画ターゲットを選択する。
///
/// canvasReady が true なら PSRAM 上の全画面 Canvas へ、false なら
/// ディスプレイへ直接描画する（フォールバック）。
inline LovyanGFX* selectDrawTarget(bool canvasReady, M5Canvas& canvas) {
    return canvasReady
        ? static_cast<LovyanGFX*>(&canvas)
        : static_cast<LovyanGFX*>(&M5.Display);
}

/// 全画面描画の prologue: 背景塗りつぶしと進捗状態のリセットを行う。
///
/// lastHoldPercent は各 Renderer が保持する前回進捗値への参照。
/// 全画面転送で進捗弧が消えるため、次回 drawHoldProgress() は
/// トラックから描き直す必要がある。
inline void beginFullScreenDraw(LovyanGFX* target, uint16_t bgColor,
                                uint8_t& lastHoldPercent) {
    target->fillScreen(bgColor);
    lastHoldPercent = 0;
}

/// 全画面描画の epilogue: 全画面 Canvas をディスプレイへ一括転送する。
///
/// canvasReady が false（Canvas 未確保）のときは何もしない。
/// 各描画メソッド内で target への描画は完了している前提。
inline void endFullScreenDraw(bool canvasReady, M5Canvas& canvas) {
    if (canvasReady) {
        canvas.pushSprite(0, 0);
    }
}

}  // namespace counter::ui
