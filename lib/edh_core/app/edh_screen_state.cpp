#include "app/edh_screen_state.hpp"

#include "app_config.hpp"

namespace counter::edh::app {

void EdhScreenState::reset() {
    screen_        = Screen::Setup;
    menuIndex_     = 0;
    confirming_    = false;
    confirmTarget_ = MenuItem::Resume;
    setupLife_     = kDefaultLife;
    resetViewStates();
    dirty_ = true;
}

Screen EdhScreenState::screen() const {
    return screen_;
}

uint8_t EdhScreenState::menuIndex() const {
    return menuIndex_;
}

MenuItem EdhScreenState::menuItem() const {
    return static_cast<MenuItem>(menuIndex_);
}

bool EdhScreenState::awaitingConfirm() const {
    return confirming_;
}

MenuItem EdhScreenState::confirmTarget() const {
    return confirmTarget_;
}

uint32_t EdhScreenState::setupLife() const {
    return setupLife_;
}

void EdhScreenState::setSetupLife(uint32_t life) {
    setupLife_ = life;
    markDirty();
}

uint8_t EdhScreenState::sensitivityIndex() const {
    return sensitivityIndex_;
}

void EdhScreenState::setSensitivityIndex(uint8_t index) {
    sensitivityIndex_ = index;
    markDirty();
}

PlayerView EdhScreenState::playerView(uint8_t playerIndex) const {
    return playerViews_[playerIndex];
}

uint8_t EdhScreenState::cmdDamageViewPlayer() const {
    return cmdDamageViewPlayer_;
}

uint8_t EdhScreenState::selectedSource() const {
    return selectedSource_;
}

void EdhScreenState::onInnerTap(uint8_t playerIndex, uint32_t nowMs) {
    if (screen_ != Screen::Active) return;

    lastActivityMs_ = nowMs;

    if (cmdDamageViewPlayer_ == kSourceNone) {
        // 誰も CmdDamageView を開いていない → 対象プレイヤーの CmdDamageView を開く
        playerViews_[playerIndex] = PlayerView::CmdDamageView;
        cmdDamageViewPlayer_ = playerIndex;
        selectedSource_ = kSourceNone;
        markDirty();
    } else if (cmdDamageViewPlayer_ == playerIndex) {
        // 自分が CmdDamageView を開いている → LifeView に戻す（再タップ）
        playerViews_[playerIndex] = PlayerView::LifeView;
        cmdDamageViewPlayer_ = kSourceNone;
        selectedSource_ = kSourceNone;
        markDirty();
    } else {
        // 他プレイヤーが CmdDamageView を開いている →
        // このタップは「被弾元選択」として解釈される。
        // selectSource() で処理すべきだが、onInnerTap() が呼ばれた場合は
        // アプリ層が「自分の扇形か他の扇形か」を判断して呼び分ける前提。
        // ここでは自分の扇形のタップ以外は無視する。
        // （他扇形のタップは selectSource() 経由で呼ばれる）
    }
}

void EdhScreenState::selectSource(uint8_t sourceIndex, uint32_t nowMs) {
    if (screen_ != Screen::Active) return;
    if (cmdDamageViewPlayer_ == kSourceNone) return;
    if (sourceIndex == cmdDamageViewPlayer_) return;  // 自分自身は選択できない

    lastActivityMs_ = nowMs;
    selectedSource_ = sourceIndex;
    markDirty();
}

void EdhScreenState::checkTimeout(uint32_t nowMs) {
    if (screen_ != Screen::Active) return;
    if (cmdDamageViewPlayer_ == kSourceNone) return;

    if (nowMs - lastActivityMs_ >= kViewTimeoutMs) {
        // タイムアウト: CmdDamageView を閉じて LifeView に復帰する
        playerViews_[cmdDamageViewPlayer_] = PlayerView::LifeView;
        cmdDamageViewPlayer_ = kSourceNone;
        selectedSource_ = kSourceNone;
        markDirty();
    }
}

void EdhScreenState::notifyActivity(uint32_t nowMs) {
    lastActivityMs_ = nowMs;
}

// --- ボタン入力ハンドラ ---

ScreenAction EdhScreenState::onNext() {
    switch (screen_) {

    case Screen::Setup: {
        // 初期ライフのプリセットをトグルする（20 ↔ 40）
        const uint32_t next = (setupLife_ == 20) ? 40u : 20u;
        setupLife_ = next;
        markDirty();
        return ScreenAction::None;
    }

    case Screen::Menu:
        confirming_ = false;
        menuIndex_ = (menuIndex_ + 1) % kMenuItemCount;
        markDirty();
        return ScreenAction::None;

    case Screen::Sensitivity: {
        sensitivityIndex_ =
            (sensitivityIndex_ + 1) % counter::config::kSensitivityPresetCount;
        markDirty();
        return ScreenAction::None;
    }

    default:
        return ScreenAction::None;
    }
}

ScreenAction EdhScreenState::onSelect() {
    switch (screen_) {

    case Screen::Setup:
        // 誤操作防止: 確定は長押し（onLongPressB）のみ
        return ScreenAction::None;

    case Screen::Menu: {
        const auto item = static_cast<MenuItem>(menuIndex_);
        switch (item) {
        case MenuItem::Resume:
            screen_ = Screen::Active;
            markDirty();
            return ScreenAction::None;

        case MenuItem::History:
            screen_ = Screen::History;
            markDirty();
            return ScreenAction::None;

        case MenuItem::SetLife:
            screen_ = Screen::Setup;
            markDirty();
            return ScreenAction::None;

        case MenuItem::SetSensitivity:
            screen_ = Screen::Sensitivity;
            markDirty();
            return ScreenAction::None;

        case MenuItem::Rematch:
            confirming_    = true;
            confirmTarget_ = MenuItem::Rematch;
            markDirty();
            return ScreenAction::None;

        case MenuItem::About:
            screen_ = Screen::About;
            markDirty();
            return ScreenAction::None;
        }
        return ScreenAction::None;
    }

    case Screen::History:
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::About:
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::Sensitivity:
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    default:
        return ScreenAction::None;
    }
}

ScreenAction EdhScreenState::onLongPressB() {
    switch (screen_) {

    case Screen::Setup:
        screen_ = Screen::Active;
        resetViewStates();
        markDirty();
        return ScreenAction::StartMatch;

    case Screen::Menu:
        if (!confirming_) {
            return ScreenAction::None;
        }
        confirming_ = false;
        if (confirmTarget_ == MenuItem::Rematch) {
            screen_ = Screen::Active;
            resetViewStates();
            markDirty();
            return ScreenAction::Rematch;
        }
        return ScreenAction::None;

    default:
        return ScreenAction::None;
    }
}

ScreenAction EdhScreenState::onCloseMenu() {
    switch (screen_) {

    case Screen::Setup:
        return ScreenAction::None;

    case Screen::Active:
        screen_    = Screen::Menu;
        menuIndex_ = 0;
        confirming_ = false;
        markDirty();
        return ScreenAction::None;

    case Screen::Menu:
        confirming_ = false;
        screen_     = Screen::Active;
        markDirty();
        return ScreenAction::None;

    case Screen::History:
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::About:
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::Sensitivity:
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;
    }
    return ScreenAction::None;
}

void EdhScreenState::enterActive() {
    screen_ = Screen::Active;
    markDirty();
}

bool EdhScreenState::consumeDirty() {
    const bool wasDirty = dirty_;
    dirty_ = false;
    return wasDirty;
}

void EdhScreenState::markDirty() {
    dirty_ = true;
}

void EdhScreenState::resetViewStates() {
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        playerViews_[i] = PlayerView::LifeView;
    }
    cmdDamageViewPlayer_ = kSourceNone;
    selectedSource_ = kSourceNone;
}

}  // namespace counter::edh::app
