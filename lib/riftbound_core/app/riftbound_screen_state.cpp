#include "app/riftbound_screen_state.hpp"

#include "app_config.hpp"

namespace counter::riftbound::app {

void RiftboundScreenState::reset() {
    nav_.reset();
    // sensitivityIndex_ は reset() で変更しない（FaB 版と同じ方針。
    // 感度はユーザーの永続的な設定であり、試合の有無に依存しない）。
}

Screen RiftboundScreenState::screen() const {
    return nav_.screen();
}

uint8_t RiftboundScreenState::menuIndex() const {
    return nav_.menuIndex();
}

MenuItem RiftboundScreenState::menuItem() const {
    return nav_.menuItem();
}

bool RiftboundScreenState::awaitingConfirm() const {
    return nav_.awaitingConfirm();
}

MenuItem RiftboundScreenState::confirmTarget() const {
    return nav_.confirmTarget();
}

uint8_t RiftboundScreenState::sensitivityIndex() const {
    return sensitivityIndex_;
}

void RiftboundScreenState::setSensitivityIndex(uint8_t index) {
    // アプリ層が NVS から読み出した値を設定する。
    sensitivityIndex_ = index;
    nav_.markDirty();
}

// --- 入力ハンドラ ---

ScreenAction RiftboundScreenState::onNext() {
    switch (nav_.screen()) {

    case Screen::Setup:
        // FaB 版の A 短押し（20/40 プリセットトグル）に相当する操作が無い。
        // Riftbound の得点は常に 0 から始まり、Setup で設定する値が存在しないため。
        return ScreenAction::None;

    case Screen::Menu:
        // メニュー遷移の共通部（確認待ち解除 + カーソル循環）は MenuNav へ委譲。
        nav_.cycleMenuItem();
        // SetLife は得点制の Riftbound では意味を持たないため、
        // カーソルが SetLife に乗った場合はさらに 1 つ進めてスキップする。
        // 共通 MenuNav は FaB との enum 互換のため 6 項目固定であり、
        // 項目の削除ではなく「選択対象から外す」方式で実装する。
        // History(1) の次は SetLife(2) を飛ばして Sensitivity(3) へ、
        // About(5) の次は SetLife を経由せず Resume(0) へ戻る。
        if (nav_.menuItem() == MenuItem::SetLife) {
            nav_.cycleMenuItem();
        }
        return ScreenAction::None;

    case Screen::Sensitivity:
        // 感度プリセットを順にトグルする（5 → 10 → 20 → 5 → ...）。
        sensitivityIndex_ =
            (sensitivityIndex_ + 1) % config::kSensitivityPresetCount;
        nav_.markDirty();
        return ScreenAction::None;

    default:
        // Active / History / About: onNext() に割り当てられた動作はない。
        // Undo / ロックはアプリ層が直接処理するため、ここには来ない。
        return ScreenAction::None;
    }
}

ScreenAction RiftboundScreenState::onSelect() {
    // MenuNav の共通実装へ委譲する。カーソルが SetLife に乗ることがないため
    // MenuItem::SetLife の分岐（Setup への遷移）は到達しない。
    return nav_.onSelect();
}

ScreenAction RiftboundScreenState::onLongPressB() {
    switch (nav_.screen()) {

    case Screen::Setup:
        // Setup の確定は長押しのみとする（FaB 版と同一方針）。
        // 短押しでは何もせず、1 秒の長押しで初めて確定する。
        nav_.enterActive();
        return ScreenAction::StartMatch;

    case Screen::Menu:
        // 確認待ちのときだけ、対象に応じたアクションを返す（FaB 版と同一）。
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

ScreenAction RiftboundScreenState::onCloseMenu() {
    // MenuNav の共通実装へ委譲する（FaB 版と完全一致）。
    return nav_.onCloseMenu();
}

void RiftboundScreenState::enterActive() {
    nav_.enterActive();
}

bool RiftboundScreenState::consumeDirty() {
    // ワンショット: 1 回 true を返したら次は false になる。
    return nav_.consumeDirty();
}

}  // namespace counter::riftbound::app
