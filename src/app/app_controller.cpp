// Phase 1 + Phase 2 + Phase 3: ライフカウンター本体 -- アプリケーション制御層の実装
//
// このファイルは docs/07-architecture.md の入力パイプラインに従って
// ボタン入力 -> タッチ入力 -> ジェスチャー検出 -> ドメイン更新 -> 描画 の流れを制御する。
//
// Phase 2 Wave 1 で追加:
//   - Undo（物理ボタン A 短押し）
//   - タッチロック（物理ボタン B 短押し）
//
// Phase 2 Wave 2 で追加:
//   - 画面遷移管理（Setup / Active / Menu / History / About）
//   - ScreenState によるメニュー・初期ライフ設定の結線
//   - ボタンイベントの画面別ルーティング
//   - Setup 画面での外周スライドによる開始ライフ調整

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

// --- Phase 2 Wave 1 で追加した振動パターン ---
// docs/05 は Undo / ロック / ロック解除の振動時間を明記していないが、
// 「振動の強弱は時間の長短でのみ区別する」(docs/05) の原則に従い、
// 既存パターン（20ms = 警告/無効、40ms = 確定/成功）との一貫性で設計する。
constexpr uint32_t kVibUndoSuccessMs = 40;   // Undo 成功: 確定と同等の「操作成立」フィードバック
constexpr uint32_t kVibUndoFailMs    = 20;   // Undo 失敗（履歴空）: 無効操作の警告（最短パルス）
constexpr uint32_t kVibLockMs        = 80;   // ロック: 重要な状態変更を長めのパルスで伝達
constexpr uint32_t kVibUnlockMs      = 40;   // ロック解除: 通常の確定と同等
constexpr uint32_t kVibLockTouchMs   = 20;   // ロック中タッチ: 最短パルスで「無効」を通知

}  // namespace

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
    case Screen::About:   return "About";
    }
    return "?";
}

}  // namespace
#endif

void AppController::begin() {
    renderer_.begin();
    haptics_.begin();
    storage_.begin();

    // NVS に有効な試合状態があり、かつ試合が進行中 (active) の場合のみ
    // 復元して Active 画面で再開する。active が false の状態（例: NewGame で
    // Setup に遷移した直後に電源が切れた場合）は復元せず、通常の初期化から始める。
    if (storage_.hasValidState() && storage_.loadedState().active) {
        state_ = storage_.loadedState();
        screenState_.enterActive();
        renderer_.drawAll(state_);
        renderer_.drawLockState(state_);
        screenState_.consumeDirty();
        return;
    }

    // NVS に有効な状態がない場合は Setup 画面から開始する。
    // Phase 1 では固定値 (40 ライフ) で即 Active に入っていたが、
    // Phase 2 では初期ライフの選択画面 (Setup) から開始する。
    // ScreenState のデフォルトライフ (40) が初期値として使われる。
    screenState_.reset();
    renderer_.drawSetup(screenState_);
}

