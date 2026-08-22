#include "app/screen_state.hpp"

#include "app_config.hpp"

namespace counter::app {

void ScreenState::reset() {
    nav_.reset();
    setupLife_[0]  = kDefaultLife;  // Top
    setupLife_[1]  = kDefaultLife;  // Bottom
}

Screen ScreenState::screen() const {
    return nav_.screen();
}

uint8_t ScreenState::menuIndex() const {
    return nav_.menuIndex();
}

MenuItem ScreenState::menuItem() const {
    return nav_.menuItem();
}

bool ScreenState::awaitingConfirm() const {
    return nav_.awaitingConfirm();
}

MenuItem ScreenState::confirmTarget() const {
    return nav_.confirmTarget();
}

uint32_t ScreenState::setupLife(PlayerId player) const {
    // PlayerId::Top = 0, PlayerId::Bottom = 1
    return setupLife_[static_cast<size_t>(player)];
}

void ScreenState::setSetupLife(PlayerId player, uint32_t life) {
    // 外周スライドによる任意値設定のためにアプリ層から呼ばれる。
    // 0 を下限とし、上限は設けない。開始ライフはヒーローカード依存で
    // 20/40 に限らない（FaB の Blitz は 20、CC は 40、ヒーロー固有値もある）。
    setupLife_[static_cast<size_t>(player)] = life;
    nav_.markDirty();
}

uint8_t ScreenState::sensitivityIndex() const {
    return sensitivityIndex_;
}

void ScreenState::setSensitivityIndex(uint8_t index) {
    // アプリ層が NVS から読み出した値を設定する。
    // reset() では変更しない（感度はユーザーの永続的な設定であるため）。
    sensitivityIndex_ = index;
    nav_.markDirty();
}

// --- 入力ハンドラ ---

ScreenAction ScreenState::onNext() {
    switch (nav_.screen()) {

    case Screen::Setup: {
        // 開始ライフのプリセットを切り替える。上下両方を同時にトグルする。
        // FaB では「両プレイヤー同じ開始ライフ」が一般的なため、
        // 個別設定は外周スライド（setSetupLife）で行う。
        const uint32_t next = (setupLife_[0] == 20) ? 40u : 20u;
        setupLife_[0] = next;
        setupLife_[1] = next;
        nav_.markDirty();
        return ScreenAction::None;
    }

    case Screen::Menu:
        // メニュー遷移の共通部（確認待ち解除 + カーソル循環）は MenuNav へ委譲。
        nav_.cycleMenuItem();
        return ScreenAction::None;

    case Screen::Sensitivity: {
        // 感度プリセットを順にトグルする（5 → 10 → 20 → 5 → ...）。
        sensitivityIndex_ =
            (sensitivityIndex_ + 1) % config::kSensitivityPresetCount;
        nav_.markDirty();
        return ScreenAction::None;
    }

    default:
        // Active / History / About: onNext() に割り当てられた動作はない。
        // Undo / ロックはアプリ層が直接処理するため、ここには来ない。
        return ScreenAction::None;
    }
}

ScreenAction ScreenState::onSelect() {
    // FaB / EDH で完全一致しているため MenuNav の共通実装へ委譲する。
    return nav_.onSelect();
}

ScreenAction ScreenState::onLongPressB() {
    switch (nav_.screen()) {

    case Screen::Setup:
        // Setup の確定は長押しのみとする。
        // 短押しでは何もせず（onSelect 参照）、1 秒の長押しで初めて確定する。
        // これにより A ボタン（プリセット切り替え）と B ボタンの押し間違いによる
        // 意図しない試合開始を防止する。
        nav_.enterActive();
        return ScreenAction::StartMatch;

    case Screen::Menu:
        // 確認待ちのときだけ、対象に応じたアクションを返す。
        // 確認待ちでなければ何もしない（長押しの誤操作を防ぐ）。
        if (!nav_.awaitingConfirm()) {
            return ScreenAction::None;
        }
        nav_.cancelConfirm();
        if (nav_.confirmTarget() == MenuItem::Rematch) {
            nav_.enterActive();
            return ScreenAction::Rematch;
        }
        // confirmTarget_ が Rematch 以外になることは
        // onSelect() の実装上ありえないが、安全のため None を返す。
        return ScreenAction::None;

    default:
        return ScreenAction::None;
    }
}

ScreenAction ScreenState::onCloseMenu() {
    // FaB / EDH で完全一致しているため MenuNav の共通実装へ委譲する。
    return nav_.onCloseMenu();
}

void ScreenState::enterActive() {
    nav_.enterActive();
}

bool ScreenState::consumeDirty() {
    // ワンショット: 1 回 true を返したら次は false になる。
    // アプリ層が「再描画が必要か」を毎ループ問い合わせ、
    // true のときだけ Renderer を呼ぶことで不要な描画を省く。
    return nav_.consumeDirty();
}

}  // namespace counter::app
