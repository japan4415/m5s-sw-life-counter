// 統合ファームウェアのゲームモード選択制御の実装。
//
// 起動フロー:
//   1. modeStore_.begin() / 各モードの NVS を走査
//   2. 最終選択モードに進行中の試合があれば直接起動（自動復元を維持）
//   3. なければゲーム選択画面を表示
//
// ゲーム選択画面の描画は M5.Display への直接描画（全画面転送）とし、
// カーソル移動・選択時のみ再描画する。起動直後とボタン操作時だけの
// 描画であり、ライフ操作中のような部分再描画の予算制約は存在しない。

#include "app_launcher.hpp"

#include <M5Unified.h>

#include "app_config.hpp"
#include "infra/edh_storage_nvs.hpp"
#include "infra/riftbound_storage_nvs.hpp"
#include "infra/storage_nvs.hpp"
#include "ui/theme_common.hpp"

namespace counter::app {

namespace {

// 選択画面のレイアウト（theme_common の共通定数を利用する）
constexpr int32_t kSelectTitleY   = 150;  // "SELECT GAME" タイトル
constexpr int32_t kSelectFirstY   = 200;  // 最初の項目の中心 y
constexpr int32_t kSelectSpacing  = 36;   // 項目間の y 間隔
constexpr int32_t kSelectHintY    = 350;  // 操作説明

constexpr float kSelectTitleFontSize = 2.0f;
constexpr float kSelectItemFontSize  = 2.0f;
constexpr float kSelectHintFontSize  = 1.0f;

// 項目ラベル。infra::GameMode の列挙順と一致させる。
const char* modeLabel(infra::GameMode mode) {
    switch (mode) {
    case infra::GameMode::Fab:       return "FaB";
    case infra::GameMode::Edh:       return "MTG EDH";
    case infra::GameMode::Riftbound: return "Riftbound";
    }
    return "?";
}

}  // namespace

// ============================================================
// setControllers — モードのコントローラ登録
// ============================================================

void AppLauncher::setControllers(IAppController** controllers, uint8_t count) {
    // count は kGameModeCount（3）を想定する。過分は切り捨てる。
    controllerCount_ =
        (count > infra::kGameModeCount) ? infra::kGameModeCount : count;
    for (uint8_t i = 0; i < controllerCount_; ++i) {
        controllers_[i] = controllers[i];
    }
}

// ============================================================
// begin — 起動時のモード決定
// ============================================================

void AppLauncher::begin() {
    modeStore_.begin();
    haptics_.begin();

    // 各モードの NVS を走査して復元可能な状態を作っておく。
    // modeHasActiveMatch() はこの走査結果を参照する。
    // コントローラ側でも begin() の中で再度走査するが、
    // NvsStateStore::begin() は冪等（全スロット再スキャン）であるため問題ない。
    fabStorage_.begin();
    edhStorage_.begin();
    riftboundStorage_.begin();

    const infra::GameMode saved = modeStore_.load();

    // 進行中の試合があるモードへは確認なしで直接復帰する
    // （docs/08-persistence.md: 起動時のゲーム自動復元）。
    if (modeHasActiveMatch(saved)) {
        enterMode(saved);
        return;
    }

    // 進行中の試合がなければゲーム選択画面から開始する。
    // カーソルは最終選択モードに出しておく（多くのユーザーは
    // 同じゲームを連続して使うため）。
    cursor_ = saved;
    state_ = State::ModeSelect;
    drawModeSelect();
}

// ============================================================
// update — ループ ディスパッチ
// ============================================================

void AppLauncher::update(uint32_t nowMs) {
    if (state_ == State::Running) {
        if (controllers_[static_cast<uint8_t>(runningMode_)] != nullptr) {
            controllers_[static_cast<uint8_t>(runningMode_)]->update(nowMs);
        }

        // メニューの Switch Game による切替要求を検出したら
        // ゲーム選択画面へ戻る。
        if (controllers_[static_cast<uint8_t>(runningMode_)] != nullptr &&
            controllers_[static_cast<uint8_t>(runningMode_)]
                ->consumeSwitchRequested()) {
            showModeSelect();
        }
        return;
    }

    // --- ゲーム選択画面のボタン処理 ---
    // ButtonInput に毎ループ押下状態を渡し、確定イベントを取得する。
    // docs/05: 画面から見て左が BtnA、右が BtnB。
    const bool aPressed = M5.BtnA.isPressed();
    const bool bPressed = M5.BtnB.isPressed();
    const auto event = buttonInput_.update(aPressed, bPressed, nowMs);

    switch (event) {
    case input::ButtonEvent::UndoRequested:
        // A 短押し: カーソルを次のモードへ循環させる
        cursor_ = static_cast<infra::GameMode>(
            (static_cast<uint8_t>(cursor_) + 1) % infra::kGameModeCount);
        haptics_.pulse(config::kVibRejectMs);  // 最短パルスでカーソル移動を通知
        drawModeSelect();
        break;

    case input::ButtonEvent::LockToggleRequested:
        // B 短押し: カーソル位置のモードを起動する
        haptics_.pulse(config::kVibConfirmMs);
        enterMode(cursor_);
        break;

    case input::ButtonEvent::MenuRequested:
    case input::ButtonEvent::ALongPressed:
    case input::ButtonEvent::BLongPressed:
    case input::ButtonEvent::None:
        // 選択画面では割り当てない
        break;
    }

    haptics_.tick(nowMs);
}

// ============================================================
// enterMode — モードの起動
// ============================================================

void AppLauncher::enterMode(infra::GameMode mode) {
    runningMode_ = mode;
    modeStore_.save(mode);

    // 選択確定のパルス（B 短押し時に鳴らした 40ms）が残っている場合、
    // 選択画面側の Haptics::tick はもう呼ばれない。モーターを停止してから
    // モードへ引き渡す（駆動中の振動は各モードの Haptics が管理する）。
    M5.Power.setVibration(0);

    // 選択画面の描画状態を引きずらないよう、モード側の begin() が
    // 全画面を描き直す前提（各 begin() は drawSetup / drawAll を呼ぶ）。
    auto* controller = controllers_[static_cast<uint8_t>(mode)];
    if (controller != nullptr) {
        controller->begin();
    }
    state_ = State::Running;
}

// ============================================================
// showModeSelect — ゲーム選択画面への遷移
// ============================================================

void AppLauncher::showModeSelect() {
    // 切替直前のモードが振動中のまま残ることがある（切替要求は
    // メニュー操作中に発生するが、確定振動の最短パルス 20ms が
    // 残る可能性だけは考慮する）。振動を明示的に停止してから
    // 選択画面へ移る。
    M5.Power.setVibration(0);

    // カーソルは現在駆動中だったモードに出しておく。
    cursor_ = runningMode_;
    state_ = State::ModeSelect;
    drawModeSelect();
}

// ============================================================
// modeHasActiveMatch — 進行中試合の有無
// ============================================================

bool AppLauncher::modeHasActiveMatch(infra::GameMode mode) const {
    // 各ストアは begin() 済みであること（AppLauncher::begin() 参照）。
    switch (mode) {
    case infra::GameMode::Fab:
        return fabStorage_.hasValidState() && fabStorage_.loadedState().active;
    case infra::GameMode::Edh:
        return edhStorage_.hasValidState() && edhStorage_.loadedState().active;
    case infra::GameMode::Riftbound:
        return riftboundStorage_.hasValidState() &&
               riftboundStorage_.loadedState().active;
    }
    return false;
}

// ============================================================
// drawModeSelect — ゲーム選択画面の描画
// ============================================================

void AppLauncher::drawModeSelect() {
    auto cx = static_cast<int32_t>(config::kCenterX);

    M5.Display.fillScreen(ui::theme_common::kBgColor);
    M5.Display.setTextDatum(middle_center);

    // --- タイトル ---
    M5.Display.setTextSize(kSelectTitleFontSize);
    M5.Display.setTextColor(ui::theme_common::kHistoryTitleColor,
                            ui::theme_common::kBgColor);
    M5.Display.drawString("SELECT GAME", cx, kSelectTitleY);

    // --- モード一覧（角括弧ではなく ">" 記号でカーソルを示す。
    //     メニュー画面と同じ表記で一貫させる。色覚差対応）---
    for (uint8_t i = 0; i < infra::kGameModeCount; ++i) {
        const auto mode = static_cast<infra::GameMode>(i);
        const int32_t itemY = kSelectFirstY
                            + static_cast<int32_t>(i) * kSelectSpacing;
        const bool isSelected = (mode == cursor_);

        M5.Display.setTextSize(kSelectItemFontSize);
        M5.Display.setTextColor(
            isSelected ? ui::theme_common::kMenuSelectedColor
                       : ui::theme_common::kMenuNormalColor,
            ui::theme_common::kBgColor);

        char buf[24];
        snprintf(buf, sizeof(buf), "%s%s",
                 isSelected ? "> " : "  ", modeLabel(mode));
        M5.Display.drawString(buf, cx, itemY);
    }

    // --- 操作説明 ---
    M5.Display.setTextSize(kSelectHintFontSize);
    M5.Display.setTextColor(ui::theme_common::kHintTextColor,
                            ui::theme_common::kBgColor);
    M5.Display.drawString("A=Next  B=Select", cx, kSelectHintY);
}

}  // namespace counter::app
