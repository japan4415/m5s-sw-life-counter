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
#include <esp_heap_caps.h>

#include "app_config.hpp"
#include "domain/edh_life_change.hpp"
#include "domain/edh_match_state.hpp"
#include "domain/edh_life_service.hpp"
#include "app/edh_screen_state.hpp"
#include "input/edh_touch_zone.hpp"
#include "input/touch_zone.hpp"  // angleDegrees() for EDH start angle validation

namespace counter::app {

// ============================================================
// 診断用シリアルログ。
// 実機デバッグが終わったら EDH_DEBUG_LOG を 0 に変更して無効化する。
// ログ書式は FaB 版 (APP_DEBUG_LOG) と同じ機械解析向けフォーマット:
//   EDH_LOOP,<nowMs>                  — update() 冒頭（ループ生存確認）
//   EDH_BTN,<EventName>,<Screen>      — ボタンイベント発火時
//   EDH_SCREEN,<Old>,<New>            — 画面遷移時
//   EDH_TDOWN,zone=<inner/ring>,sector=<n>,r=<radius> — タッチダウン位置
//   EDH_TUP,sector=<n>,moved=<px>,elapsed=<ms>,isTap=<0/1> — タッチアップ判定
//   EDH_TAP,sector=<n>,cmdViewPlayer=<n>,selectedSource=<n> — タップ成立
//   EDH_VIEW,player=<n>,cmdViewPlayer=<n>,selectedSource=<n> — ビュー状態遷移
//   EDH_SRC,player=<n>,source=<n>     — 被弾元選択
//   EDH_CMDSLIDE,player=<n>,source=<n>,steps=<n>,applied=<0/1> — スライド確定
//   EDH_CMDAPPLY,player=<n>,source=<n>,delta=<n>,before=<n>,after=<n>,life=<n>
// ============================================================
#ifndef EDH_DEBUG_LOG
#define EDH_DEBUG_LOG 1
#endif

#if EDH_DEBUG_LOG
namespace {

const char* edhButtonEventName(input::ButtonEvent e) {
    switch (e) {
    case input::ButtonEvent::None:                return "None";
    case input::ButtonEvent::UndoRequested:       return "Undo";
    case input::ButtonEvent::LockToggleRequested: return "LockToggle";
    case input::ButtonEvent::MenuRequested:       return "Menu";
    case input::ButtonEvent::ALongPressed:        return "ALong";
    case input::ButtonEvent::BLongPressed:        return "BLong";
    }
    return "?";
}

const char* edhScreenName(edh::app::Screen s) {
    switch (s) {
    case edh::app::Screen::Setup:       return "Setup";
    case edh::app::Screen::Active:      return "Active";
    case edh::app::Screen::Menu:        return "Menu";
    case edh::app::Screen::History:     return "History";
    case edh::app::Screen::About:       return "About";
    case edh::app::Screen::Sensitivity: return "Sensitivity";
    }
    return "?";
}

}  // namespace
#endif

// ============================================================
// 振動パターンの持続時間 (ms)
// FaB 版と共通のパルス長は lib/counter_core/app_config.hpp の
// counter::config::kVib* に集約した。ここには EDH 固有のパターンのみ残す。
// ============================================================
namespace {
constexpr uint32_t kVibCmdDmg21Ms = 120;  // 統率者ダメージ 21 到達の強い振動

constexpr uint32_t kVibTapMs      = 20;   // 内側タップのフィードバック
}  // namespace

// ============================================================
// begin — 初期化
// ============================================================

void EdhAppController::begin() {
    // ================================================================
    // 二分法の実験ビルド (EDH_BISECT)
    //
    // -DEDH_BISECT=1: renderer_.begin() を呼ばない。update() は生入力ログのみ。
    //                 → 入力が生きれば renderer_.begin() が原因。
    // -DEDH_BISECT=2: renderer_.begin() は呼ぶ。update() は生入力ログのみ。
    //                 → 入力が生きれば update() 内の処理が原因。
    // -DEDH_BISECT=3 または未定義: 通常動作。
    // ================================================================

#if EDH_DEBUG_LOG
    Serial.printf("EDH_HEAP,phase=pre_renderer,"
                  "freeHeap=%lu,intFree=%lu,intMaxBlock=%lu,freePsram=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(ESP.getFreePsram()));
#endif

#if !defined(EDH_BISECT) || EDH_BISECT >= 2
    renderer_.begin();
#else
    Serial.println("EDH_BISECT=1: renderer_.begin() skipped");
#endif

#if EDH_DEBUG_LOG
    Serial.printf("EDH_HEAP,phase=post_renderer,"
                  "freeHeap=%lu,intFree=%lu,intMaxBlock=%lu,freePsram=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(ESP.getFreePsram()));
#endif

    haptics_.begin();
    storage_.begin();

#if EDH_DEBUG_LOG
    Serial.printf("EDH_HEAP,phase=post_init,"
                  "freeHeap=%lu,intFree=%lu,intMaxBlock=%lu,freePsram=%lu,"
                  "board=%d\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(ESP.getFreePsram()),
                  static_cast<int>(M5.getBoard()));
#endif

#if !defined(EDH_BISECT) || EDH_BISECT >= 3
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
#else
    Serial.printf("EDH_BISECT=%d: begin() setup/draw skipped\n", EDH_BISECT);
#endif
}

// ============================================================
// update — メインループ
// ============================================================

void EdhAppController::update(uint32_t nowMs) {
    // ================================================================
    // 生入力ログ（全 BISECT レベルで出力する）
    // ================================================================
#if EDH_DEBUG_LOG
    static uint32_t lastRawLogMs = 0;
    if (nowMs - lastRawLogMs >= 1000) {
        const bool rawA = M5.BtnA.isPressed();
        const bool rawB = M5.BtnB.isPressed();
        const auto tc = M5.Touch.getCount();
        int16_t tx = -1, ty = -1;
        if (tc > 0) {
            const auto td = M5.Touch.getDetail(0);
            if (td.isPressed()) { tx = td.x; ty = td.y; }
        }
        Serial.printf("EDH_RAW,btnA=%d,btnB=%d,touch=%d,x=%d,y=%d,"
                      "intFree=%lu,ms=%lu\n",
                      rawA, rawB, tc, tx, ty,
                      static_cast<unsigned long>(
                          heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned long>(nowMs));
        lastRawLogMs = nowMs;
    }
#endif

    // ================================================================
    // 二分法: BISECT=1,2 では生入力ログだけ出して即 return
    // ================================================================
#if defined(EDH_BISECT) && EDH_BISECT <= 2
    return;
#endif

    // ================================================================
    // 0. 物理ボタンの処理
    // ================================================================
    const bool aPressed = M5.BtnA.isPressed();
    const bool bPressed = M5.BtnB.isPressed();
    const auto buttonEvent = buttonInput_.update(aPressed, bPressed, nowMs);

#if EDH_DEBUG_LOG
    const auto screenBeforeBtn = screenState_.screen();
    if (buttonEvent != input::ButtonEvent::None) {
        Serial.printf("EDH_BTN,%s,%s\n",
                      edhButtonEventName(buttonEvent),
                      edhScreenName(screenBeforeBtn));
    }
#endif

    if (buttonEvent != input::ButtonEvent::None) {
        handleButtonEvent(buttonEvent, nowMs);
    }

    const auto currentScreen = screenState_.screen();

#if EDH_DEBUG_LOG
    if (currentScreen != screenBeforeBtn) {
        Serial.printf("EDH_SCREEN,%s,%s\n",
                      edhScreenName(screenBeforeBtn),
                      edhScreenName(currentScreen));
    }
#endif

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
                haptics_.pulse(config::kVibLockTouchMs);
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

                    // 半径を計算してログに出力する
                    const float rdx = static_cast<float>(x) - config::kCenterX;
                    const float rdy = static_cast<float>(y) - config::kCenterY;
                    const float radius = std::sqrt(rdx * rdx + rdy * rdy);
                    const uint8_t sector = edh::selectSector(x, y);

#if EDH_DEBUG_LOG
                    Serial.printf("EDH_TDOWN,zone=%s,sector=%u,r=%d\n",
                                  outerRing ? "ring" : "inner",
                                  static_cast<unsigned>(sector),
                                  static_cast<int>(radius));
#endif

                    if (outerRing) {
                        // 外周リング: GestureDetector にタッチを渡す
                        slidePlayerIndex_ = sector;

                        // CmdDamageView が開いている場合、スライド開始扇形で
                        // 被弾元を決定する。開いているプレイヤー以外の扇形から
                        // スライドを開始すると、その扇形が被弾元になる。
                        {
                            const uint8_t cmdVP =
                                screenState_.cmdDamageViewPlayer();
                            if (cmdVP != edh::kSourceNone &&
                                sector != cmdVP) {
                                screenState_.selectSource(sector, nowMs);
#if EDH_DEBUG_LOG
                                Serial.printf(
                                    "EDH_SRC,player=%u,source=%u\n",
                                    static_cast<unsigned>(cmdVP),
                                    static_cast<unsigned>(sector));
#endif
                            }
                        }

                        // EDH 用の開始角度判定:
                        // 扇形境界（対角線）付近の不感帯をチェックする。
                        // FaB 版の isValidStartAngle() は 0°/180° 付近を禁止
                        // するが、EDH では代わりに 45°/135°/225°/315° 付近を
                        // 禁止する。
                        const float startAngle = input::angleDegrees(x, y);
                        if (!edh::isValidStartAngleEdh(startAngle)) {
                            // 不感帯内: 開始を拒否し、警告振動を鳴らす
                            haptics_.pulse(config::kVibRejectMs);
                            innerTouchStarted_ = false;
                        } else {
                            // P2/P4 セクターでは GestureDetector 内部の
                            // FaB 用禁止領域を迂回するため座標を 90° 回転させる。
                            // 回転は角度差分を保存するためライフ計算に影響しない。
                            rotatingCoords_ = edh::needsCoordinateRotation(
                                slidePlayerIndex_);

                            int16_t gx = x, gy = y;
                            if (rotatingCoords_) {
                                edh::rotateCCW90(x, y, gx, gy);
                            }

                            gesture_.onTouchDown(gx, gy, nowMs);

                            if (gesture_.state() ==
                                input::GestureState::Candidate) {
                                haptics_.beginGesture();
                                haptics_.pulse(config::kVibStartMs);
                            }
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
                        haptics_.pulse(config::kVibStartMs);
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
                    // P2/P4 の座標回転を onTouchMove にも一貫して適用する。
                    // onTouchDown と同じ回転を適用しないと角度差分が狂う。
                    int16_t gx = x, gy = y;
                    if (rotatingCoords_) {
                        edh::rotateCCW90(x, y, gx, gy);
                    }
                    gesture_.onTouchMove(gx, gy, nowMs);
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
        haptics_.pulse(config::kVibRejectMs);
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

                // 統率者ダメージスライドの判定:
                // CmdDamageView が開いていて、かつスライドがビューを
                // 開いているプレイヤー**以外**の扇形から始まった場合。
                // 被弾元はスライド開始扇形 (pi) で自動決定される。
                const uint8_t cmdViewPlayer = screenState_.cmdDamageViewPlayer();
                const bool isCmdDmgSlide =
                    (cmdViewPlayer != edh::kSourceNone) &&
                    (pi != cmdViewPlayer);

                if (isCmdDmgSlide) {
                    // 統率者ダメージのプレビュー:
                    // ビューを開いているプレイヤー (cmdViewPlayer) の扇形を
                    // 再描画して、pi からのダメージ増減を表示する。
                    renderer_.drawPlayerSector(
                        state_, screenState_, cmdViewPlayer,
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
                const uint8_t cmdViewPlayer =
                    screenState_.cmdDamageViewPlayer();
                const bool wasCmdDmg =
                    (cmdViewPlayer != edh::kSourceNone) &&
                    (slidePlayerIndex_ != cmdViewPlayer);

                if (wasCmdDmg) {
                    // 統率者ダメージプレビューのキャンセル:
                    // ビュー表示プレイヤーの扇形を元に戻す
                    renderer_.drawPlayerSector(
                        state_, screenState_, cmdViewPlayer, 0, false);
                    screenState_.clearSource();
                } else {
                    renderer_.drawPlayerSector(
                        state_, screenState_, slidePlayerIndex_, 0, false);
                }
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
            haptics_.pulse(config::kVibConfirmMs);
        } else {
            // Active: ライフ変更 or 統率者ダメージの確定
            const uint8_t pi = slidePlayerIndex_;
            const uint8_t cmdViewPlayer = screenState_.cmdDamageViewPlayer();

            // 統率者ダメージスライドの判定:
            // CmdDamageView が開いていて、スライドが開いているプレイヤー
            // **以外**の扇形から始まった場合。被弾元 = pi（スライド開始扇形）。
            const bool isCmdDmgSlide =
                (cmdViewPlayer != edh::kSourceNone) &&
                (pi != cmdViewPlayer);

#if EDH_DEBUG_LOG
            Serial.printf("EDH_CMDSLIDE,player=%u,source=%u,steps=%d,applied=%d\n",
                          static_cast<unsigned>(
                              isCmdDmgSlide ? cmdViewPlayer : pi),
                          static_cast<unsigned>(
                              isCmdDmgSlide ? pi : edh::kSourceNone),
                          static_cast<int>(result.deltaLife),
                          isCmdDmgSlide ? 1 : 0);
#endif

            if (isCmdDmgSlide) {
                // 統率者ダメージ操作（ライフ連動あり）
                // 対象プレイヤー = cmdViewPlayer（ビューを開いている人）
                // 被弾元 = pi（スライド開始扇形のプレイヤー）
                const uint8_t targetPlayer = cmdViewPlayer;
                const uint8_t srcIdx = pi;
                const uint8_t dmgBefore =
                    state_.players[targetPlayer].commanderDamageFrom[srcIdx];
                edh::applyCommanderDamage(
                    state_, targetPlayer, srcIdx,
                    static_cast<int16_t>(result.deltaLife), nowMs);
                const uint8_t dmgAfter =
                    state_.players[targetPlayer].commanderDamageFrom[srcIdx];

#if EDH_DEBUG_LOG
                Serial.printf("EDH_CMDAPPLY,player=%u,source=%u,delta=%d,"
                              "before=%u,after=%u,life=%u\n",
                              static_cast<unsigned>(targetPlayer),
                              static_cast<unsigned>(srcIdx),
                              static_cast<int>(result.deltaLife),
                              static_cast<unsigned>(dmgBefore),
                              static_cast<unsigned>(dmgAfter),
                              static_cast<unsigned>(
                                  state_.players[targetPlayer].life));
#endif

                // 統率者ダメージ 21 到達チェック
                if (dmgAfter >= 21) {
                    haptics_.pulse(kVibCmdDmg21Ms);
                } else if (state_.players[targetPlayer].life == 0) {
                    haptics_.pulse(config::kVibLifeZeroMs);
                } else {
                    haptics_.pulse(config::kVibConfirmMs);
                }

                // ビューを開いているプレイヤーの扇形を再描画する
                // （統率者ダメージ一覧の数値が変化するため）
                renderer_.drawPlayerSector(
                    state_, screenState_, targetPlayer, 0, false);
            } else {
                // 通常ライフ操作
                edh::applyLifeChange(
                    state_, pi,
                    static_cast<int16_t>(result.deltaLife), nowMs);

                if (state_.players[pi].life == 0) {
                    haptics_.pulse(config::kVibLifeZeroMs);
                } else {
                    haptics_.pulse(config::kVibConfirmMs);
                }

                // スライド対象プレイヤーの扇形を再描画する
                renderer_.drawPlayerSector(
                    state_, screenState_, pi, 0, false);
            }

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
                haptics_.pulse(config::kVibUndoSuccessMs);
                // 全プレイヤーの扇形を再描画する（Undo は任意のプレイヤーに影響しうる）
                renderer_.drawAll(state_, screenState_);
                storage_.save(state_);
            } else {
                haptics_.pulse(config::kVibUndoFailMs);
            }
            break;
        }
        case input::ButtonEvent::LockToggleRequested: {
            state_.touchLocked = !state_.touchLocked;
            if (state_.touchLocked) {
                cancelOngoingGesture();
                haptics_.pulse(config::kVibLockMs);
            } else {
                haptics_.pulse(config::kVibUnlockMs);
            }
            renderer_.drawLockState(state_);
            storage_.save(state_);
            break;
        }
        case input::ButtonEvent::MenuRequested: {
            // A+B 長押し: メニューを開く（FaB 版と同じ）
            cancelOngoingGesture();
            const auto action = screenState_.onCloseMenu();
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

    // タッチアップのログ: 移動量・経過時間・判定結果を出力する。
    // handleInnerTap が呼ばれるのは内側領域でタッチが開始された場合のみ。
    const uint8_t upSector = edh::selectSector(tapStartX_, tapStartY_);
    const bool isTap = (moveSq <= maxMoveSq && elapsed <= edh::kTapMaxDurationMs);

#if EDH_DEBUG_LOG
    // moveSq の平方根を整数で近似する（ログの可読性のため）
    int32_t movePx = 0;
    {
        int32_t s = moveSq;
        int32_t r = 0;
        while (r * r < s) ++r;
        movePx = r;
    }
    Serial.printf("EDH_TUP,sector=%u,moved=%d,elapsed=%lu,isTap=%d\n",
                  static_cast<unsigned>(upSector),
                  static_cast<int>(movePx),
                  static_cast<unsigned long>(elapsed),
                  isTap ? 1 : 0);
#endif

    if (!isTap) {
        return false;  // タップではない（ドラッグ等）
    }

    // タップが成立: タッチ開始位置の扇形を判定する
    const uint8_t tappedSector = upSector;

    // EdhScreenState の onInnerTap を呼ぶ
    // onInnerTap は排他制御のみを行う。
    // 振り分けはアプリ層の責務（仕様書の申し送り事項）。

    const uint8_t cmdViewPlayer = screenState_.cmdDamageViewPlayer();

#if EDH_DEBUG_LOG
    Serial.printf("EDH_TAP,sector=%u,cmdViewPlayer=%u,selectedSource=%u\n",
                  static_cast<unsigned>(tappedSector),
                  static_cast<unsigned>(cmdViewPlayer),
                  static_cast<unsigned>(screenState_.selectedSource()));
#endif

    if (cmdViewPlayer == edh::kSourceNone) {
        // 誰も CmdDamageView を開いていない → 自分の扇形をトグル
        screenState_.onInnerTap(tappedSector, nowMs);
        haptics_.pulse(kVibTapMs);
    } else if (tappedSector == cmdViewPlayer) {
        // CmdDamageView を開いているプレイヤー自身をタップ → ライフビューに戻す
        screenState_.onInnerTap(tappedSector, nowMs);
        haptics_.pulse(kVibTapMs);
    } else {
        // 他扇形をタップ → 被弾元はスライドで決まるため、タップは無視する
#if EDH_DEBUG_LOG
        Serial.printf("EDH_TAP_IGNORE,sector=%u,cmdViewPlayer=%u\n",
                      static_cast<unsigned>(tappedSector),
                      static_cast<unsigned>(cmdViewPlayer));
#endif
        return true;  // タップ自体は成立（ジェスチャーには渡さない）
    }

    // ビュー状態遷移のログ
#if EDH_DEBUG_LOG
    Serial.printf("EDH_VIEW,player=%u,cmdViewPlayer=%u,selectedSource=%u\n",
                  static_cast<unsigned>(tappedSector),
                  static_cast<unsigned>(screenState_.cmdDamageViewPlayer()),
                  static_cast<unsigned>(screenState_.selectedSource()));
#endif

    // タップされた扇形を再描画する
    renderer_.drawPlayerSector(state_, screenState_, tappedSector, 0, false);

    // CmdDamageView が開いている場合、そのプレイヤーの扇形も再描画する
    // （被弾元選択の反映のため）
    const uint8_t newCmdViewPlayer = screenState_.cmdDamageViewPlayer();
    if (newCmdViewPlayer != edh::kSourceNone &&
        tappedSector != newCmdViewPlayer) {
        renderer_.drawPlayerSector(
            state_, screenState_, newCmdViewPlayer, 0, false);
    }

    // handleInnerTap が必要な扇形をすべて部分再描画済みのため、
    // onInnerTap/selectSource が立てた dirty フラグを消費する。
    // これにより update() の step 7 で冗長な drawAll() が走るのを防ぎ、
    // 全画面再描画 (~100-200ms) のブロックで次のタッチを取りこぼす問題を解消する。
    screenState_.consumeDirty();

    return true;
}

}  // namespace counter::app
