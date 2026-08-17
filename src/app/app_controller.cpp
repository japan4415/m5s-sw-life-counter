// Phase 1: ライフカウンター本体 -- アプリケーション制御層の実装
//
// このファイルは docs/07-architecture.md の入力パイプラインに従って
// タッチ入力 -> ジェスチャー検出 -> ドメイン更新 -> 描画 の流れを制御する。
//
// Phase 2 で追加予定:
//   - Undo（物理ボタン A）
//   - タッチロック（物理ボタン B）
//   - 初期ライフ設定画面
//   - Rematch / New Game メニュー

#include "app_controller.hpp"

#include <M5Unified.h>

#include "domain/life_service.hpp"

namespace counter::app {

// ============================================================
// 振動パターンの持続時間 (ms)
// docs/05-ui-ux.md の提案値に基づく。
// 通しでの体感評価はまだ行っていないため、実機テスト後に調整しうる。
// 将来的には config に集約する可能性がある。
// ============================================================
namespace {
constexpr uint32_t kVibStartMs    = 30;   // スライド操作開始の合図
constexpr uint32_t kVibConfirmMs  = 40;   // 指を離して確定
constexpr uint32_t kVibRejectMs   = 20;   // 開始禁止領域の警告（最短パルス）
constexpr uint32_t kVibLifeZeroMs = 120;  // ライフ 0 到達の強い振動
}  // namespace

void AppController::begin() {
    renderer_.begin();
    haptics_.begin();

    // FaB の Classic Constructed を想定した暫定値 (40 ライフ)。
    // 開始ライフの選択画面は Phase 2 の範囲なので、ここでは固定値で初期化する。
    domain::startMatch(state_, 40, 40);

    renderer_.drawAll(state_);
}

void AppController::update(uint32_t nowMs) {
    // ================================================================
    // 1. タッチの取得と GestureDetector への受け渡し
    //    docs/07 入力パイプライン: タッチ座標 -> GestureDetector
    // ================================================================
    const auto touchCount = M5.Touch.getCount();
    bool touching = false;
    int16_t x = 0;
    int16_t y = 0;

    if (touchCount > 0) {
        const auto detail = M5.Touch.getDetail(0);
        touching = detail.isPressed();
        if (touching) {
            x = detail.x;
            y = detail.y;
        }
    }

    // onTouchUp の結果を保持する変数。立ち下がり検出時にのみ設定される。
    input::GestureResult result{};

    // --- 立ち上がり検出（押した瞬間）---
    // 前フレームで非押下、今フレームで押下 = 立ち上がりエッジ
    if (touching && !prevTouching_) {
        gesture_.onTouchDown(x, y, nowMs);

        // 有効な開始（Candidate に遷移した場合のみ）で開始の振動を鳴らす。
        // 外周リング外のタッチや禁止領域のタッチでは振動しない。
        if (gesture_.state() == input::GestureState::Candidate) {
            haptics_.beginGesture();
            haptics_.pulse(kVibStartMs);
        }

        prevTouchX_ = x;
        prevTouchY_ = y;
    }
    // --- 押している間で座標が変化 ---
    // 座標が変化していないときは onTouchMove を呼ばない。
    // GestureDetector 内部の角速度チェックに無意味なサンプルを渡さないため。
    else if (touching && prevTouching_) {
        if (x != prevTouchX_ || y != prevTouchY_) {
            gesture_.onTouchMove(x, y, nowMs);
            prevTouchX_ = x;
            prevTouchY_ = y;
        }
    }
    // --- 立ち下がり検出（離した瞬間）---
    // 前フレームで押下、今フレームで非押下 = 立ち下がりエッジ
    else if (!touching && prevTouching_) {
        result = gesture_.onTouchUp(nowMs);
    }

    prevTouching_ = touching;

    // ================================================================
    // 2. 開始拒否の扱い
    //    docs/05: 開始禁止領域に触れた場合は最短パルス (20ms) で警告する。
    //    「弱い振動」は強度では実現不可（255 以外は体感できない）のため
    //    最短時間で他パターンと区別する。
    // ================================================================
    if (gesture_.consumeRejectedStart()) {
        haptics_.pulse(kVibRejectMs);
    }

    // ================================================================
    // 3. ライフ段階の変化に対する振動
    //    間引きは Haptics 側の責務（docs/07, docs/05）。
    //    AppController は段階変化ごとに無条件で pulseStep を呼ぶ。
    // ================================================================
    if (gesture_.consumeStepChanged()) {
        haptics_.pulseStep();
    }

    // ================================================================
    // 4. プレビューの描画（変化したときだけ）
    //    部分再描画でも約 5.0 ms かかるため（docs/07 実測）、
    //    前フレームと比較して変化があるときだけ描画する。
    // ================================================================
    const auto currentPreview = gesture_.preview();
    const auto currentState   = gesture_.state();

    // 確定フローでは step 5 で描画するため、ここでのプレビュークリアをスキップする。
    // そうしないと、古い state で一瞬描画した直後に確定後の state で再描画してしまう。
    const bool willCommit = result.committed;

    // プレビュー値が変化したら部分再描画する
    if (currentPreview.active    != prevPreview_.active ||
        currentPreview.player    != prevPreview_.player ||
        currentPreview.deltaLife != prevPreview_.deltaLife) {

        if (currentPreview.active) {
            // プレビュー表示中: 現在のプレビュー値を描画
            renderer_.drawLife(state_, currentPreview.player,
                              currentPreview.deltaLife);
        } else if (prevPreview_.active && !willCommit) {
            // プレビューが終了した（キャンセルなど）ので元のライフ値を描画する。
            // 確定時は step 5 で処理するのでここでは描画しない。
            renderer_.drawLife(state_, prevPreview_.player, 0);
        }
    }

    // リングハイライトの更新:
    // Active に遷移した瞬間に点灯し、Active から離れた瞬間に消灯する。
    if (currentState == input::GestureState::Active &&
        prevGestureState_ != input::GestureState::Active) {
        renderer_.drawRingHighlight(currentPreview.player, true);
    } else if (currentState != input::GestureState::Active &&
               prevGestureState_ == input::GestureState::Active) {
        // 消灯時は前フレームのプレイヤーを参照する。
        // 今フレームでは Idle/Cancelled に遷移しており、
        // currentPreview からプレイヤーを取るのは不正確な可能性があるため。
        renderer_.drawRingHighlight(prevPreview_.player, false);
    }

    prevPreview_      = currentPreview;
    prevGestureState_ = currentState;

    // ================================================================
    // 5. 確定
    //    指を離して committed == true なら、ドメインにライフ変更を適用し、
    //    確定の振動を鳴らして確定値を描画する。
    // ================================================================
    if (willCommit) {
        domain::applyLifeChange(state_, result.player, result.deltaLife,
                                nowMs);

        // ライフ 0 に到達した場合はより長い振動で警告する (docs/05: 120ms)。
        // それ以外は通常の確定振動 (docs/05: 40ms)。
        const auto& ps = state_.players[domain::toIndex(result.player)];
        if (ps.life == 0) {
            haptics_.pulse(kVibLifeZeroMs);
        } else {
            haptics_.pulse(kVibConfirmMs);
        }

        // 確定後のライフ値を描画する。
        // previewDelta = 0 でプレビューなしの確定表示を行う。
        renderer_.drawLife(state_, result.player, 0);
    }

    // ================================================================
    // 6. Haptics::tick を毎回呼ぶ
    //    振動の停止タイミング管理はここでしか行われない (docs/07)。
    //    tick を呼ばないとモーターが回りっぱなしになる。
    // ================================================================
    haptics_.tick(nowMs);

    // ================================================================
    // 物理ボタン: Phase 1 では処理しない。シリアルにログを出すのみ。
    // Phase 2 で Undo (BtnA)、タッチロック (BtnB) を実装予定。
    // ================================================================
    if (M5.BtnA.wasPressed()) {
        Serial.println("[Phase1] BtnA pressed (Undo is Phase 2)");
    }
    if (M5.BtnB.wasPressed()) {
        Serial.println("[Phase1] BtnB pressed (Touch Lock is Phase 2)");
    }
}

}  // namespace counter::app
