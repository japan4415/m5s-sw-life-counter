// test/test_edh_screen/test_edh_screen.cpp
//
// EDH 版 ScreenState のホスト単体テスト
// 仕様の正は docs/15-edh-firmware-spec.md

#include <unity.h>
#include <cstdint>

#include "app/edh_screen_state.hpp"
#include "app/edh_button_route.hpp"
#include "domain/edh_life_change.hpp"  // kSourceNone, kPlayerCount

using namespace counter::edh;
using namespace counter::edh::app;
using counter::input::ButtonEvent;

static EdhScreenState ss;

void setUp(void) {
    ss.reset();
}

void tearDown(void) {
}

// ========================================================================
// 初期状態
// ========================================================================

void test_initial_screen_is_setup(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
}

void test_initial_setup_life_is_40(void) {
    TEST_ASSERT_EQUAL_UINT32(40, ss.setupLife());
}

void test_initial_dirty(void) {
    TEST_ASSERT_TRUE(ss.consumeDirty());
    TEST_ASSERT_FALSE(ss.consumeDirty());
}

// ========================================================================
// Setup -> Active
// ========================================================================

void test_setup_long_press_starts_match(void) {
    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::StartMatch),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

void test_setup_select_does_nothing(void) {
    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
}

void test_setup_next_toggles_life(void) {
    TEST_ASSERT_EQUAL_UINT32(40, ss.setupLife());
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT32(20, ss.setupLife());
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT32(40, ss.setupLife());
}

// ========================================================================
// メニュー
// ========================================================================

static void openMenu() {
    ss.enterActive();
    ss.onCloseMenu();
}

static void openMenuAndMoveTo(uint8_t targetIndex) {
    openMenu();
    for (uint8_t i = 0; i < targetIndex; ++i) {
        ss.onNext();
    }
}

void test_menu_item_count_is_6(void) {
    TEST_ASSERT_EQUAL_UINT8(6, kMenuItemCount);
}

void test_menu_index_wraps(void) {
    openMenu();
    for (uint8_t i = 0; i < kMenuItemCount; ++i) {
        ss.onNext();
    }
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

void test_select_resume(void) {
    openMenuAndMoveTo(0);
    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

void test_select_history(void) {
    openMenuAndMoveTo(1);
    ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::History),
                          static_cast<int>(ss.screen()));
}

void test_select_set_life(void) {
    openMenuAndMoveTo(2);
    ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
}

void test_select_sensitivity(void) {
    openMenuAndMoveTo(3);
    ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Sensitivity),
                          static_cast<int>(ss.screen()));
}

void test_select_rematch_enters_confirm(void) {
    openMenuAndMoveTo(4);
    ss.onSelect();
    TEST_ASSERT_TRUE(ss.awaitingConfirm());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Rematch),
                          static_cast<int>(ss.confirmTarget()));
}

void test_confirm_rematch(void) {
    openMenuAndMoveTo(4);
    ss.onSelect();
    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::Rematch),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

void test_select_about(void) {
    openMenuAndMoveTo(5);
    ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::About),
                          static_cast<int>(ss.screen()));
}

void test_close_menu_clears_confirm(void) {
    openMenuAndMoveTo(4);
    ss.onSelect();
    TEST_ASSERT_TRUE(ss.awaitingConfirm());
    ss.onCloseMenu();
    TEST_ASSERT_FALSE(ss.awaitingConfirm());
}

void test_next_clears_confirm(void) {
    openMenuAndMoveTo(4);
    ss.onSelect();
    TEST_ASSERT_TRUE(ss.awaitingConfirm());
    ss.onNext();
    TEST_ASSERT_FALSE(ss.awaitingConfirm());
}

// ========================================================================
// ビュー状態: トグル
// ========================================================================

void test_initial_player_views_are_life_view(void) {
    ss.enterActive();
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::LifeView),
                              static_cast<int>(ss.playerView(i)));
    }
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.cmdDamageViewPlayer());
}

void test_inner_tap_opens_cmd_damage_view(void) {
    ss.enterActive();
    ss.onInnerTap(2, 1000);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::CmdDamageView),
                          static_cast<int>(ss.playerView(2)));
    TEST_ASSERT_EQUAL_UINT8(2, ss.cmdDamageViewPlayer());
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.selectedSource());
}

void test_inner_tap_toggle_closes_cmd_damage_view(void) {
    ss.enterActive();
    ss.onInnerTap(2, 1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::CmdDamageView),
                          static_cast<int>(ss.playerView(2)));

    ss.onInnerTap(2, 2000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::LifeView),
                          static_cast<int>(ss.playerView(2)));
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.cmdDamageViewPlayer());
}

