// test/test_screen/test_screen.cpp
//
// ScreenState のホスト単体テスト（L1）
// NewGame 統合（issue #15）後のメニュー構成・画面遷移を検証する。
// Swap Sides 削除（issue #16）後、kMenuItemCount は 5 に変更された。
// Sensitivity 追加（issue #38）後、kMenuItemCount は 6 に変更された。
// メニュー構成: Resume=0, History=1, SetLife=2, SetSensitivity=3, Rematch=4, About=5

#include <unity.h>
#include <cstdint>

#include "app/screen_state.hpp"

using namespace counter::app;

// テスト間で共有する ScreenState。setUp() で毎回 reset() される。
static ScreenState ss;

void setUp(void) {
    ss.reset();
}

void tearDown(void) {
    // クリーンアップ不要
}

// ========================================================================
// 1. kMenuItemCount の回帰テスト（issue #38: Sensitivity 追加後は 6）
// ========================================================================

// kMenuItemCount が 6 であること（Sensitivity 追加前は 5 だった）
void test_menu_item_count_is_6(void) {
    TEST_ASSERT_EQUAL_UINT8(6, kMenuItemCount);
}

// ========================================================================
// 2. メニューインデックスの循環（0..5）
// ========================================================================

// reset() 後のメニューインデックスが 0 であること
void test_menu_index_initial_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

// onNext() でメニューインデックスが 0 から 5 まで順に進むこと
void test_menu_index_increments_through_all_items(void) {
    // Active → Menu を開く
    ss.enterActive();
    ss.onCloseMenu();  // Active → Menu
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));

    // 初期位置は 0（Resume）
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());

    // onNext() を 5 回呼ぶと 1, 2, 3, 4, 5 と進む
    for (uint8_t i = 1; i < kMenuItemCount; ++i) {
        ss.onNext();
        TEST_ASSERT_EQUAL_UINT8(i, ss.menuIndex());
    }
}

// インデックス 5（最後の項目）で onNext() を呼ぶと 0 に戻ること
void test_menu_index_wraps_around_to_zero(void) {
    // Active → Menu を開く
    ss.enterActive();
    ss.onCloseMenu();

    // インデックスを 5（最後）まで進める
    for (uint8_t i = 0; i < kMenuItemCount - 1; ++i) {
        ss.onNext();
    }
    TEST_ASSERT_EQUAL_UINT8(5, ss.menuIndex());

    // もう一回 onNext() → 0 に戻る
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

// onNext() を kMenuItemCount 回呼ぶとちょうど一周して 0 に戻ること
void test_menu_index_full_cycle_returns_to_zero(void) {
    ss.enterActive();
    ss.onCloseMenu();

    for (uint8_t i = 0; i < kMenuItemCount; ++i) {
        ss.onNext();
    }
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

// onNext() を kMenuItemCount * 3 回呼んでも 0 に戻ること（複数周）
void test_menu_index_multiple_cycles(void) {
    ss.enterActive();
    ss.onCloseMenu();

    for (uint8_t i = 0; i < kMenuItemCount * 3; ++i) {
        ss.onNext();
    }
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

// ========================================================================
// 3. 各 MenuItem の選択と期待される ScreenAction
// ========================================================================

// ヘルパ: Menu 画面を開き、指定インデックスまでカーソルを移動する
static void openMenuAndMoveTo(uint8_t targetIndex) {
    ss.enterActive();
    ss.onCloseMenu();  // Active → Menu, menuIndex = 0
    for (uint8_t i = 0; i < targetIndex; ++i) {
        ss.onNext();
    }
}

// Resume（インデックス 0）: 選択すると Active に戻り、None を返す
void test_select_resume_returns_to_active(void) {
    openMenuAndMoveTo(0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Resume),
                          static_cast<int>(ss.menuItem()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

// History（インデックス 1）: 選択すると History 画面に遷移し、None を返す
void test_select_history_goes_to_history_screen(void) {
    openMenuAndMoveTo(1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::History),
                          static_cast<int>(ss.menuItem()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::History),
                          static_cast<int>(ss.screen()));
}

// SetLife（インデックス 2）: 選択すると Setup 画面に遷移し、None を返す
void test_select_set_life_goes_to_setup_screen(void) {
    openMenuAndMoveTo(2);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::SetLife),
                          static_cast<int>(ss.menuItem()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
}

// SetLife → Setup → ライフ変更 → 長押しで StartMatch: E2E フロー
// issue #15 で NewGame が削除された後、開始ライフを変えて新しい試合を始める
// 唯一の経路（Menu → SetLife → Setup → onLongPressB）を検証する。
void test_set_life_to_setup_then_start_match(void) {
    // 1. Menu から SetLife（インデックス 2）を選択して Setup 画面へ遷移する
    openMenuAndMoveTo(2);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::SetLife),
                          static_cast<int>(ss.menuItem()));
    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));

    // 2. Setup 画面でライフプリセットを変更する（40 → 20）
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT32(20, ss.setupLife(PlayerId::Top));
    TEST_ASSERT_EQUAL_UINT32(20, ss.setupLife(PlayerId::Bottom));

    // 3. 長押しで試合を開始する → StartMatch が返り Active に遷移する
    action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::StartMatch),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

