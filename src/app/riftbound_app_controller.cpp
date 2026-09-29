// Riftbound（1v1 得点カウンター）本体 -- アプリケーション制御層の実装
//
// このファイルは docs/07-architecture.md の入力パイプラインに従って
// ボタン入力 -> タッチ入力 -> ジェスチャー検出 -> ドメイン更新 -> 描画 の流れを制御する。
// 構造は FaB 版 app_controller.cpp と同一であり、意味の差分はファイル冒頭の
// riftbound_app_controller.hpp のコメントにまとめてある。

#include "riftbound_app_controller.hpp"

#include <M5Unified.h>

#include "app_config.hpp"
#include "domain/riftbound_score_service.hpp"

namespace counter::app {

// 振動パターンの持続時間 (ms) は lib/counter_core/app_config.hpp の counter::config::kVib* に集約済み。

// ============================================================
// 診断用シリアルログ。
// 実機デバッグが終わったら APP_DEBUG_LOG を 0 に変更して無効化する。
// ログ書式は機械解析向けの固定フォーマット:
//   BTN,<EventName>,<Screen>        — ボタンイベント発火時
//   SCREEN,<OldScreen>,<NewScreen>  — 画面遷移時
//   HELD,<ms>,<Screen>,<A>,<B>      — ボタン押下中（200ms 間引き）
// ============================================================
#ifndef APP_DEBUG_LOG
#define APP_DEBUG_LOG 0
#endif

#if APP_DEBUG_LOG
namespace {

const char* buttonEventName(input::ButtonEvent e) {
    switch (e) {
    case input::ButtonEvent::None:                return "None";
    case input::ButtonEvent::UndoRequested:       return "UndoRequested";
    case input::ButtonEvent::LockToggleRequested: return "LockToggleRequested";
    case input::ButtonEvent::MenuRequested:       return "MenuRequested";
    case input::ButtonEvent::ALongPressed:        return "ALongPressed";
    case input::ButtonEvent::BLongPressed:        return "BLongPressed";
    }
    return "?";
}

const char* screenName(Screen s) {
    switch (s) {
    case Screen::Setup:   return "Setup";
    case Screen::Active:  return "Active";
    case Screen::Menu:    return "Menu";
    case Screen::History: return "History";
    case Screen::About:       return "About";
    case Screen::Sensitivity: return "Sensitivity";
    }
    return "?";
}

}  // namespace
#endif

void RiftboundAppController::begin() {
    renderer_.begin();
    haptics_.begin();
    storage_.begin();

    // NVS から感度設定を復元する。試合状態とは独立して管理するため、
    // 試合の有無にかかわらず常に読み出す。
    {
        const uint8_t sensIdx = storage_.loadedSensitivity();
        screenState_.setSensitivityIndex(sensIdx);
        gesture_.setDegreesPerLife(
            config::degreesPerLifeFromPreset(sensIdx));
        // setSensitivityIndex が dirty フラグを立てるため消費する。
        screenState_.consumeDirty();
    }

    // NVS に有効な試合状態があり、かつ試合が進行中 (active) の場合のみ
    // 復元して Active 画面で再開する。active が false の状態は復元せず、
    // 通常の初期化から始める。
    if (storage_.hasValidState() && storage_.loadedState().active) {
        state_ = storage_.loadedState();
        screenState_.enterActive();
        renderer_.drawAll(state_);
        renderer_.drawLockState(state_);
        screenState_.consumeDirty();
        return;
    }

    // NVS に有効な状態がない場合は Setup 画面から開始する。
    // 得点は常に 0 から始まるため、FaB 版のような開始値の選択は不要である
    // （Setup 画面は試合開始の確認のみに使う）。
    screenState_.reset();
    renderer_.drawSetup(screenState_);
}

void RiftboundAppController::update(uint32_t nowMs) {
    // ================================================================
    // 0. 物理ボタンの処理（タッチより先に処理する）
    //     ButtonInput に毎ループ押下状態を渡し、イベントを取得する。
    //     docs/05: 画面から見て左が BtnA、右が BtnB。
    //     タッチロック中もボタンは有効（docs/05）。
    // ================================================================
    const bool aPressed = M5.BtnA.isPressed();
    const bool bPressed = M5.BtnB.isPressed();
    const auto buttonEvent = buttonInput_.update(aPressed, bPressed, nowMs);

#if APP_DEBUG_LOG
    const auto screenBeforeBtn = screenState_.screen();
#endif

    if (buttonEvent != input::ButtonEvent::None) {
#if APP_DEBUG_LOG
        Serial.printf("BTN,%s,%s\n",
                      buttonEventName(buttonEvent),
                      screenName(screenBeforeBtn));
#endif
        handleButtonEvent(buttonEvent, nowMs);
    }

    const auto currentScreen = screenState_.screen();

#if APP_DEBUG_LOG
    if (currentScreen != screenBeforeBtn) {
        Serial.printf("SCREEN,%s,%s\n",
                      screenName(screenBeforeBtn),
                      screenName(currentScreen));
    }
#endif

    // ================================================================
    // 1. タッチの取得
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

    // ================================================================
    // 2. 画面ごとのタッチ振り分け
    //    - Active: 外周スライドで得点を増減（ロック中は警告振動のみ）
    //    - Setup / Menu / History / About / Sensitivity:
    //      タッチを一切受け付けない。
    //      FaB 版は Setup で外周スライドにより開始ライフを調整するが、
    //      Riftbound の得点は常に 0 から始まるため Setup で設定する
    //      値が存在しない。
    // ================================================================
    if (currentScreen == Screen::Active) {
        // Active かつタッチロック中: ジェスチャーに渡さず警告振動のみ
        if (state_.touchLocked) {
            // 押した瞬間に1回だけ警告振動を鳴らす（docs/05: 最短パルス 20ms）。
            // lockTouchWarned_ で連発を防止する。
            if (touching && !prevTouching_) {
                haptics_.pulse(config::kVibLockTouchMs);
                lockTouchWarned_ = true;
            } else if (!touching && prevTouching_) {
                lockTouchWarned_ = false;
            }
        } else {
            // Active（ロック解除）: GestureDetector にタッチを渡す

            // --- 立ち上がり検出（押した瞬間）---
            if (touching && !prevTouching_) {
                gesture_.onTouchDown(x, y, nowMs);

                // 有効な開始（Candidate に遷移した場合のみ）で開始の振動を鳴らす。
                if (gesture_.state() == input::GestureState::Candidate) {
                    haptics_.beginGesture();
                    haptics_.pulse(config::kVibStartMs);
                }

                prevTouchX_ = x;
                prevTouchY_ = y;
            }
            // --- 押している間で座標が変化 ---
            else if (touching && prevTouching_) {
                if (x != prevTouchX_ || y != prevTouchY_) {
                    gesture_.onTouchMove(x, y, nowMs);
                    prevTouchX_ = x;
                    prevTouchY_ = y;
                }
            }
            // --- 立ち下がり検出（離した瞬間）---
            else if (!touching && prevTouching_) {
                result = gesture_.onTouchUp(nowMs);
            }
        }
    }
    // Setup / Menu / History / About / Sensitivity:
    // タッチを GestureDetector に渡さない（誤操作防止）

    prevTouching_ = touching;

    // ================================================================
    // 3. 開始拒否の扱い
    //    docs/05: 開始禁止領域に触れた場合は最短パルス (20ms) で警告する。
    // ================================================================
    if (gesture_.consumeRejectedStart()) {
        haptics_.pulse(config::kVibRejectMs);
    }

    // ================================================================
    // 4. 得点段階の変化に対する振動
    //    間引きは Haptics 側の責務（docs/07, docs/05）。
    // ================================================================
    if (gesture_.consumeStepChanged()) {
        haptics_.pulseStep();
    }

    // ================================================================
    // 5. プレビューの描画（変化したときだけ）
    //    部分再描画でも約 5.0 ms かかるため（docs/07 実測）、
    //    前フレームと比較して変化があるときだけ描画する。
    // ================================================================
    const auto currentPreview = gesture_.preview();
    const auto currentState   = gesture_.state();

    // 確定フローでは step 6 で描画するため、ここでのプレビュークリアをスキップする。
    const bool willCommit = result.committed;

    // プレビュー値が変化したら部分再描画する
    if (currentPreview.active    != prevPreview_.active ||
        currentPreview.player    != prevPreview_.player ||
        currentPreview.deltaLife != prevPreview_.deltaLife) {

        if (currentPreview.active) {
            // Active: プレビュー表示中の現在のプレビュー値を描画
            renderer_.drawScore(state_, currentPreview.player,
                                currentPreview.deltaLife);
        } else if (prevPreview_.active && !willCommit) {
            // プレビューが終了した（キャンセルなど）ので元の得点値を描画する。
            // 確定時は step 6 で処理するのでここでは描画しない。
            renderer_.drawScore(state_, prevPreview_.player, 0);
        }
    }

    // リングハイライトの更新:
    // Active（ジェスチャー状態）に遷移した瞬間に点灯し、離れた瞬間に消灯する。
    if (currentState == input::GestureState::Active &&
        prevGestureState_ != input::GestureState::Active) {
        renderer_.drawRingHighlight(currentPreview.player, true);
    } else if (currentState != input::GestureState::Active &&
               prevGestureState_ == input::GestureState::Active) {
        // 消灯時は前フレームのプレイヤーを参照する。
        renderer_.drawRingHighlight(prevPreview_.player, false);
    }

    prevPreview_      = currentPreview;
    prevGestureState_ = currentState;

    // ================================================================
    // 6. 確定
    //    指を離して committed == true なら得点を確定する。
    //    Active 画面のみでジェスチャーを受け付けるため、確定も Active のみ。
    // ================================================================
    if (willCommit) {
        commitScore(result.player, result.deltaLife, nowMs);
    }

    // ================================================================
    // 7. 画面の dirty 描画
    //    ScreenState の状態変化（画面遷移・メニューカーソル移動・得点変更等）が
    //    あったときだけ再描画する。
    //
    //    【順序の理由】ステップ 7 → ステップ 8 の順で実行する。
    //    drawHoldProgress() の描画は全画面メソッド (drawMenu / drawAll 等) で
    //    上書きされるため、全画面再描画の「後」に進捗を重ねる必要がある。
    // ================================================================
    if (screenState_.consumeDirty()) {
        drawCurrentScreen(nowMs);
        prevHoldPercent_ = 0;
    }

    // ================================================================
    // 8. 長押し進捗描画（全画面共通）
    //    画面ごとに「その長押しが意味を持つか」で表示を出し分ける。
    //
    //    | 画面              | A+B 長押し       | 単独 B 長押し        |
    //    |-------------------|------------------|----------------------|
    //    | Active            | 表示（メニュー） | なし                 |
    //    | Setup             | なし             | 表示（START）         |
    //    | Menu（確認待ちなし）| 表示（閉じる）   | なし                 |
    //    | Menu（確認待ち）   | 表示（閉じる）   | 表示（確定）          |
    //    | History/About/Sens| 表示（Menu へ）  | なし                 |
    //    単独 A 長押しはどの画面でも表示しない（意味のある操作が無い）。
    // ================================================================
    {
        uint8_t holdPercent = 0;

        if (aPressed && bPressed) {
            // A+B 長押し: Active / Menu / History / About で意味がある。
            if (currentScreen != Screen::Setup) {
                const uint32_t held = buttonInput_.heldMs(nowMs);
                const uint32_t rawPercent =
                    held * 100 / input::kMenuLongPressMs;
                holdPercent =
                    (rawPercent > 100)
                        ? 100
                        : static_cast<uint8_t>((rawPercent / 5) * 5);
            }
        } else if (bPressed && !aPressed) {
            // 単独 B 長押し: Setup（START）/ Menu 確認待ち（確定）で意味がある。
            if (currentScreen == Screen::Setup ||
                (currentScreen == Screen::Menu &&
                 screenState_.awaitingConfirm())) {
                const uint32_t held = buttonInput_.heldMs(nowMs);
                const uint32_t rawPercent =
                    held * 100 / input::kSingleLongPressMs;
                holdPercent =
                    (rawPercent > 100)
                        ? 100
                        : static_cast<uint8_t>((rawPercent / 5) * 5);
            }
        }
        // 単独 A 長押し / ボタン非押下: holdPercent = 0 のまま

        if (holdPercent != prevHoldPercent_) {
            renderer_.drawHoldProgress(holdPercent);
            prevHoldPercent_ = holdPercent;
        }
    }

    // ================================================================
    // 9. 診断用: ボタン押下中の heldMs ログ（200ms 間引き）
    // ================================================================
#if APP_DEBUG_LOG
    {
        const uint32_t held = buttonInput_.heldMs(nowMs);
        if (held > 0 && (nowMs - lastHeldLogMs_ >= 200)) {
            Serial.printf("HELD,%lu,%s,%d,%d\n",
                          static_cast<unsigned long>(held),
                          screenName(currentScreen),
                          static_cast<int>(aPressed),
                          static_cast<int>(bPressed));
            lastHeldLogMs_ = nowMs;
        } else if (held == 0) {
            lastHeldLogMs_ = 0;
        }
    }
#endif

    // ================================================================
    // 10. Haptics::tick を毎回呼ぶ
    //     振動の停止タイミング管理はここでしか行われない (docs/07)。
    //     tick を呼ばないとモーターが回りっぱなしになる。
    // ================================================================
    haptics_.tick(nowMs);
}

// ============================================================
// ボタンイベントの処理
// 画面ごとにボタンの意味が変わるため、Active と非 Active で分岐する。
// Active 以外では Undo とロックが発動しないことで、メニュー操作中に
// 誤って試合状態が変わることを防ぐ。
// ============================================================
void RiftboundAppController::handleButtonEvent(input::ButtonEvent event,
                                               uint32_t /*nowMs*/) {
    const auto currentScreen = screenState_.screen();

    // ================================================================
    // Active 画面: Undo / ロック切替 + メニュー起動
    // ================================================================
    if (currentScreen == Screen::Active) {
        switch (event) {
        case input::ButtonEvent::UndoRequested: {
            // Undo はタッチロック中でも有効（docs/05）。
            const bool undone = riftbound::undoLast(state_);
            if (undone) {
                // 成功: 確定と同等の振動で「操作が成立した」ことを伝える (40ms)
                haptics_.pulse(config::kVibUndoSuccessMs);

                // 両プレイヤーの数字を再描画する。
                // なぜ両方か: undoLast はどちらのプレイヤーの得点を戻したのか
                // 呼び出し側に返さないため、安全側に倒して両方を更新する。
                renderer_.drawScore(state_, riftbound::PlayerId::Top, 0);
                renderer_.drawScore(state_, riftbound::PlayerId::Bottom, 0);

                // Undo 後の状態を NVS に永続化する。
                storage_.save(state_);
            } else {
                // 失敗（履歴空）: 最短パルスで「無効操作」を伝える (20ms)。
                haptics_.pulse(config::kVibUndoFailMs);
            }
            break;
        }

        case input::ButtonEvent::LockToggleRequested: {
            // タッチロックをトグルする
            state_.touchLocked = !state_.touchLocked;

            if (state_.touchLocked) {
                // ロック時: 長めのパルスで「重要な状態変更」を伝える (80ms)。
                haptics_.pulse(config::kVibLockMs);

                // ロックした瞬間に進行中のジェスチャーがあれば破棄する。
                cancelOngoingGesture();
            } else {
                // ロック解除: 通常の確定と同等の振動 (40ms)。
                haptics_.pulse(config::kVibUnlockMs);

                lockTouchWarned_ = false;
            }

            // ロック状態の描画を更新する
            renderer_.drawLockState(state_);
            break;
        }

        case input::ButtonEvent::MenuRequested: {
            // メニューを開く前に進行中のジェスチャーを破棄する。
            cancelOngoingGesture();
            const auto action = screenState_.onCloseMenu();
            executeScreenAction(action);
            break;
        }

        case input::ButtonEvent::ALongPressed:
        case input::ButtonEvent::BLongPressed:
            // Active では単独長押しは何もしない
            break;

        case input::ButtonEvent::None:
            break;
        }
        return;
    }

    // ================================================================
    // Active 以外（Setup / Menu / History / About / Sensitivity）:
    // ボタンイベントを ScreenState に委譲する。
    // Undo やロック切替は Active 専用なので、ここでは発動しない。
    // ================================================================
    const auto prevScreen = currentScreen;
    ScreenAction action = ScreenAction::None;

    switch (event) {
    case input::ButtonEvent::UndoRequested:
        // A 短押し → カーソル移動 / 画面遷移
        action = screenState_.onNext();
        break;

    case input::ButtonEvent::LockToggleRequested:
        // B 短押し → 項目選択 / 決定
        action = screenState_.onSelect();
        break;

    case input::ButtonEvent::BLongPressed:
        // B 長押し → 確認を確定（Rematch）/ Setup で試合開始
        action = screenState_.onLongPressB();
        break;

    case input::ButtonEvent::MenuRequested:
        // A+B 長押し → メニューを閉じる等
        cancelOngoingGesture();
        action = screenState_.onCloseMenu();
        break;

    case input::ButtonEvent::ALongPressed:
    case input::ButtonEvent::None:
        // 何もしない
        break;
    }

    // Sensitivity 画面から離脱したとき、感度を GestureDetector に反映し NVS に保存する。
    // 離脱は B 短押し（onSelect → Menu）または A+B 長押し（onCloseMenu → Menu）で起きる。
    if (prevScreen == Screen::Sensitivity &&
        screenState_.screen() != Screen::Sensitivity) {
        const uint8_t idx = screenState_.sensitivityIndex();
        gesture_.setDegreesPerLife(config::degreesPerLifeFromPreset(idx));
        storage_.saveSensitivity(idx);
    }

    // FaB 版には「Set Life 選択による Setup 遷移を検出して setupLife に
    // 現在値を写す」処理が存在するが、Riftbound には SetLife メニューが
    // 存在しないためこの処理は不要である（Setup は起動時のみ表示される）。

    if (action != ScreenAction::None) {
        executeScreenAction(action);
    }
}

// ============================================================
// ScreenAction の実行
// ScreenState の入力メソッドが返したアクションをドメイン層に反映し、
// 必要な描画を即座に行う。
// ============================================================
void RiftboundAppController::executeScreenAction(ScreenAction action) {
    switch (action) {
    case ScreenAction::StartMatch:
        // Setup で確定。得点 0 / 0、勝利点 8 で試合を開始する
        riftbound::startMatch(state_);
        screenState_.enterActive();
        renderer_.drawAll(state_);
        // enterActive が dirty を立てるので、consumeDirty で二重描画しないよう消費する
        screenState_.consumeDirty();
        // 試合開始時の状態を NVS に永続化する。
        storage_.save(state_);
        break;

    case ScreenAction::Rematch:
        // 得点 0 でやり直す
        riftbound::rematch(state_);
        screenState_.enterActive();
        renderer_.drawAll(state_);
        screenState_.consumeDirty();
        // Rematch 後の状態を NVS に永続化する。
        storage_.save(state_);
        break;

    case ScreenAction::SwitchGame:
        // メニューの Switch Game 確定。AppLauncher へゲーム切替を要求する。
        // 状態は既に NVS へ保存済み（確定・Undo のたびに save される）であり、
        // ここでは要求フラグを立てるだけである。フラグは AppLauncher が
        // consumeSwitchRequested() で消費し、ゲーム選択画面へ戻る。
        switchRequested_ = true;
        break;

    case ScreenAction::None:
        break;
    }
}

// ============================================================
// consumeSwitchRequested — ゲーム切替要求の取得（消費型）
// ============================================================

bool RiftboundAppController::consumeSwitchRequested() {
    const bool requested = switchRequested_;
    switchRequested_ = false;
    return requested;
}

// ============================================================
// 得点の確定（Active 画面専用）
// ドメインに得点変更を適用し、振動・描画・NVS 保存を行う。
// ============================================================
void RiftboundAppController::commitScore(riftbound::PlayerId player,
                                         int32_t delta, uint32_t nowMs) {
    riftbound::applyScoreChange(state_, player, delta, nowMs);

    // 勝利点に到達した場合はより長い振動で警告する (120ms)。
    // FaB 版のライフ 0 到達（kVibLifeZeroMs）と同じパルス長を使い、
    // 「強い警告」という意味階層を共通化する。
    // それ以外は通常の確定振動 (40ms)。
    if (riftbound::hasReachedVictory(state_, player)) {
        haptics_.pulse(config::kVibLifeZeroMs);
    } else {
        haptics_.pulse(config::kVibConfirmMs);
    }

    // 確定後の得点値を描画する。
    // previewDelta = 0 でプレビューなしの確定表示を行う。
    renderer_.drawScore(state_, player, 0);

    // 得点変更を NVS に永続化する。
    // スライド中の中間値ではなく確定時のみ保存する（NVS 書き込み寿命のため）。
    // save() が失敗してもアプリの動作は継続する。
    storage_.save(state_);
}

// ============================================================
// 進行中のジェスチャーの破棄
// ============================================================
void RiftboundAppController::cancelOngoingGesture() {
    if (gesture_.state() == input::GestureState::Idle) return;

    // リングハイライトが点灯中なら消灯する
    if (gesture_.state() == input::GestureState::Active) {
        renderer_.drawRingHighlight(gesture_.preview().player, false);
    }

    // プレビュー中なら元の得点値に戻す
    const auto preview = gesture_.preview();
    if (preview.active) {
        renderer_.drawScore(state_, preview.player, 0);
    }

    gesture_.reset();

    // prevPreview_ と prevGestureState_ もリセットする。
    // でないと次フレームのプレビュー差分検出で不整合が起きる。
    prevPreview_ = input::GesturePreview{};
    prevGestureState_ = input::GestureState::Idle;
}

// ============================================================
// 現在画面の描画
// consumeDirty() が true を返したときに呼ばれる。
// ============================================================
void RiftboundAppController::drawCurrentScreen(uint32_t /*nowMs*/) {
    // 全画面メソッドを呼ぶ。進捗表示の責務は update() ステップ 8 の
    // drawHoldProgress() に一本化されたため、ここでは進捗を計算しない。
    switch (screenState_.screen()) {
    case Screen::Setup:
        renderer_.drawSetup(screenState_);
        break;

    case Screen::Active:
        // 全画面再描画 + ロック状態の表示。
        // drawAll がロック表示を含むかは Renderer 実装依存のため、
        // 安全側に倒して drawLockState も呼ぶ。冗長でも害はない。
        renderer_.drawAll(state_);
        renderer_.drawLockState(state_);
        break;

    case Screen::Menu: {
        // バッテリー情報をメニュー描画のたびに読み取る。
        const uint8_t batPercent = M5.Power.getBatteryLevel();
        const bool charging = M5.Power.isCharging();
        renderer_.drawMenu(screenState_, batPercent, charging);
        break;
    }

    case Screen::History:
        renderer_.drawHistory(state_);
        break;

    case Screen::About:
        renderer_.drawAbout();
        break;

    case Screen::Sensitivity:
        renderer_.drawSensitivity(screenState_);
        break;
    }
}

}  // namespace counter::app