// ========================================================================
// ビュー状態: 被弾元選択（スライド開始時にアプリ層が呼ぶ）
// ========================================================================

void test_select_source(void) {
    ss.enterActive();
    ss.onInnerTap(0, 1000);  // P1 が CmdDamageView を開く

    ss.selectSource(2, 2000);  // P3 の扇形からスライド開始 → 被弾元 P3
    TEST_ASSERT_EQUAL_UINT8(2, ss.selectedSource());
}

void test_select_source_self_ignored(void) {
    ss.enterActive();
    ss.onInnerTap(0, 1000);

    ss.selectSource(0, 2000);  // 自分自身 -> 無視
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.selectedSource());
}

void test_select_source_without_cmd_view_ignored(void) {
    ss.enterActive();
    // CmdDamageView が開いていない状態で被弾元選択 -> 無視
    ss.selectSource(2, 1000);
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.selectedSource());
}

void test_clear_source(void) {
    ss.enterActive();
    ss.onInnerTap(0, 1000);
    ss.selectSource(2, 2000);
    TEST_ASSERT_EQUAL_UINT8(2, ss.selectedSource());

    ss.clearSource();
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.selectedSource());
}

// ========================================================================
// ビュー状態: 他扇形タップは無視（被弾元はスライドで決まる）
// ========================================================================

void test_other_sector_tap_ignored_when_cmd_view_open(void) {
    ss.enterActive();
    ss.onInnerTap(0, 1000);  // P1 が CmdDamageView を開く
    TEST_ASSERT_EQUAL_UINT8(0, ss.cmdDamageViewPlayer());

    // P2 の内側タップ → 被弾元選択ではなく無視される
    ss.onInnerTap(1, 2000);
    TEST_ASSERT_EQUAL_UINT8(0, ss.cmdDamageViewPlayer());
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.selectedSource());
    // P1 はまだ CmdDamageView
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::CmdDamageView),
                          static_cast<int>(ss.playerView(0)));
}

// ========================================================================
// ビュー状態: 排他制御
// ========================================================================

void test_cmd_damage_view_exclusive(void) {
    ss.enterActive();
    ss.onInnerTap(0, 1000);  // P1 が開く
    TEST_ASSERT_EQUAL_UINT8(0, ss.cmdDamageViewPlayer());

    ss.onInnerTap(1, 2000);
    TEST_ASSERT_EQUAL_UINT8(0, ss.cmdDamageViewPlayer());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::CmdDamageView),
                          static_cast<int>(ss.playerView(0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::LifeView),
                          static_cast<int>(ss.playerView(1)));
}

// ========================================================================
// ビュー状態: タイムアウト復帰
// ========================================================================

void test_timeout_restores_life_view(void) {
    ss.enterActive();
    ss.onInnerTap(1, 1000);
    TEST_ASSERT_EQUAL_UINT8(1, ss.cmdDamageViewPlayer());

    ss.checkTimeout(10999);
    TEST_ASSERT_EQUAL_UINT8(1, ss.cmdDamageViewPlayer());

    ss.checkTimeout(11000);
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.cmdDamageViewPlayer());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::LifeView),
                          static_cast<int>(ss.playerView(1)));
}

void test_activity_resets_timeout(void) {
    ss.enterActive();
    ss.onInnerTap(1, 1000);

    ss.notifyActivity(6000);

    ss.checkTimeout(11000);
    TEST_ASSERT_EQUAL_UINT8(1, ss.cmdDamageViewPlayer());

    ss.checkTimeout(16000);
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.cmdDamageViewPlayer());
}

void test_no_timeout_when_no_cmd_view(void) {
    ss.enterActive();
    ss.checkTimeout(999999);
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::LifeView),
                              static_cast<int>(ss.playerView(i)));
    }
}

// ========================================================================
// 画面遷移でビュー状態がリセットされる
// ========================================================================

void test_start_match_resets_views(void) {
    ss.onLongPressB();
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::LifeView),
                              static_cast<int>(ss.playerView(i)));
    }
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.cmdDamageViewPlayer());
}

void test_rematch_resets_views(void) {
    ss.enterActive();
    ss.onInnerTap(0, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, ss.cmdDamageViewPlayer());

    ss.onCloseMenu();
    for (uint8_t i = 0; i < 4; ++i) ss.onNext();
    ss.onSelect();
    ss.onLongPressB();

    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(PlayerView::LifeView),
                              static_cast<int>(ss.playerView(i)));
    }
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, ss.cmdDamageViewPlayer());
}

// ========================================================================
// Active 画面でのメニュー起動
// ========================================================================

void test_active_close_menu_opens_menu(void) {
    ss.enterActive();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
}