// SetSensitivity（インデックス 3）: 選択すると Sensitivity 画面に遷移し、None を返す
void test_select_set_sensitivity_goes_to_sensitivity_screen(void) {
    openMenuAndMoveTo(3);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::SetSensitivity),
                          static_cast<int>(ss.menuItem()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Sensitivity),
                          static_cast<int>(ss.screen()));
}

// Sensitivity 画面で onNext() がプリセットを循環すること
void test_sensitivity_next_cycles_presets(void) {
    openMenuAndMoveTo(3);
    ss.onSelect();  // Menu → Sensitivity
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Sensitivity),
                          static_cast<int>(ss.screen()));

    // デフォルトは 1（10 ライフ/周）
    TEST_ASSERT_EQUAL_UINT8(1, ss.sensitivityIndex());

    // onNext() → 2（20 ライフ/周）
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(2, ss.sensitivityIndex());

    // onNext() → 0（5 ライフ/周）— ラップアラウンド
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(0, ss.sensitivityIndex());

    // onNext() → 1（10 ライフ/周）— 一周
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(1, ss.sensitivityIndex());
}

// Sensitivity 画面で onSelect() → Menu に戻ること
void test_sensitivity_select_returns_to_menu(void) {
    openMenuAndMoveTo(3);
    ss.onSelect();  // Menu → Sensitivity
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Sensitivity),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
}

// Sensitivity 画面で onCloseMenu() → Menu に戻ること
void test_sensitivity_close_menu_returns_to_menu(void) {
    openMenuAndMoveTo(3);
    ss.onSelect();  // Menu → Sensitivity
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Sensitivity),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
}

// sensitivityIndex が reset() で変わらないこと（永続的な設定）
void test_sensitivity_index_survives_reset(void) {
    ss.setSensitivityIndex(2);
    TEST_ASSERT_EQUAL_UINT8(2, ss.sensitivityIndex());

    ss.reset();
    // reset() は sensitivityIndex_ を変更しない
    TEST_ASSERT_EQUAL_UINT8(2, ss.sensitivityIndex());
}

// Rematch（インデックス 4）: 選択すると確認待ちになり、None を返す
void test_select_rematch_enters_confirm(void) {
    openMenuAndMoveTo(4);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Rematch),
                          static_cast<int>(ss.menuItem()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_TRUE(ss.awaitingConfirm());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Rematch),
                          static_cast<int>(ss.confirmTarget()));
}

// Rematch 確認待ちで長押し → Rematch アクションが返り Active に遷移する
void test_confirm_rematch_returns_rematch_action(void) {
    openMenuAndMoveTo(4);
    ss.onSelect();  // 確認待ちに入る
    TEST_ASSERT_TRUE(ss.awaitingConfirm());

    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::Rematch),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
    TEST_ASSERT_FALSE(ss.awaitingConfirm());
}