void AppController::update(uint32_t nowMs) {
    // ================================================================
    // 0. 物理ボタンの処理（タッチより先に処理する）
    //     ButtonInput に毎ループ押下状態を渡し、イベントを取得する。
    //     docs/05: 画面から見て左が BtnA、右が BtnB。
    //     タッチロック中もボタンは有効（docs/05: 「タッチロック中もすべての
    //     物理ボタン操作は有効である」）。
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
    //    - Active: 既存どおり（ロック中はジェスチャーに渡さず警告振動）
    //    - Setup: 外周スライドで開始ライフを増減する
    //    - Menu / History / About: タッチを一切受け付けない（誤操作防止）
    // ================================================================
    if (currentScreen == Screen::Active || currentScreen == Screen::Setup) {
        // Active かつタッチロック中: ジェスチャーに渡さず警告振動のみ
        if (currentScreen == Screen::Active && state_.touchLocked) {
            // 押した瞬間に1回だけ警告振動を鳴らす（docs/05: 最短パルス 20ms）。
            // lockTouchWarned_ で連発を防止する。指を離すまでフラグを維持し、
            // 指を離したらリセットして次のタッチで再度鳴るようにする。
            if (touching && !prevTouching_) {
                haptics_.pulse(kVibLockTouchMs);
                lockTouchWarned_ = true;
            } else if (!touching && prevTouching_) {
                lockTouchWarned_ = false;
            }
        } else {
            // Active（ロック解除）/ Setup: GestureDetector にタッチを渡す

            // --- 立ち上がり検出（押した瞬間）---
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
            else if (!touching && prevTouching_) {
                result = gesture_.onTouchUp(nowMs);
            }
        }
    }
    // Menu / History / About: タッチを GestureDetector に渡さない（誤操作防止）

    prevTouching_ = touching;

    // ================================================================
    // 3. 開始拒否の扱い
    //    docs/05: 開始禁止領域に触れた場合は最短パルス (20ms) で警告する。
    //    「弱い振動」は強度では実現不可（255 以外は体感できない）のため
    //    最短時間で他パターンと区別する。
    // ================================================================
    if (gesture_.consumeRejectedStart()) {
        haptics_.pulse(kVibRejectMs);
    }

    // ================================================================
    // 4. ライフ段階の変化に対する振動
    //    間引きは Haptics 側の責務（docs/07, docs/05）。
    //    AppController は段階変化ごとに無条件で pulseStep を呼ぶ。
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
    // そうしないと、古い state で一瞬描画した直後に確定後の state で再描画してしまう。
    const bool willCommit = result.committed;

    // プレビュー値が変化したら部分再描画する
    if (currentPreview.active    != prevPreview_.active ||
        currentPreview.player    != prevPreview_.player ||
        currentPreview.deltaLife != prevPreview_.deltaLife) {

        if (currentPreview.active) {
            if (currentScreen == Screen::Setup) {
                // Setup: setupLife をベースにプレビュー表示する。
                // drawLife は MatchState を要求するので、setupLife を詰めた
                // 仮の MatchState を組み立てて渡す。
                domain::MatchState setupDisplay{};
                setupDisplay.players[0].life =
                    screenState_.setupLife(PlayerId::Top);
                setupDisplay.players[1].life =
                    screenState_.setupLife(PlayerId::Bottom);

                // プレビュー中に 0 未満にならないようデルタをクランプする。
                // ユーザーが負のライフを目にするのを防ぐため。
                int32_t clampedDelta = currentPreview.deltaLife;
                const auto base = static_cast<int64_t>(
                    setupDisplay.players[domain::toIndex(
                        currentPreview.player)].life);
                if (base + clampedDelta < 0) {
                    clampedDelta = static_cast<int32_t>(-base);
                }
                renderer_.drawLife(setupDisplay, currentPreview.player,
                                  clampedDelta);
            } else {
                // Active: プレビュー表示中の現在のプレビュー値を描画
                renderer_.drawLife(state_, currentPreview.player,
                                  currentPreview.deltaLife);
            }
        } else if (prevPreview_.active && !willCommit) {
            // プレビューが終了した（キャンセルなど）ので元のライフ値を描画する。
            // 確定時は step 6 で処理するのでここでは描画しない。
            if (currentScreen == Screen::Setup) {
                domain::MatchState setupDisplay{};
                setupDisplay.players[0].life =
                    screenState_.setupLife(PlayerId::Top);
                setupDisplay.players[1].life =
                    screenState_.setupLife(PlayerId::Bottom);
                renderer_.drawLife(setupDisplay, prevPreview_.player, 0);
            } else {
                renderer_.drawLife(state_, prevPreview_.player, 0);
            }
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
        // 今フレームでは Idle/Cancelled に遷移しており、
        // currentPreview からプレイヤーを取るのは不正確な可能性があるため。
        renderer_.drawRingHighlight(prevPreview_.player, false);
    }

    prevPreview_      = currentPreview;
    prevGestureState_ = currentState;

    // ================================================================
    // 6. 確定
    //    指を離して committed == true なら、画面に応じた確定処理を行う。
    // ================================================================
    if (willCommit) {
        if (currentScreen == Screen::Setup) {
            // Setup: setupLife を更新する。0 を下限としてクランプする。
            // setSetupLife 側がクランプしない可能性に備えて呼び出し側で保証する。
            const auto base = static_cast<int64_t>(
                screenState_.setupLife(result.player));
            const auto newLife = base + result.deltaLife;
            const uint32_t clamped =
                (newLife < 0) ? 0 : static_cast<uint32_t>(newLife);
            screenState_.setSetupLife(result.player, clamped);
            haptics_.pulse(kVibConfirmMs);
            // dirty フラグが立つので、consumeDirty → drawSetup で反映される
        } else {
            // Active: ドメインにライフ変更を適用する
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

            // ライフ変更を NVS に永続化する。
            // スライド中の中間値ではなく確定時のみ保存する（NVS 書き込み寿命のため）。
            // save() が失敗してもアプリの動作は継続する。
            storage_.save(state_);
        }
    }

    // ================================================================
    // 7. 画面の dirty 描画
    //    ScreenState の状態変化（画面遷移・メニューカーソル移動・ライフ変更等）が
    //    あったときだけ再描画する。毎ループ描画しないことで不要な全画面転送を回避する。
    //
    //    【順序の理由】ステップ 7 → ステップ 8 の順で実行する。
    //    drawHoldProgress() の描画は全画面メソッド (drawMenu / drawAll 等) で
    //    上書きされるため、全画面再描画の「後」に進捗を重ねる必要がある。
    //    consumeDirty() が true のとき prevHoldPercent_ を 0 にリセットすることで、
    //    ボタンが押されたまま画面遷移した場合でもステップ 8 で進捗が再描画される。
    //    ボタンが押されていなければ holdPercent=0 と一致し、不要な
    //    drawHoldProgress(0) は呼ばれない。
    // ================================================================
    if (screenState_.consumeDirty()) {
        drawCurrentScreen(nowMs);
        // 全画面再描画は進捗弧を上書きする。prevHoldPercent_ を 0 にリセット
        // することで、ステップ 8 が holdPercent > 0 のとき再描画を行う。
        prevHoldPercent_ = 0;
    }

    // ================================================================
    // 8. 長押し進捗描画（全画面共通）
    //    画面ごとに「その長押しが意味を持つか」で表示を出し分ける。
    //    意味の無い長押しに進捗を出すと、溜まりきっても何も起きず混乱を招く。
    //
    //    | 画面              | A+B 長押し       | 単独 B 長押し        |
    //    |-------------------|------------------|----------------------|
    //    | Active            | 表示（メニュー） | なし                 |
    //    | Setup             | なし             | 表示（START）         |
    //    | Menu（確認待ちなし）| 表示（閉じる）   | なし                 |
    //    | Menu（確認待ち）   | 表示（閉じる）   | 表示（確定）          |
    //    | History / About   | 表示（Menu へ）  | なし                 |
    //    単独 A 長押しはどの画面でも表示しない（意味のある操作が無い）。
    //
    //    drawHoldProgress() は約 11 ms の部分再描画。5% 刻み (20 段階) で
    //    1500ms / 20 = 75ms 間隔となり、描画予算に十分な余裕がある。
    //    値が変化したときだけ呼ぶことで冗長な描画を回避する。
    // ================================================================
    {
        uint8_t holdPercent = 0;

        if (aPressed && bPressed) {
            // A+B 長押し: Active / Menu / History / About で意味がある。
            // Setup では何も起きないため表示しない。
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
            // ボタンが離されたら間引きタイマーをリセットする。
            // 次回の押下開始時に即座にログ出力するため。
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
void AppController::handleButtonEvent(input::ButtonEvent event,
                                      uint32_t /*nowMs*/) {
    const auto currentScreen = screenState_.screen();

    // ================================================================
    // Active 画面: 既存の Undo / ロック切替 + メニュー起動
    // ================================================================
    if (currentScreen == Screen::Active) {
        switch (event) {
        case input::ButtonEvent::UndoRequested: {
            // Undo はタッチロック中でも有効（docs/05:
            // 「タッチロック中もすべての物理ボタン操作は有効である」）。
            const bool undone = domain::undoLast(state_);
            if (undone) {
                // 成功: 確定と同等の振動で「操作が成立した」ことを伝える (40ms)
                haptics_.pulse(kVibUndoSuccessMs);

                // 両プレイヤーの数字を再描画する。
                // なぜ両方か: undoLast はどちらのプレイヤーのライフを戻したのか
                // 呼び出し側に返さないため、安全側に倒して両方を更新する。
                // drawLife は各約 4.5 ms なので 2 回呼んでも予算内（docs/07）。
                renderer_.drawLife(state_, PlayerId::Top, 0);
                renderer_.drawLife(state_, PlayerId::Bottom, 0);

                // Undo 後の状態を NVS に永続化する。
                storage_.save(state_);
            } else {
                // 失敗（履歴空）: 最短パルスで「無効操作」を伝える (20ms)。
                // docs/05 の開始禁止領域と同じパルス長で一貫性を保つ。
                haptics_.pulse(kVibUndoFailMs);
            }
            break;
        }

        case input::ButtonEvent::LockToggleRequested: {
            // タッチロックをトグルする
            state_.touchLocked = !state_.touchLocked;

            if (state_.touchLocked) {
                // ロック時: 長めのパルスで「重要な状態変更」を伝える (80ms)。
                // 確定 (40ms) より長く、ライフ 0 (120ms) より短い位置に置くことで
                // 操作の重要度の階層を維持する。
                haptics_.pulse(kVibLockMs);

                // ロックした瞬間に進行中のジェスチャーがあれば破棄する。
                // なぜ: 途中まで累積した角度が残ると、解除後の最初のタッチで
                // 意図しない確定が起きうるため。
                cancelOngoingGesture();
            } else {
                // ロック解除: 通常の確定と同等の振動 (40ms)。
                // ロック (80ms) より短くすることで「解放された」軽さを表現する。
                haptics_.pulse(kVibUnlockMs);

                // ロック中タッチ警告のフラグをクリアする
                lockTouchWarned_ = false;
            }

            // ロック状態の描画を更新する
            renderer_.drawLockState(state_);
            break;
        }

        case input::ButtonEvent::MenuRequested: {
            // メニューを開く前に進行中のジェスチャーを破棄する。
            // そうしないと、途中まで累積した角度がメニュー復帰後に
            // 意図しない確定として適用されてしまう。
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
    // Active 以外（Setup / Menu / History / About）:
    // ボタンイベントを ScreenState に委譲する。
    // Undo やロック切替は Active 専用なので、ここでは発動しない。
    // メニュー操作中に誤って試合状態が変わることを防ぐ。
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
        // B 長押し → 確認を確定（Rematch / NewGame）
        action = screenState_.onLongPressB();
        break;

    case input::ButtonEvent::MenuRequested:
        // A+B 長押し → メニューを閉じる等
        // Setup でジェスチャーが進行中なら破棄する
        cancelOngoingGesture();
        action = screenState_.onCloseMenu();
        break;

    case input::ButtonEvent::ALongPressed:
    case input::ButtonEvent::None:
        // 何もしない
        break;
    }

    // SetLife 選択による Setup 遷移を検出する。
    // ScreenAction は None が返るが、画面が Setup に変わっている。
    // 現在のプレイライフを setupLife に写さないと前回の設定値が残り、
    // ユーザーが前回の Setup で設定した値が表示されてしまう。
    if (screenState_.screen() == Screen::Setup && prevScreen != Screen::Setup) {
        screenState_.setSetupLife(PlayerId::Top,
                                 state_.players[0].life);
        screenState_.setSetupLife(PlayerId::Bottom,
                                 state_.players[1].life);
    }

    if (action != ScreenAction::None) {
        executeScreenAction(action);
    }
}

// ============================================================
// ScreenAction の実行
// ScreenState の入力メソッドが返したアクションをドメイン層に反映し、
// 必要な描画を即座に行う。
// ============================================================
void AppController::executeScreenAction(ScreenAction action) {
    switch (action) {
    case ScreenAction::StartMatch:
        // Setup で確定。setupLife の値で試合を開始する
        domain::startMatch(state_,
                           screenState_.setupLife(PlayerId::Top),
                           screenState_.setupLife(PlayerId::Bottom));
        screenState_.enterActive();
        renderer_.drawAll(state_);
        // enterActive が dirty を立てるので、consumeDirty で二重描画しないよう消費する
        screenState_.consumeDirty();
        // 試合開始時の状態を NVS に永続化する。
        storage_.save(state_);
        break;

    case ScreenAction::Rematch:
        // 同じ開始ライフでやり直す
        domain::rematch(state_);
        screenState_.enterActive();
        renderer_.drawAll(state_);
        screenState_.consumeDirty();
        // Rematch 後の状態を NVS に永続化する。
        storage_.save(state_);
        break;

    case ScreenAction::NewGame:
        // Setup に遷移する。開始ライフ（現在のプレイライフではない）を写す。
        // なぜ startingLife か: ユーザーが前回のゲームで設定した開始値を
        // ベースに調整したいため。現在のプレイライフは試合中の増減で変動しており、
        // 設定のベースとしては不適切。
        // ScreenState 側は既に Screen::Setup へ遷移しているはず。
        screenState_.setSetupLife(PlayerId::Top,
                                 state_.players[0].startingLife);
        screenState_.setSetupLife(PlayerId::Bottom,
                                 state_.players[1].startingLife);
        // setSetupLife が dirty を立てるので、consumeDirty → drawSetup で反映される
        // ここでは storage_.save() を呼ばない。NewGame は試合開始前（Setup 遷移）であり、
        // state_.active が前試合の true のまま保存すると、電源 OFF→ON で前試合に戻ってしまう。
        // 新しい試合は StartMatch 確定時に保存される。
        break;

    case ScreenAction::SwapSides:
        // 上下プレイヤーを入れ替える。メニュー表示のまま。
        domain::swapSides(state_);
        // 確定の振動を鳴らして操作成功を伝える
        haptics_.pulse(kVibConfirmMs);
        // SwapSides 後の状態を NVS に永続化する。
        storage_.save(state_);
        break;

    case ScreenAction::None:
        break;
    }
}

// ============================================================
// 進行中のジェスチャーの破棄
// ============================================================
void AppController::cancelOngoingGesture() {
    if (gesture_.state() == input::GestureState::Idle) return;

    // リングハイライトが点灯中なら消灯する
    if (gesture_.state() == input::GestureState::Active) {
        renderer_.drawRingHighlight(gesture_.preview().player, false);
    }

    // プレビュー中なら元のライフ値に戻す
    const auto preview = gesture_.preview();
    if (preview.active) {
        if (screenState_.screen() == Screen::Setup) {
            // Setup: setupLife をベースにした仮 MatchState で元の値を描画する
            domain::MatchState setupDisplay{};
            setupDisplay.players[0].life =
                screenState_.setupLife(PlayerId::Top);
            setupDisplay.players[1].life =
                screenState_.setupLife(PlayerId::Bottom);
            renderer_.drawLife(setupDisplay, preview.player, 0);
        } else {
            renderer_.drawLife(state_, preview.player, 0);
        }
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
void AppController::drawCurrentScreen(uint32_t /*nowMs*/) {
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
        // drawMenu は consumeDirty() が true のときだけ呼ばれる（全画面転送）ため、
        // 毎ループではなく画面遷移やカーソル移動時の頻度に限定される。
        // I2C 読み取りのコスト（数 ms）は全画面転送（44.6 ms）に対して十分小さい。
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
    }
}

}  // namespace counter::app