void test_active_long_press_b_does_nothing(void) {
    ss.enterActive();
    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

void test_active_close_menu_resets_confirm(void) {
    ss.enterActive();
    ss.onCloseMenu();
    TEST_ASSERT_FALSE(ss.awaitingConfirm());
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

// ========================================================================
// Setup 画面のボタン操作の網羅テスト
// ========================================================================

void test_setup_on_close_menu_does_nothing(void) {
    ScreenAction action = ss.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
}

void test_setup_long_press_b_returns_start_match_and_transitions(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::StartMatch),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
    TEST_ASSERT_TRUE(ss.consumeDirty());
}

void test_setup_long_press_b_with_custom_life(void) {
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT32(20, ss.setupLife());

    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::StartMatch),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_UINT32(20, ss.setupLife());
}

// ========================================================================
// consumeDirty after inner tap (handleInnerTap の部分再描画で dirty を消費する)
// ========================================================================

void test_consume_dirty_after_inner_tap_open(void) {
    // onInnerTap で CmdDamageView を開くと dirty が立つ
    ss.enterActive();
    ss.consumeDirty();  // enterActive の dirty を消費

    ss.onInnerTap(0, 1000);
    // dirty が立っている
    TEST_ASSERT_TRUE(ss.consumeDirty());
    // 2 回目は false
    TEST_ASSERT_FALSE(ss.consumeDirty());
}

void test_consume_dirty_after_select_source(void) {
    // selectSource でも dirty が立つ
    ss.enterActive();
    ss.onInnerTap(0, 1000);
    ss.consumeDirty();  // 消費

    ss.selectSource(2, 2000);
    TEST_ASSERT_TRUE(ss.consumeDirty());
    TEST_ASSERT_FALSE(ss.consumeDirty());
}

// ========================================================================
// Sensitivity
// ========================================================================

void test_sensitivity_default(void) {
    TEST_ASSERT_EQUAL_UINT8(1, ss.sensitivityIndex());
}

void test_sensitivity_cycles(void) {
    openMenuAndMoveTo(3);
    ss.onSelect();

    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(2, ss.sensitivityIndex());
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(0, ss.sensitivityIndex());
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(1, ss.sensitivityIndex());
}

void test_sensitivity_survives_reset(void) {
    ss.setSensitivityIndex(2);
    ss.reset();
    TEST_ASSERT_EQUAL_UINT8(2, ss.sensitivityIndex());
}

// ========================================================================
// ボタンルーティングテーブル (resolveButtonRoute)
// ========================================================================

void test_route_active_menu_requested(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnCloseMenu),
        static_cast<int>(resolveButtonRoute(
            Screen::Active, ButtonEvent::MenuRequested)));
}

void test_route_active_undo_is_ignore(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::Ignore),
        static_cast<int>(resolveButtonRoute(
            Screen::Active, ButtonEvent::UndoRequested)));
}

void test_route_active_lock_is_ignore(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::Ignore),
        static_cast<int>(resolveButtonRoute(
            Screen::Active, ButtonEvent::LockToggleRequested)));
}

void test_route_active_b_long_is_ignore(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::Ignore),
        static_cast<int>(resolveButtonRoute(
            Screen::Active, ButtonEvent::BLongPressed)));
}

void test_route_setup_b_long(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnLongPressB),
        static_cast<int>(resolveButtonRoute(
            Screen::Setup, ButtonEvent::BLongPressed)));
}

void test_route_setup_undo(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnNext),
        static_cast<int>(resolveButtonRoute(
            Screen::Setup, ButtonEvent::UndoRequested)));
}

void test_route_setup_lock_is_ignore(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::Ignore),
        static_cast<int>(resolveButtonRoute(
            Screen::Setup, ButtonEvent::LockToggleRequested)));
}

void test_route_setup_menu_is_ignore(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::Ignore),
        static_cast<int>(resolveButtonRoute(
            Screen::Setup, ButtonEvent::MenuRequested)));
}

void test_route_menu_all(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnNext),
        static_cast<int>(resolveButtonRoute(
            Screen::Menu, ButtonEvent::UndoRequested)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnSelect),
        static_cast<int>(resolveButtonRoute(
            Screen::Menu, ButtonEvent::LockToggleRequested)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnCloseMenu),
        static_cast<int>(resolveButtonRoute(
            Screen::Menu, ButtonEvent::MenuRequested)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnLongPressB),
        static_cast<int>(resolveButtonRoute(
            Screen::Menu, ButtonEvent::BLongPressed)));
}