// About（インデックス 5）: 選択すると About 画面に遷移し、None を返す
void test_select_about_goes_to_about_screen(void) {
    openMenuAndMoveTo(5);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::About),
                          static_cast<int>(ss.menuItem()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::About),
                          static_cast<int>(ss.screen()));
}

// ========================================================================
// 4. Setup → Active への遷移
// ========================================================================

// reset() 後は Setup 画面であること
void test_initial_screen_is_setup(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
}

// Setup で onLongPressB() → Active に遷移し StartMatch を返すこと
void test_setup_long_press_b_starts_match(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::StartMatch),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

// Setup で onSelect() は何もしないこと（誤操作防止）
void test_setup_select_does_nothing(void) {
    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(ss.screen()));
}

// enterActive() で Active に遷移すること
void test_enter_active_transitions_to_active(void) {
    ss.enterActive();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

// Setup で onNext() はライフプリセットをトグルすること（20 ↔ 40）
void test_setup_next_toggles_life_preset(void) {
    // 初期ライフは 40
    TEST_ASSERT_EQUAL_UINT32(40, ss.setupLife(PlayerId::Top));
    TEST_ASSERT_EQUAL_UINT32(40, ss.setupLife(PlayerId::Bottom));

    // onNext() → 20 に切り替わる
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT32(20, ss.setupLife(PlayerId::Top));
    TEST_ASSERT_EQUAL_UINT32(20, ss.setupLife(PlayerId::Bottom));

    // もう一度 onNext() → 40 に戻る
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT32(40, ss.setupLife(PlayerId::Top));
    TEST_ASSERT_EQUAL_UINT32(40, ss.setupLife(PlayerId::Bottom));
}

// ========================================================================
// 5. メニューの開閉
// ========================================================================

// Active で onCloseMenu() → Menu に遷移し、menuIndex が 0 にリセットされること
void test_active_close_menu_opens_menu(void) {
    ss.enterActive();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

// Menu で onCloseMenu() → Active に戻ること
void test_menu_close_menu_returns_to_active(void) {
    ss.enterActive();
    ss.onCloseMenu();  // Active → Menu
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(ss.screen()));
}

// Menu を閉じると確認待ちが解除されること
void test_close_menu_clears_confirm(void) {
    openMenuAndMoveTo(4);  // Rematch
    ss.onSelect();         // 確認待ちに入る
    TEST_ASSERT_TRUE(ss.awaitingConfirm());

    ss.onCloseMenu();  // Menu → Active
    TEST_ASSERT_FALSE(ss.awaitingConfirm());
}

// Menu を開き直すと menuIndex が 0 にリセットされること
void test_reopen_menu_resets_index(void) {
    ss.enterActive();
    ss.onCloseMenu();  // Active → Menu, index=0

    // インデックスを 3 に進める
    ss.onNext();
    ss.onNext();
    ss.onNext();
    TEST_ASSERT_EQUAL_UINT8(3, ss.menuIndex());

    ss.onCloseMenu();  // Menu → Active
    ss.onCloseMenu();  // Active → Menu, index=0 にリセット
    TEST_ASSERT_EQUAL_UINT8(0, ss.menuIndex());
}

// History 画面で onCloseMenu() → Menu に戻ること
void test_history_close_menu_returns_to_menu(void) {
    openMenuAndMoveTo(1);  // History
    ss.onSelect();         // Menu → History
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::History),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
}

// About 画面で onCloseMenu() → Menu に戻ること
void test_about_close_menu_returns_to_menu(void) {
    openMenuAndMoveTo(5);  // About
    ss.onSelect();         // Menu → About
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::About),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
}

// History 画面で onSelect() → Menu に戻ること
void test_history_select_returns_to_menu(void) {
    openMenuAndMoveTo(1);
    ss.onSelect();  // Menu → History
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::History),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
}

// About 画面で onSelect() → Menu に戻ること
void test_about_select_returns_to_menu(void) {
    openMenuAndMoveTo(5);
    ss.onSelect();  // Menu → About
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::About),
                          static_cast<int>(ss.screen()));

    ScreenAction action = ss.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(ss.screen()));
}

