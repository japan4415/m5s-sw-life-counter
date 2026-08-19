#include "app/screen_state.hpp"

#include "app_config.hpp"

namespace counter::app {

void ScreenState::reset() {
    screen_        = Screen::Setup;
    menuIndex_     = 0;
    confirming_    = false;
    confirmTarget_ = MenuItem::Resume;
    setupLife_[0]  = kDefaultLife;  // Top
    setupLife_[1]  = kDefaultLife;  // Bottom
    dirty_         = true;
}

Screen ScreenState::screen() const {
    return screen_;
}

uint8_t ScreenState::menuIndex() const {
    return menuIndex_;
}

MenuItem ScreenState::menuItem() const {
    return static_cast<MenuItem>(menuIndex_);
}

bool ScreenState::awaitingConfirm() const {
    return confirming_;
}

MenuItem ScreenState::confirmTarget() const {
    return confirmTarget_;
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
    markDirty();
}

uint8_t ScreenState::sensitivityIndex() const {
    return sensitivityIndex_;
}

void ScreenState::setSensitivityIndex(uint8_t index) {
    // アプリ層が NVS から読み出した値を設定する。
    // reset() では変更しない（感度はユーザーの永続的な設定であるため）。
    sensitivityIndex_ = index;
    markDirty();
}

// --- 入力ハンドラ ---

ScreenAction ScreenState::onNext() {
    switch (screen_) {

    case Screen::Setup: {
        // 開始ライフのプリセットを切り替える。上下両方を同時にトグルする。
        // FaB では「両プレイヤー同じ開始ライフ」が一般的なため、
        // 個別設定は外周スライド（setSetupLife）で行う。
        const uint32_t next = (setupLife_[0] == 20) ? 40u : 20u;
        setupLife_[0] = next;
        setupLife_[1] = next;
        markDirty();
        return ScreenAction::None;
    }

    case Screen::Menu:
        // 確認待ちを解除する。別の項目へカーソルが移ったのに
        // 前の項目の確認が残っていると、意図しない Rematch を
        // 長押しで実行してしまう危険があるため。
        confirming_ = false;
        menuIndex_  = (menuIndex_ + 1) % kMenuItemCount;
        markDirty();
        return ScreenAction::None;

    case Screen::Sensitivity: {
        // 感度プリセットを順にトグルする（5 → 10 → 20 → 5 → ...）。
        sensitivityIndex_ =
            (sensitivityIndex_ + 1) % config::kSensitivityPresetCount;
        markDirty();
        return ScreenAction::None;
    }

    default:
        // Active / History / About: onNext() に割り当てられた動作はない。
        // Undo / ロックはアプリ層が直接処理するため、ここには来ない。
        return ScreenAction::None;
    }
}

ScreenAction ScreenState::onSelect() {
    switch (screen_) {

    case Screen::Setup:
        // 誤って試合を始めないため、確定は長押し（onLongPressB）のみとする。
        // 短押しで開始できてしまうと、プリセット切り替え（A ボタン）と
        // 間違えて B を押した場合に意図しない試合が始まってしまう。
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
            // Setup 画面へ遷移する。現在のライフを setupLife に写す作業は
            // アプリ層が setSetupLife() を呼んで行う。ScreenState は
            // domain::MatchState に依存しないため、ここでは画面遷移だけ行う。
            screen_ = Screen::Setup;
            markDirty();
            return ScreenAction::None;

        case MenuItem::SetSensitivity:
            // Sensitivity 画面へ遷移する。SetLife → Setup と同じパターン。
            // 現在の感度値は sensitivityIndex_ に保持されているため、
            // アプリ層での追加処理は不要。
            screen_ = Screen::Sensitivity;
            markDirty();
            return ScreenAction::None;

        case MenuItem::Rematch:
            // 確認待ちにする。長押しで実行を確定する 2 段階操作。
            // 対戦中にうっかりメニューから即 Rematch してしまうのを防ぐ。
            confirming_    = true;
            confirmTarget_ = MenuItem::Rematch;
            markDirty();
            return ScreenAction::None;

        case MenuItem::About:
            screen_ = Screen::About;
            markDirty();
            return ScreenAction::None;
        }
        // MenuItem の全値を switch で網羅しているため到達しない。
        // コンパイラ警告を抑止するためのフォールバック。
        return ScreenAction::None;
    }

    case Screen::History:
        // History からは Menu に戻る。
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::About:
        // About からは Menu に戻る。
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::Sensitivity:
        // Sensitivity からは Menu に戻る。
        // 感度の反映と NVS 保存はアプリ層が画面遷移を検出して行う。
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    default:
        // Active: onSelect() に割り当てられた動作はない。
        return ScreenAction::None;
    }
}

ScreenAction ScreenState::onLongPressB() {
    switch (screen_) {

    case Screen::Setup:
        // Setup の確定は長押しのみとする。
        // 短押しでは何もせず（onSelect 参照）、1.5 秒の長押しで初めて確定する。
        // これにより A ボタン（プリセット切り替え）と B ボタンの押し間違いによる
        // 意図しない試合開始を防止する。
        screen_ = Screen::Active;
        markDirty();
        return ScreenAction::StartMatch;

    case Screen::Menu:
        // 確認待ちのときだけ、対象に応じたアクションを返す。
        // 確認待ちでなければ何もしない（長押しの誤操作を防ぐ）。
        if (!confirming_) {
            return ScreenAction::None;
        }
        confirming_ = false;
        if (confirmTarget_ == MenuItem::Rematch) {
            screen_ = Screen::Active;
            markDirty();
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
    switch (screen_) {

    case Screen::Setup:
        // Setup には戻る先が無い。何もしない。
        return ScreenAction::None;

    case Screen::Active:
        // Active から Menu を開く。menuIndex を先頭（Resume）に戻す。
        screen_    = Screen::Menu;
        menuIndex_ = 0;
        confirming_ = false;
        markDirty();
        return ScreenAction::None;

    case Screen::Menu:
        // Menu から Active へ戻る。確認待ちも解除する。
        confirming_ = false;
        screen_     = Screen::Active;
        markDirty();
        return ScreenAction::None;

    case Screen::History:
        // History から Menu へ戻る。
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::About:
        // About から Menu へ戻る。
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;

    case Screen::Sensitivity:
        // Sensitivity から Menu へ戻る。
        screen_ = Screen::Menu;
        markDirty();
        return ScreenAction::None;
    }
    // Screen の全値を switch で網羅しているため到達しない。
    return ScreenAction::None;
}

void ScreenState::enterActive() {
    screen_ = Screen::Active;
    markDirty();
}

bool ScreenState::consumeDirty() {
    // ワンショット: 1 回 true を返したら次は false になる。
    // GestureDetector::consumeStepChanged() と同じ消費型パターン。
    // アプリ層が「再描画が必要か」を毎ループ問い合わせ、
    // true のときだけ Renderer を呼ぶことで不要な描画を省く。
    const bool wasDirty = dirty_;
    dirty_ = false;
    return wasDirty;
}

void ScreenState::markDirty() {
    dirty_ = true;
}

}  // namespace counter::app