void test_route_sensitivity_undo(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnNext),
        static_cast<int>(resolveButtonRoute(
            Screen::Sensitivity, ButtonEvent::UndoRequested)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnSelect),
        static_cast<int>(resolveButtonRoute(
            Screen::Sensitivity, ButtonEvent::LockToggleRequested)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnCloseMenu),
        static_cast<int>(resolveButtonRoute(
            Screen::Sensitivity, ButtonEvent::MenuRequested)));
}

void test_route_history_lock(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnSelect),
        static_cast<int>(resolveButtonRoute(
            Screen::History, ButtonEvent::LockToggleRequested)));
}

void test_route_history_menu(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::OnCloseMenu),
        static_cast<int>(resolveButtonRoute(
            Screen::History, ButtonEvent::MenuRequested)));
}

void test_route_history_undo_is_ignore(void) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(ButtonRoute::Ignore),
        static_cast<int>(resolveButtonRoute(
            Screen::History, ButtonEvent::UndoRequested)));
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // 初期状態
    RUN_TEST(test_initial_screen_is_setup);
    RUN_TEST(test_initial_setup_life_is_40);
    RUN_TEST(test_initial_dirty);

    // Setup -> Active
    RUN_TEST(test_setup_long_press_starts_match);
    RUN_TEST(test_setup_select_does_nothing);
    RUN_TEST(test_setup_next_toggles_life);

    // メニュー
    RUN_TEST(test_menu_item_count_is_6);
    RUN_TEST(test_menu_index_wraps);
    RUN_TEST(test_select_resume);
    RUN_TEST(test_select_history);
    RUN_TEST(test_select_set_life);
    RUN_TEST(test_select_sensitivity);
    RUN_TEST(test_select_rematch_enters_confirm);
    RUN_TEST(test_confirm_rematch);
    RUN_TEST(test_select_about);
    RUN_TEST(test_close_menu_clears_confirm);
    RUN_TEST(test_next_clears_confirm);

    // ビュー状態: トグル
    RUN_TEST(test_initial_player_views_are_life_view);
    RUN_TEST(test_inner_tap_opens_cmd_damage_view);
    RUN_TEST(test_inner_tap_toggle_closes_cmd_damage_view);

    // ビュー状態: 被弾元選択（スライド開始時）
    RUN_TEST(test_select_source);
    RUN_TEST(test_select_source_self_ignored);
    RUN_TEST(test_select_source_without_cmd_view_ignored);
    RUN_TEST(test_clear_source);
    RUN_TEST(test_other_sector_tap_ignored_when_cmd_view_open);

    // ビュー状態: 排他制御
    RUN_TEST(test_cmd_damage_view_exclusive);

    // ビュー状態: タイムアウト復帰
    RUN_TEST(test_timeout_restores_life_view);
    RUN_TEST(test_activity_resets_timeout);
    RUN_TEST(test_no_timeout_when_no_cmd_view);

    // 画面遷移でビュー状態リセット
    RUN_TEST(test_start_match_resets_views);
    RUN_TEST(test_rematch_resets_views);

    // Active 画面でのメニュー起動
    RUN_TEST(test_active_close_menu_opens_menu);
    RUN_TEST(test_active_long_press_b_does_nothing);
    RUN_TEST(test_active_close_menu_resets_confirm);

    // Setup 画面のボタン操作の網羅
    RUN_TEST(test_setup_on_close_menu_does_nothing);
    RUN_TEST(test_setup_long_press_b_returns_start_match_and_transitions);
    RUN_TEST(test_setup_long_press_b_with_custom_life);

    // consumeDirty after inner tap
    RUN_TEST(test_consume_dirty_after_inner_tap_open);
    RUN_TEST(test_consume_dirty_after_select_source);

    // Sensitivity
    RUN_TEST(test_sensitivity_default);
    RUN_TEST(test_sensitivity_cycles);
    RUN_TEST(test_sensitivity_survives_reset);

    // ボタンルーティングテーブル
    RUN_TEST(test_route_active_menu_requested);
    RUN_TEST(test_route_active_undo_is_ignore);
    RUN_TEST(test_route_active_lock_is_ignore);
    RUN_TEST(test_route_active_b_long_is_ignore);
    RUN_TEST(test_route_setup_b_long);
    RUN_TEST(test_route_setup_undo);
    RUN_TEST(test_route_setup_lock_is_ignore);
    RUN_TEST(test_route_setup_menu_is_ignore);
    RUN_TEST(test_route_menu_all);
    RUN_TEST(test_route_sensitivity_undo);
    RUN_TEST(test_route_history_lock);
    RUN_TEST(test_route_history_menu);
    RUN_TEST(test_route_history_undo_is_ignore);

    return UNITY_END();
}