// ========================================================================
// 追加: onNext() で確認待ちが解除されること
// ========================================================================

// Menu で onNext() すると確認待ちが解除されること
void test_menu_next_clears_confirm(void) {
    openMenuAndMoveTo(4);  // Rematch
    ss.onSelect();         // 確認待ちに入る
    TEST_ASSERT_TRUE(ss.awaitingConfirm());

    ss.onNext();  // カーソル移動 → 確認待ち解除
    TEST_ASSERT_FALSE(ss.awaitingConfirm());
}

// 確認待ちでないときに onLongPressB() しても何も起きないこと（Menu 画面）
void test_menu_long_press_without_confirm_does_nothing(void) {
    openMenuAndMoveTo(0);  // Resume
    TEST_ASSERT_FALSE(ss.awaitingConfirm());

    ScreenAction action = ss.onLongPressB();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
}

// ========================================================================
// consumeDirty() のテスト
// ========================================================================

// reset() 後は dirty であること（初期状態は描画が必要）
void test_initial_state_is_dirty(void) {
    TEST_ASSERT_TRUE(ss.consumeDirty());
    // 消費後は false
    TEST_ASSERT_FALSE(ss.consumeDirty());
}

// onNext()（Menu 画面）後は dirty であること
void test_menu_next_marks_dirty(void) {
    ss.enterActive();
    ss.onCloseMenu();
    ss.consumeDirty();  // 消費してクリア

    ss.onNext();
    TEST_ASSERT_TRUE(ss.consumeDirty());
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // 1. kMenuItemCount の回帰テスト
    RUN_TEST(test_menu_item_count_is_6);

    // 2. メニューインデックスの循環
    RUN_TEST(test_menu_index_initial_is_zero);
    RUN_TEST(test_menu_index_increments_through_all_items);
    RUN_TEST(test_menu_index_wraps_around_to_zero);
    RUN_TEST(test_menu_index_full_cycle_returns_to_zero);
    RUN_TEST(test_menu_index_multiple_cycles);

    // 3. 各 MenuItem の選択と ScreenAction
    RUN_TEST(test_select_resume_returns_to_active);
    RUN_TEST(test_select_history_goes_to_history_screen);
    RUN_TEST(test_select_set_life_goes_to_setup_screen);
    RUN_TEST(test_set_life_to_setup_then_start_match);
    RUN_TEST(test_select_set_sensitivity_goes_to_sensitivity_screen);
    RUN_TEST(test_sensitivity_next_cycles_presets);
    RUN_TEST(test_sensitivity_select_returns_to_menu);
    RUN_TEST(test_sensitivity_close_menu_returns_to_menu);
    RUN_TEST(test_sensitivity_index_survives_reset);
    RUN_TEST(test_select_rematch_enters_confirm);
    RUN_TEST(test_confirm_rematch_returns_rematch_action);
    RUN_TEST(test_select_about_goes_to_about_screen);

    // 4. Setup -> Active への遷移
    RUN_TEST(test_initial_screen_is_setup);
    RUN_TEST(test_setup_long_press_b_starts_match);
    RUN_TEST(test_setup_select_does_nothing);
    RUN_TEST(test_enter_active_transitions_to_active);
    RUN_TEST(test_setup_next_toggles_life_preset);

    // 5. メニューの開閉
    RUN_TEST(test_active_close_menu_opens_menu);
    RUN_TEST(test_menu_close_menu_returns_to_active);
    RUN_TEST(test_close_menu_clears_confirm);
    RUN_TEST(test_reopen_menu_resets_index);
    RUN_TEST(test_history_close_menu_returns_to_menu);
    RUN_TEST(test_about_close_menu_returns_to_menu);
    RUN_TEST(test_history_select_returns_to_menu);
    RUN_TEST(test_about_select_returns_to_menu);

    // 追加テスト
    RUN_TEST(test_menu_next_clears_confirm);
    RUN_TEST(test_menu_long_press_without_confirm_does_nothing);
    RUN_TEST(test_initial_state_is_dirty);
    RUN_TEST(test_menu_next_marks_dirty);

    return UNITY_END();
}
