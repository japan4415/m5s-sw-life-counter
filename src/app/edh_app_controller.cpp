// EDH（統率者戦）ファームウェアのアプリケーション制御層の実装。
//
// FaB 版 app_controller.cpp と同じ入力パイプラインを踏襲する:
//   ボタン入力 -> タッチ入力 -> ジェスチャー検出 -> ドメイン更新 -> 描画
//
// EDH 固有の追加制御:
//   - selectSector() で 4 扇形のタッチ判定
//   - isOnOuterRing() / isInInnerZone() で半径方向の区分
//   - 内側タップでビュー切替（LifeView <-> CmdDamageView）
//   - 統率者ダメージビュー中の被弾元選択とダメージ操作
//   - checkTimeout() で無操作 10 秒後のライフビュー復帰

#include "edh_app_controller.hpp"

#include <M5Unified.h>

#include "app_config.hpp"
#include "domain/edh_life_change.hpp"
#include "domain/edh_match_state.hpp"
#include "domain/edh_life_service.hpp"
#include "app/edh_screen_state.hpp"
#include "input/edh_touch_zone.hpp"

namespace counter::app {

// ============================================================
// 振動パターンの持続時間 (ms)
// FaB 版と同じパターンを踏襲する。
// ============================================================
namespace {
constexpr uint32_t kVibStartMs    = 30;   // スライド操作開始の合図
constexpr uint32_t kVibConfirmMs  = 40;   // 指を離して確定
constexpr uint32_t kVibRejectMs   = 20;   // 開始禁止領域の警告
constexpr uint32_t kVibLifeZeroMs = 120;  // ライフ 0 到達の強い振動
constexpr uint32_t kVibCmdDmg21Ms = 120;  // 統率者ダメージ 21 到達の強い振動

constexpr uint32_t kVibUndoSuccessMs = 40;
constexpr uint32_t kVibUndoFailMs    = 20;
constexpr uint32_t kVibLockMs        = 80;
constexpr uint32_t kVibUnlockMs      = 40;
constexpr uint32_t kVibLockTouchMs   = 20;

constexpr uint32_t kVibTapMs         = 20;  // 内側タップのフィードバック
}  // namespace

// ============================================================
// begin — 初期化
// ============================================================

void EdhAppController::begin() {
    renderer_.begin();
    haptics_.begin();
    storage_.begin();

    // NVS から感度設定を復元する
    {
        const uint8_t sensIdx = storage_.loadedSensitivity();
        screenState_.setSensitivityIndex(sensIdx);
        gesture_.setDegreesPerLife(
            config::degreesPerLifeFromPreset(sensIdx));
        // setSensitivityIndex が dirty フラグを立てるため消費する。
        screenState_.consumeDirty();
    }

    // NVS に有効な試合状態があり、かつ試合が進行中の場合は復元する
    if (storage_.hasValidState() && storage_.loadedState().active) {
        state_ = storage_.loadedState();
        screenState_.enterActive();
        renderer_.drawAll(state_, screenState_);
        renderer_.drawLockState(state_);
        screenState_.consumeDirty();
        return;
    }

    // 初回起動: Setup 画面から開始する
    screenState_.reset();
    renderer_.drawSetup(screenState_);
}

// ============================================================
// update — メインループ
// ============================================================

void EdhAppController::update(uint32_t nowMs) {
    // ================================================================
    // 0. 物理ボタンの処理
    // ================================================================
    const bool aPressed = M5.BtnA.isPressed();
    const bool bPressed = M5.BtnB.isPressed();
    const auto buttonEvent = buttonInput_.update(aPressed, bPressed, nowMs);

    if (buttonEvent != input::ButtonEvent::None) {
        handleButtonEvent(buttonEvent, nowMs);
    }

    const auto currentScreen = screenState_.screen();

    // ================================================================
    // 0.5. 無操作タイムアウトの確認（毎ループ）
    // ================================================================
    if (currentScreen == edh::app::Screen::Active) {
        screenState_.checkTimeout(nowMs);
    }

    // ================================================================
    // 1. タッチの取得
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

    input::GestureResult result{};

    // ================================================================
    // 2. 画面ごとのタッチ振り分け
    // ================================================================
    if (currentScreen == edh::app::Screen::Active ||
        currentScreen == edh::app::Screen::Setup) {

        // タッチロック中: ジェスチャーに渡さず警告振動のみ
        if (currentScreen == edh::app::Screen::Active && state_.touchLocked) {
            if (touching && !prevTouching_) {
                haptics_.pulse(kVibLockTouchMs);
                lockTouchWarned_ = true;
            } else if (!touching && prevTouching_) {
                lockTouchWarned_ = false;
            }
        } else {
            // --- Active / Setup: タッチ処理 ---

            if (touching && !prevTouching_) {
                // --- 立ち上がり検出（押した瞬間）---

                if (currentScreen == edh::app::Screen::Active) {
                    // Active 画面: 扇形判定と半径区分で処理を分岐する
                    const bool outerRing = edh::isOnOuterRing(x, y);
                    const bool innerZone = edh::isInInnerZone(x, y);

                    if (outerRing) {
                        // 外周リング: GestureDetector にタッチを渡す
                        slidePlayerIndex_ = edh::selectSector(x, y);
                        gesture_.onTouchDown(x, y, nowMs);

                        if (gesture_.state() == input::GestureState::Candidate) {
                            haptics_.beginGesture();
                            haptics_.pulse(kVibStartMs);
                        }

                        innerTouchStarted_ = false;
                    } else if (innerZone) {
                        // 内側領域: タップ判定の開始
                        tapStartX_ = x;
                        tapStartY_ = y;
                        tapStartMs_ = nowMs;
                        innerTouchStarted_ = true;
                    }
                } else {
                    // Setup 画面: 外周スライドのみ
                    gesture_.onTouchDown(x, y, nowMs);
                    if (gesture_.state() == input::GestureState::Candidate) {
                        haptics_.beginGesture();
                        haptics_.pulse(kVibStartMs);
                    }
                    innerTouchStarted_ = false;
                }

                prevTouchX_ = x;
                prevTouchY_ = y;

                // タッチ活動を記録（タイムアウトリセット）
                if (currentScreen == edh::app::Screen::Active) {
                    screenState_.notifyActivity(nowMs);
                }
            }
            // --- 押している間で座標が変化 ---
            else if (touching && prevTouching_) {
                if (innerTouchStarted_) {
                    // 内側タッチ中: 最新座標を追跡する（タップ判定用）。
                    // GestureDetector には渡さない。
                    prevTouchX_ = x;
                    prevTouchY_ = y;
                } else if (x != prevTouchX_ || y != prevTouchY_) {
                    gesture_.onTouchMove(x, y, nowMs);
                    prevTouchX_ = x;
                    prevTouchY_ = y;
                }
            }
            // --- 立ち下がり検出（離した瞬間）---
            else if (!touching && prevTouching_) {
                if (innerTouchStarted_) {
                    // 内側タップの判定。最新座標 (prevTouchX_/Y_) を渡す。
                    handleInnerTap(prevTouchX_, prevTouchY_, nowMs);
                    innerTouchStarted_ = false;
                } else {
                    result = gesture_.onTouchUp(nowMs);
                }
            }
        }
    }
    // Menu / History / About / Sensitivity: タッチを受け付けない

    prevTouching_ = touching;

    // ================================================================
    // 3. 開始拒否の振動
    // ================================================================
    if (gesture_.consumeRejectedStart()) {
        haptics_.pulse(kVibRejectMs);
    }

    // ================================================================
    // 4. ライフ段階の変化に対する振動
    // ================================================================
    if (gesture_.consumeStepChanged()) {
        haptics_.pulseStep();
    }

    // ================================================================
    // 5. プレビューの描画（変化したときだけ）
    // ================================================================
    const auto currentPreview = gesture_.preview();
    const auto currentState   = gesture_.state();
    const bool willCommit = result.committed;

    if (currentPreview.active    != prevPreview_.active ||
        currentPreview.player    != prevPreview_.player ||
        currentPreview.deltaLife != prevPreview_.deltaLife) {

        if (currentPreview.active) {
            if (currentScreen == edh::app::Screen::Active) {
                // Active: どのプレイヤーのプレビューかを決定する
                const uint8_t pi = slidePlayerIndex_;

                // 統率者ダメージビューで自扇形からのスライドの場合
                const uint8_t cmdViewPlayer = screenState_.cmdDamageViewPlayer();
                const bool isCmdDmgSlide =
                    (cmdViewPlayer != edh::kSourceNone) &&
                    (pi == cmdViewPlayer) &&
                    (screenState_.selectedSource() != edh::kSourceNone);

                if (isCmdDmgSlide) {
                    // 統率者ダメージのプレビュー
                    renderer_.drawPlayerSector(
                        state_, screenState_, pi,
                        currentPreview.deltaLife, true);
                } else {
                    // 通常ライフのプレビュー
                    renderer_.drawPlayerSector(
                        state_, screenState_, pi,
                        currentPreview.deltaLife, false);
                }
            }
            // Setup 画面のプレビューは現時点では省略（FaB 版を参照して追加可能）
        } else if (prevPreview_.active && !willCommit) {
            // プレビュー終了（キャンセル等）: 元の表示に戻す
            if (currentScreen == edh::app::Screen::Active) {
                renderer_.drawPlayerSector(
                    state_, screenState_, slidePlayerIndex_, 0, false);
            }
        }
    }

    prevPreview_      = currentPreview;
    prevGestureState_ = currentState;

    // ================================================================
    // 6. 確定
    // ================================================================
    if (willCommit) {
        if (currentScreen == edh::app::Screen::Setup) {
            // Setup: 初期ライフの変更
            const auto base = static_cast<int64_t>(screenState_.setupLife());
            const auto newLife = base + result.deltaLife;
            const uint32_t clamped =
                (newLife < 0) ? 0 : static_cast<uint32_t>(newLife);
            screenState_.setSetupLife(clamped);
            haptics_.pulse(kVibConfirmMs);
        } else {
            // Active: ライフ変更 or 統率者ダメージの確定
            const uint8_t pi = slidePlayerIndex_;
            const uint8_t cmdViewPlayer = screenState_.cmdDamageViewPlayer();
            const bool isCmdDmgSlide =
                (cmdViewPlayer != edh::kSourceNone) &&
                (pi == cmdViewPlayer) &&
                (screenState_.selectedSource() != edh::kSourceNone);

            if (isCmdDmgSlide) {
                // 統率者ダメージ操作（ライフ連動あり）
                const uint8_t srcIdx = screenState_.selectedSource();
                edh::applyCommanderDamage(
                    state_, pi, srcIdx,
                    static_cast<int16_t>(result.deltaLife), nowMs);

                // 統率者ダメージ 21 到達チェック
                if (state_.players[pi].commanderDamageFrom[srcIdx] >= 21) {
                    haptics_.pulse(kVibCmdDmg21Ms);
                } else if (state_.players[pi].life == 0) {
                    haptics_.pulse(kVibLifeZeroMs);
                } else {
                    haptics_.pulse(kVibConfirmMs);
                }
            } else {
                // 通常ライフ操作
                edh::applyLifeChange(
                    state_, pi,
                    static_cast<int16_t>(result.deltaLife), nowMs);

                if (state_.players[pi].life == 0) {
                    haptics_.pulse(kVibLifeZeroMs);
                } else {
                    haptics_.pulse(kVibConfirmMs);
                }
            }

            // 扇形を再描画する
            renderer_.drawPlayerSector(state_, screenState_, pi, 0, false);

            // NVS に永続化する
            storage_.save(state_);

            // タッチ活動を記録
            screenState_.notifyActivity(nowMs);
        }
    }

    // ================================================================
    // 7. 画面の dirty 描画
    // ================================================================
    if (screenState_.consumeDirty()) {
        drawCurrentScreen(nowMs);
        prevHoldPercent_ = 0;
    }

    // ================================================================
    // 8. 長押し進捗描画
    // ================================================================
    {
        uint8_t holdPercent = 0;

        if (aPressed && bPressed) {
            if (currentScreen != edh::app::Screen::Setup) {
                const uint32_t held = buttonInput_.heldMs(nowMs);
                const uint32_t rawPercent =
                    held * 100 / input::kMenuLongPressMs;
                holdPercent =
                    (rawPercent > 100)
                        ? 100
                        : static_cast<uint8_t>((rawPercent / 5) * 5);
            }
        } else if (bPressed && !aPressed) {
            if (currentScreen == edh::app::Screen::Setup) {
                const uint32_t held = buttonInput_.heldMs(nowMs);
                const uint32_t rawPercent =
                    held * 100 / input::kSingleLongPressMs;
                holdPercent =
                    (rawPercent > 100)
                        ? 100
                        : static_cast<uint8_t>((rawPercent / 5) * 5);
            }
        }

        if (holdPercent != prevHoldPercent_) {
            renderer_.drawHoldProgress(holdPercent);
            prevHoldPercent_ = holdPercent;
        }
    }

    // ================================================================
    // 9. Haptics::tick を毎回呼ぶ
    // ================================================================
    haptics_.tick(nowMs);
}

// ============================================================
// handleButtonEvent — ボタンイベントの処理
// ============================================================

void EdhAppController::handleButtonEvent(input::ButtonEvent event,
                                          uint32_t nowMs) {
    const auto prevScreen = screenState_.screen();

    // ================================================================
    // Active 画面
    // ================================================================
    if (prevScreen == edh::app::Screen::Active) {
        switch (event) {
        case input::ButtonEvent::UndoRequested: {
            const bool undone = edh::undoLast(state_);
            if (undone) {
                haptics_.pulse(kVibUndoSuccessMs);
                // 全プレイヤーの扇形を再描画する（Undo は任意のプレイヤーに影響しうる）
                renderer_.drawAll(state_, screenState_);
                storage_.save(state_);
            } else {
                haptics_.pulse(kVibUndoFailMs);
            }
            break;
        }
        case input::ButtonEvent::LockToggleRequested: {
            state_.touchLocked = !state_.touchLocked;
            if (state_.touchLocked) {
                cancelOngoingGesture();
                haptics_.pulse(kVibLockMs);
            } else {
                haptics_.pulse(kVibUnlockMs);
            }
            renderer_.drawLockState(state_);
            storage_.save(state_);
            break;
        }
        case input::ButtonEvent::MenuRequested: {
            cancelOngoingGesture();
            const auto action = screenState_.onLongPressB();
            executeScreenAction(action);
            break;
        }
        default:
            break;
        }
    }
    // ================================================================
    // Setup 画面
    // ================================================================
    else if (prevScreen == edh::app::Screen::Setup) {
        switch (event) {
        case input::ButtonEvent::UndoRequested: {
            // A 短押し: ライフプリセット切替（20/40 トグル）
            const auto action = screenState_.onNext();
            executeScreenAction(action);
            break;
        }
        case input::ButtonEvent::BLongPressed: {
            // B 長押し: 試合開始
            const auto action = screenState_.onLongPressB();
            executeScreenAction(action);
            break;
        }
        default:
            break;
        }
    }
    // ================================================================
    // Menu 画面
    // ================================================================
    else if (prevScreen == edh::app::Screen::Menu) {
        switch (event) {
        case input::ButtonEvent::UndoRequested: {
            // A 短押し: 次の項目へ
            const auto action = screenState_.onNext();
            executeScreenAction(action);
            break;
        }
        case input::ButtonEvent::LockToggleRequested: {
            // B 短押し: 選択
            const auto action = screenState_.onSelect();
            executeScreenAction(action);
            break;
        }
        case input::ButtonEvent::MenuRequested: {
            // A+B 長押し: メニューを閉じる
            const auto action = screenState_.onCloseMenu();
            executeScreenAction(action);
            break;
        }
        case input::ButtonEvent::BLongPressed: {
            // B 長押し: 確認待ちの確定（Rematch 等）
            const auto action = screenState_.onLongPressB();
            executeScreenAction(action);
            break;
        }
        default:
            break;
        }
    }
    // ================================================================
    // History / About / Sensitivity 画面
    // ================================================================
    else {
        switch (event) {
        case input::ButtonEvent::LockToggleRequested: {
            // B 短押し: Menu に戻る
            const auto action = screenState_.onSelect();
            executeScreenAction(action);
            break;
        }
        case input::ButtonEvent::UndoRequested: {
            // A 短押し: Sensitivity では値を変更
            if (prevScreen == edh::app::Screen::Sensitivity) {
                const auto action = screenState_.onNext();
                executeScreenAction(action);
            }
            break;
        }
        case input::ButtonEvent::MenuRequested: {
            // A+B 長押し: Menu に戻る
            const auto action = screenState_.onCloseMenu();
            executeScreenAction(action);
            break;
        }
        default:
            break;
        }
    }

    // ================================================================
    // 画面遷移後の後処理
    // ================================================================

    // Sensitivity 画面から離脱したとき、感度を GestureDetector に反映し NVS に保存する。
    // FaB 版 app_controller.cpp と同じ流儀。
    if (prevScreen == edh::app::Screen::Sensitivity &&
        screenState_.screen() != edh::app::Screen::Sensitivity) {
        const uint8_t idx = screenState_.sensitivityIndex();
        gesture_.setDegreesPerLife(config::degreesPerLifeFromPreset(idx));
        storage_.saveSensitivity(idx);
    }
}

// ============================================================
// executeScreenAction — ScreenAction の実行
// ============================================================

void EdhAppController::executeScreenAction(edh::app::ScreenAction action) {
    switch (action) {
    case edh::app::ScreenAction::None:
        break;

    case edh::app::ScreenAction::StartMatch: {
        const uint32_t startingLife = screenState_.setupLife();
        edh::startMatch(state_, startingLife);
        screenState_.enterActive();
        renderer_.drawAll(state_, screenState_);
        storage_.save(state_);
        break;
    }

    case edh::app::ScreenAction::Rematch: {
        edh::rematch(state_);
        screenState_.enterActive();
        renderer_.drawAll(state_, screenState_);
        storage_.save(state_);
        break;
    }
    }
}

// ============================================================
// cancelOngoingGesture — ジェスチャーの破棄
// ============================================================

void EdhAppController::cancelOngoingGesture() {
    if (gesture_.state() != input::GestureState::Idle) {
        gesture_.reset();
        prevPreview_ = {};
        prevGestureState_ = input::GestureState::Idle;
    }
}

// ============================================================
// drawCurrentScreen — 現在の画面の描画
// ============================================================

void EdhAppController::drawCurrentScreen(uint32_t nowMs) {
    const auto screen = screenState_.screen();

    switch (screen) {
    case edh::app::Screen::Setup:
        renderer_.drawSetup(screenState_);
        break;

    case edh::app::Screen::Active:
        renderer_.drawAll(state_, screenState_);
        renderer_.drawLockState(state_);
        break;

    case edh::app::Screen::Menu:
        renderer_.drawMenu(screenState_,
                           M5.Power.getBatteryLevel(),
                           M5.Power.isCharging());
        break;

    case edh::app::Screen::History:
        renderer_.drawHistory(state_);
        break;

    case edh::app::Screen::About:
        renderer_.drawAbout();
        break;

    case edh::app::Screen::Sensitivity:
        renderer_.drawSensitivity(screenState_);
        break;
    }
}

// ============================================================
// handleInnerTap — 内側タップの判定と処理
// ============================================================

bool EdhAppController::handleInnerTap(int16_t x, int16_t y, uint32_t nowMs) {
    // タップ判定: 移動量と時間が閾値以下であること
    const int16_t dx = x - tapStartX_;
    const int16_t dy = y - tapStartY_;
    const int32_t moveSq = static_cast<int32_t>(dx) * dx +
                           static_cast<int32_t>(dy) * dy;
    const int32_t maxMoveSq =
        static_cast<int32_t>(edh::kTapMaxMovePx) *
        edh::kTapMaxMovePx;
    const uint32_t elapsed = nowMs - tapStartMs_;

    if (moveSq > maxMoveSq || elapsed > edh::kTapMaxDurationMs) {
        return false;  // タップではない（ドラッグ等）
    }

    // タップが成立: タッチ開始位置の扇形を判定する
    const uint8_t tappedSector = edh::selectSector(tapStartX_, tapStartY_);

    // EdhScreenState の onInnerTap を呼ぶ
    // onInnerTap は排他制御のみを行う。
    // 振り分けはアプリ層の責務（仕様書の申し送り事項）。

    const uint8_t cmdViewPlayer = screenState_.cmdDamageViewPlayer();

    if (cmdViewPlayer == edh::kSourceNone) {
        // 誰も CmdDamageView を開いていない → 自分の扇形をトグル
        screenState_.onInnerTap(tappedSector, nowMs);
        haptics_.pulse(kVibTapMs);
    } else if (tappedSector == cmdViewPlayer) {
        // CmdDamageView を開いているプレイヤー自身をタップ → ライフビューに戻す
        screenState_.onInnerTap(tappedSector, nowMs);
        haptics_.pulse(kVibTapMs);
    } else {
        // 他扇形をタップ → 被弾元選択
        screenState_.selectSource(tappedSector, nowMs);
        haptics_.pulse(kVibTapMs);
    }

    // タップされた扇形を再描画する
    renderer_.drawPlayerSector(state_, screenState_, tappedSector, 0, false);

    // CmdDamageView が開いている場合、そのプレイヤーの扇形も再描画する
    // （被弾元選択の反映のため）
    if (cmdViewPlayer != edh::kSourceNone && tappedSector != cmdViewPlayer) {
        renderer_.drawPlayerSector(
            state_, screenState_, cmdViewPlayer, 0, false);
    }

    return true;
}

}  // namespace counter::app
