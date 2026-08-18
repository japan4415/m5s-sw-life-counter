#pragma once

// Phase 1 + Phase 2 Wave 1-2: ライフカウンター本体 -- アプリケーション制御層
//
// 外周スライドジェスチャーによるライフ増減の統合制御を行う。
// Phase 2 で追加: Undo（物理ボタン A）、タッチロック（物理ボタン B）、
// 画面遷移管理（Setup / Active / Menu / History / About）、
// ボタン入力の画面別ルーティング、メニュー操作、初期ライフ設定。
//
// docs/07-architecture.md のレイヤ構成に従い、AppController は最上位に位置し、
// domain / input / ui / infra の各層を統合する。

#include "app/screen_state.hpp"
#include "domain/match_state.hpp"
#include "input/button_input.hpp"
#include "input/gesture_detector.hpp"
#include "infra/haptics_m5.hpp"
#include "ui/renderer.hpp"

namespace counter::app {

class AppController {
public:
    void begin();

    /// メインループから毎フレーム呼ばれる。
    /// nowMs は main.cpp の millis() から渡される値。
    /// 内部で millis() を呼ばないことで、時刻源を main に集約する。
    void update(uint32_t nowMs);

private:
    domain::MatchState state_{};
    ScreenState screenState_;
    input::GestureDetector gesture_;
    input::ButtonInput buttonInput_;
    ui::Renderer renderer_;
    infra::Haptics haptics_;

    // --- タッチ状態の立ち上がり／立ち下がり検出 ---
    // 前フレームの押下状態と座標を保持し、エッジ検出に使う。
    // M5.Touch は押下状態しか提供しないため、自前で差分を取る必要がある。
    bool prevTouching_ = false;
    int16_t prevTouchX_ = 0;
    int16_t prevTouchY_ = 0;

    // --- プレビュー変化検出 ---
    // 毎フレーム描画を避けるため、前回のプレビュー状態を保持して差分のみ描画する。
    // 部分再描画でも約 5.0 ms かかるため（docs/07 実測）、変化がないのに呼ぶと無駄になる。
    input::GesturePreview prevPreview_{};
    input::GestureState prevGestureState_ = input::GestureState::Idle;

    // --- ロック中タッチ警告の連発防止 ---
    // ロック中に画面に触れた瞬間に1回だけ警告振動を鳴らすためのフラグ。
    // タッチが継続している間は再度鳴らさず、指を離してから再タッチしたときだけ鳴る。
    bool lockTouchWarned_ = false;

    // --- 長押し進捗の再描画抑制 ---
    // 全画面を通じて、holdPercent が前回と同値なら drawHoldProgress を呼ばない。
    // drawHoldProgress は約 11 ms の部分再描画を行うため、
    // 値が変化したときだけ呼ぶことで描画予算を節約する。
    // 全画面描画 (consumeDirty 起点) 後は 0 にリセットして再描画を強制する。
    uint8_t prevHoldPercent_ = 0;

    // --- 診断ログの間引き用 ---
    // APP_DEBUG_LOG 有効時に heldMs() の出力を 200ms 間隔に間引くための時刻。
    // 診断が終わったら APP_DEBUG_LOG を 0 に設定してログごと無効化する。
    uint32_t lastHeldLogMs_ = 0;

    // --- スリープ保留フラグ ---
    // ScreenAction::Sleep を受けた時点ではボタンが押されたままなので、
    // 即座にスリープに入ると押下中のボタンが wakeup ソースとして即復帰してしまう。
    // このフラグを立てておき、次のループ以降でボタンが全て離れたことを確認してから
    // 実際にスリープ処理を実行する。delay() を使わない非ブロッキング設計。
    bool sleepPending_ = false;

    // --- スリープ復帰後の入力抑制フラグ ---
    // enterLightSleep() からの復帰直後、メニューのカーソルが Sleep 項目の上にある。
    // 復帰に使ったボタンを離した瞬間に onSelect() → Sleep が再発火して
    // 即座にスリープへ再突入してしまう。これを防ぐため、両方のボタンが離されるまで
    // ボタン・タッチ入力を一切処理しない。
    // delay() を使わない非ブロッキング設計（sleepPending_ と同じ流儀）。
    bool wakeInputSuppressed_ = false;

    // --- ボタンイベント処理 ---
    void handleButtonEvent(input::ButtonEvent event, uint32_t nowMs);

    /// ScreenAction をドメイン層に反映する。
    /// ScreenState の入力メソッドが返した動作を実行する。
    void executeScreenAction(ScreenAction action);

    /// 進行中のジェスチャーを破棄する。
    /// メニュー起動やロック切替時に呼び出し、途中のジェスチャーが
    /// 残ることによる意図しない確定を防ぐ。
    void cancelOngoingGesture();

    /// 現在の画面に対応する描画メソッドを呼ぶ。
    /// consumeDirty() が true のときに呼ばれる想定。
    void drawCurrentScreen(uint32_t nowMs);

    /// ライトスリープに入り、ボタン押下またはタイマーで復帰する。
    /// なぜ deepSleep / powerOff / timerSleep を使わないか:
    ///   これらは RAM を失うため、永続化が未実装（Phase 3 予定）の現時点では
    ///   試合状態が消えてしまう。lightSleep は RAM を保持するので安全。
    /// GPIO 復帰が効かなかった場合に備え、タイマー復帰を必ず併用する。
    /// タイマーで復帰した場合は画面を復帰させ、スリープには戻らない。
    /// nowMs は復帰後のリセット処理に使う。
    void enterLightSleep(uint32_t nowMs);
};

}  // namespace counter::app
