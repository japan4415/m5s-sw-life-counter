#include "app/edh_screen_state.hpp"

#include "app_config.hpp"

namespace counter::edh::app {

void EdhScreenState::reset() {
    nav_.reset();
    setupLife_     = kDefaultLife;
    resetViewStates();
}

Screen EdhScreenState::screen() const {
    return nav_.screen();
}

uint8_t EdhScreenState::menuIndex() const {
    return nav_.menuIndex();
}

MenuItem EdhScreenState::menuItem() const {
    return nav_.menuItem();
}

bool EdhScreenState::awaitingConfirm() const {
    return nav_.awaitingConfirm();
}

MenuItem EdhScreenState::confirmTarget() const {
    return nav_.confirmTarget();
}

uint32_t EdhScreenState::setupLife() const {
    return setupLife_;
}

void EdhScreenState::setSetupLife(uint32_t life) {
    setupLife_ = life;
    nav_.markDirty();
}

uint8_t EdhScreenState::sensitivityIndex() const {
    return sensitivityIndex_;
}

void EdhScreenState::setSensitivityIndex(uint8_t index) {
    sensitivityIndex_ = index;
    nav_.markDirty();
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
    if (nav_.screen() != Screen::Active) return;

    lastActivityMs_ = nowMs;

    if (cmdDamageViewPlayer_ == kSourceNone) {
        // 誰も CmdDamageView を開いていない → 対象プレイヤーの CmdDamageView を開く
        playerViews_[playerIndex] = PlayerView::CmdDamageView;
        cmdDamageViewPlayer_ = playerIndex;
        selectedSource_ = kSourceNone;
        nav_.markDirty();
    } else if (cmdDamageViewPlayer_ == playerIndex) {
        // 自分が CmdDamageView を開いている → LifeView に戻す（再タップ）
        playerViews_[playerIndex] = PlayerView::LifeView;
        cmdDamageViewPlayer_ = kSourceNone;
        selectedSource_ = kSourceNone;
        nav_.markDirty();
    } else {
        // 他プレイヤーが CmdDamageView を開いている間の他扇形タップ →
        // 被弾元選択はスライドで行う方式に変更したため、タップは無視する。
    }
}

void EdhScreenState::selectSource(uint8_t sourceIndex, uint32_t nowMs) {
    if (nav_.screen() != Screen::Active) return;
    if (cmdDamageViewPlayer_ == kSourceNone) return;
    if (sourceIndex == cmdDamageViewPlayer_) return;  // 自分自身は選択できない

    lastActivityMs_ = nowMs;
    selectedSource_ = sourceIndex;
    nav_.markDirty();
}

void EdhScreenState::clearSource() {
    selectedSource_ = kSourceNone;
}

void EdhScreenState::checkTimeout(uint32_t nowMs) {
    if (nav_.screen() != Screen::Active) return;
    if (cmdDamageViewPlayer_ == kSourceNone) return;

    if (nowMs - lastActivityMs_ >= kViewTimeoutMs) {
        // タイムアウト: CmdDamageView を閉じて LifeView に復帰する
        playerViews_[cmdDamageViewPlayer_] = PlayerView::LifeView;
        cmdDamageViewPlayer_ = kSourceNone;
        selectedSource_ = kSourceNone;
        nav_.markDirty();
    }
}

void EdhScreenState::notifyActivity(uint32_t nowMs) {
    lastActivityMs_ = nowMs;
}

// --- ボタン入力ハンドラ ---

ScreenAction EdhScreenState::onNext() {
    switch (nav_.screen()) {

    case Screen::Setup: {
        // 初期ライフのプリセットをトグルする（20 ↔ 40）
        const uint32_t next = (setupLife_ == 20) ? 40u : 20u;
        setupLife_ = next;
        nav_.markDirty();
        return ScreenAction::None;
    }

    case Screen::Menu:
        // メニュー遷移の共通部（確認待ち解除 + カーソル循環）は MenuNav へ委譲。
        nav_.cycleMenuItem();
        return ScreenAction::None;

    case Screen::Sensitivity: {
        sensitivityIndex_ =
            (sensitivityIndex_ + 1) % counter::config::kSensitivityPresetCount;
        nav_.markDirty();
        return ScreenAction::None;
    }

    default:
        return ScreenAction::None;
    }
}

ScreenAction EdhScreenState::onSelect() {
    // FaB / EDH で完全一致しているため MenuNav の共通実装へ委譲する。
    return nav_.onSelect();
}

ScreenAction EdhScreenState::onLongPressB() {
    switch (nav_.screen()) {

    case Screen::Setup:
        nav_.enterActive();
        resetViewStates();
        return ScreenAction::StartMatch;

    case Screen::Menu:
        if (!nav_.awaitingConfirm()) {
            return ScreenAction::None;
        }
        nav_.cancelConfirm();
        if (nav_.confirmTarget() == MenuItem::Rematch) {
            nav_.enterActive();
            resetViewStates();
            return ScreenAction::Rematch;
        }
        return ScreenAction::None;

    default:
        return ScreenAction::None;
    }
}

ScreenAction EdhScreenState::onCloseMenu() {
    // FaB / EDH で完全一致しているため MenuNav の共通実装へ委譲する。
    return nav_.onCloseMenu();
}

void EdhScreenState::enterActive() {
    nav_.enterActive();
}

bool EdhScreenState::consumeDirty() {
    return nav_.consumeDirty();
}

void EdhScreenState::resetViewStates() {
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        playerViews_[i] = PlayerView::LifeView;
    }
    cmdDamageViewPlayer_ = kSourceNone;
    selectedSource_ = kSourceNone;
}

}  // namespace counter::edh::app
