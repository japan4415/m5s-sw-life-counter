// test/test_menu_nav/test_menu_nav.cpp
//
// MenuNav（FaB / EDH 共通のメニュー遷移コア）のホスト単体テスト。
// Phase 3 共通化で ScreenState / EdhScreenState から抽出されたクラスの
// 振る舞いを直接検証する。
// メニュー構成: Resume=0, History=1, SetLife=2, SetSensitivity=3, Rematch=4, About=5

#include <unity.h>
#include <cstdint>

#include "app/menu_nav.hpp"

using namespace counter::app;

// テスト間で共有する MenuNav。setUp() で毎回 reset() される。
static MenuNav nav;

void setUp(void) {
    nav.reset();
}

void tearDown(void) {
    // クリーンアップ不要
}

// ヘルパ: dirty を消費してクリーンな状態にする
static void clearDirty() {
    nav.consumeDirty();
}

// ヘルパ: Menu を開いて指定インデックスまでカーソルを移動する
static void openMenuAndMoveTo(uint8_t targetIndex) {
    nav.enterActive();
    nav.onCloseMenu();  // Active → Menu, menuIndex = 0
    for (uint8_t i = 0; i < targetIndex; ++i) {
        nav.cycleMenuItem();
    }
}

// ========================================================================
// 1. 初期状態とメニューインデックスの循環（端での wrap 含む）
// ========================================================================

// reset() 直後は Setup / menuIndex 0 / dirty
void test_initial_state_is_setup_with_index_zero(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(nav.screen()));
    TEST_ASSERT_EQUAL_UINT8(0, nav.menuIndex());
    TEST_ASSERT_FALSE(nav.awaitingConfirm());
    TEST_ASSERT_TRUE(nav.consumeDirty());
    TEST_ASSERT_FALSE(nav.consumeDirty());
}

// cycleMenuItem() でインデックスが 0..5 まで順に進むこと
void test_cycle_advances_through_all_items(void) {
    nav.enterActive();
    nav.onCloseMenu();  // Active → Menu
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(nav.screen()));

    for (uint8_t i = 0; i < kMenuItemCount; ++i) {
        TEST_ASSERT_EQUAL_UINT8(i, nav.menuIndex());
        // menuItem() も同じインデックスに対応する enum を返すこと
        TEST_ASSERT_EQUAL_INT(i, static_cast<int>(nav.menuItem()));
        if (i < kMenuItemCount - 1) {
            nav.cycleMenuItem();
        }
    }
}

// 最後の項目（About = 5）で循環すると 0（Resume）に戻ること
void test_cycle_wraps_from_last_to_zero(void) {
    openMenuAndMoveTo(kMenuItemCount - 1);
    TEST_ASSERT_EQUAL_UINT8(kMenuItemCount - 1, nav.menuIndex());

    nav.cycleMenuItem();
    TEST_ASSERT_EQUAL_UINT8(0, nav.menuIndex());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Resume),
                          static_cast<int>(nav.menuItem()));
}

// kMenuItemCount 回の循環でちょうど一周して 0 に戻ること（複数周）
void test_cycle_multiple_rounds_returns_to_zero(void) {
    openMenuAndMoveTo(0);
    for (uint8_t i = 0; i < kMenuItemCount * 3; ++i) {
        nav.cycleMenuItem();
    }
    TEST_ASSERT_EQUAL_UINT8(0, nav.menuIndex());
}

// ========================================================================
// 2. 各 MenuItem の select 遷移先
// ========================================================================

// Resume（0）: Active へ戻り、ScreenAction::None
void test_select_resume_returns_to_active(void) {
    openMenuAndMoveTo(0);
    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(nav.screen()));
}

// History（1）: History 画面へ遷移
void test_select_history_goes_to_history_screen(void) {
    openMenuAndMoveTo(1);
    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::History),
                          static_cast<int>(nav.screen()));
}

// SetLife（2）: Setup 画面へ遷移（ライフの写しはアプリ層の責務）
void test_select_set_life_goes_to_setup_screen(void) {
    openMenuAndMoveTo(2);
    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(nav.screen()));
}

// SetSensitivity（3）: Sensitivity 画面へ遷移
void test_select_set_sensitivity_goes_to_sensitivity_screen(void) {
    openMenuAndMoveTo(3);
    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Sensitivity),
                          static_cast<int>(nav.screen()));
}

// Rematch（4）: 確認待ちに入り、confirmTarget が Rematch になる
void test_select_rematch_enters_confirm(void) {
    openMenuAndMoveTo(4);
    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_TRUE(nav.awaitingConfirm());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Rematch),
                          static_cast<int>(nav.confirmTarget()));
    // 画面は Menu のままであること（遷移は長押し確定時）
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(nav.screen()));
}

// About（5）: About 画面へ遷移
void test_select_about_goes_to_about_screen(void) {
    openMenuAndMoveTo(5);
    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::About),
                          static_cast<int>(nav.screen()));
}

// Setup で onSelect() しても何も起きないこと（誤操作防止・確定は長押しのみ）
void test_select_on_setup_does_nothing(void) {
    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(nav.screen()));
}

// Active で onSelect() しても何も起きないこと
void test_select_on_active_does_nothing(void) {
    nav.enterActive();
    clearDirty();

    ScreenAction action = nav.onSelect();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(nav.screen()));
    // 動作なしのときは dirty も立たないこと
    TEST_ASSERT_FALSE(nav.consumeDirty());
}

// ========================================================================
// 3. close 時の戻り先
// ========================================================================

// Setup で onCloseMenu() しても何も起きないこと（dirty も立たない）
void test_close_menu_on_setup_is_noop_and_not_dirty(void) {
    clearDirty();

    ScreenAction action = nav.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(nav.screen()));
    TEST_ASSERT_FALSE(nav.consumeDirty());
}

// Active で onCloseMenu() → Menu を開き、menuIndex が 0 に戻ること
void test_close_menu_on_active_opens_menu_at_index_zero(void) {
    nav.enterActive();
    nav.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(nav.screen()));
    TEST_ASSERT_EQUAL_UINT8(0, nav.menuIndex());

    // カーソルを進めてから開き直しても 0 に戻ること
    nav.cycleMenuItem();
    nav.cycleMenuItem();
    nav.onCloseMenu();  // Menu → Active
    nav.onCloseMenu();  // Active → Menu
    TEST_ASSERT_EQUAL_UINT8(0, nav.menuIndex());
}

// Menu で onCloseMenu() → Active に戻ること
void test_close_menu_on_menu_returns_to_active(void) {
    openMenuAndMoveTo(3);
    ScreenAction action = nav.onCloseMenu();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(nav.screen()));
}

// Menu で onCloseMenu() すると確認待ちも解除されること
void test_close_menu_clears_confirm(void) {
    openMenuAndMoveTo(4);
    nav.onSelect();  // 確認待ちに入る
    TEST_ASSERT_TRUE(nav.awaitingConfirm());

    nav.onCloseMenu();
    TEST_ASSERT_FALSE(nav.awaitingConfirm());
}

// History / About / Sensitivity で onCloseMenu() → Menu に戻ること
void test_close_menu_from_subscreens_returns_to_menu(void) {
    const uint8_t subscreens[] = {1, 5, 3};  // History, About, SetSensitivity
    const Screen expected[] = {Screen::History, Screen::About,
                               Screen::Sensitivity};
    for (int c = 0; c < 3; ++c) {
        setUp();  // 各ケースで初期化
        openMenuAndMoveTo(subscreens[c]);
        nav.onSelect();  // 対象画面へ遷移
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[c]),
                              static_cast<int>(nav.screen()));

        ScreenAction action = nav.onCloseMenu();
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                              static_cast<int>(action));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                              static_cast<int>(nav.screen()));
    }
}

// ========================================================================
// 4. カーソル移動と確認待ちの相互作用（onNext の Menu ケース相当）
// ========================================================================

// cycleMenuItem() すると確認待ちが解除されること
void test_cycle_clears_confirm(void) {
    openMenuAndMoveTo(4);
    nav.onSelect();  // 確認待ちに入る
    TEST_ASSERT_TRUE(nav.awaitingConfirm());

    nav.cycleMenuItem();
    TEST_ASSERT_FALSE(nav.awaitingConfirm());
}

// ========================================================================
// 5. バリアント側 onLongPressB 用プリミティブ
// ========================================================================

// enterActive() は Active へ遷移し dirty を立てること
void test_enter_active_marks_dirty(void) {
    clearDirty();

    nav.enterActive();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Active),
                          static_cast<int>(nav.screen()));
    TEST_ASSERT_TRUE(nav.consumeDirty());
}

// cancelConfirm() は確認待ちのみ解除し、dirty を立てないこと
// （元実装の「Rematch 以外の確認先では再描画しない」挙動の維持）
void test_cancel_confirm_does_not_mark_dirty(void) {
    openMenuAndMoveTo(4);
    nav.onSelect();      // 確認待ちに入る（ここで dirty が立つ）
    clearDirty();        // 消費してクリーンにする
    TEST_ASSERT_TRUE(nav.awaitingConfirm());

    nav.cancelConfirm();
    TEST_ASSERT_FALSE(nav.awaitingConfirm());
    TEST_ASSERT_FALSE(nav.consumeDirty());  // dirty は立っていない
}

// cancelConfirm() 後も confirmTarget は保持されること
// （元 onLongPressB は解除後に confirmTarget を読んで分岐するため）
void test_cancel_confirm_keeps_target_readable(void) {
    openMenuAndMoveTo(4);
    nav.onSelect();
    nav.cancelConfirm();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Rematch),
                          static_cast<int>(nav.confirmTarget()));
}

// ========================================================================
// 6. dirty フラグの伝搬
// ========================================================================

// select による画面遷移で dirty が立つこと
void test_select_marks_dirty(void) {
    openMenuAndMoveTo(1);
    clearDirty();

    nav.onSelect();
    TEST_ASSERT_TRUE(nav.consumeDirty());
}

// close による画面遷移で dirty が立つこと
void test_close_menu_marks_dirty(void) {
    nav.enterActive();
    clearDirty();

    nav.onCloseMenu();
    TEST_ASSERT_TRUE(nav.consumeDirty());
}

// cycleMenuItem() で dirty が立つこと
void test_cycle_marks_dirty(void) {
    nav.enterActive();
    nav.onCloseMenu();
    clearDirty();

    nav.cycleMenuItem();
    TEST_ASSERT_TRUE(nav.consumeDirty());
}

// ========================================================================
// 7. 非初期状態からの reset() と確認待ち中の再選択
// ========================================================================

// About 表示まで進んだ非初期状態から reset() すると
// Setup / menuIndex 0 / 確認待ち解除 / dirty に戻ること
// （確認待ちも経由させ、confirmTarget の初期化も併せて検証する）
void test_reset_from_subscreen_restores_initial_state(void) {
    openMenuAndMoveTo(4);
    nav.onSelect();          // Rematch 確認待ちに入る
    TEST_ASSERT_TRUE(nav.awaitingConfirm());

    nav.cycleMenuItem();     // 確認解除 + カーソルを About（5）へ
    nav.onSelect();          // About 表示中の非初期状態を作る
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::About),
                          static_cast<int>(nav.screen()));

    nav.reset();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Setup),
                          static_cast<int>(nav.screen()));
    TEST_ASSERT_EQUAL_UINT8(0, nav.menuIndex());
    TEST_ASSERT_FALSE(nav.awaitingConfirm());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Resume),
                          static_cast<int>(nav.confirmTarget()));
    TEST_ASSERT_TRUE(nav.consumeDirty());   // 初期状態と同様に描画が必要
    TEST_ASSERT_FALSE(nav.consumeDirty());
}

// Rematch 確認待ち中に同じ項目を再選択しても確認待ちが維持され、
// dirty が立ち直ること（旧実装は confirming_ / confirmTarget_ を再設定して
// markDirty() するだけ。MenuNav も同一本文のため、ここで挙動を直接ロックする）
void test_reselect_rematch_while_confirming_keeps_state_and_marks_dirty(void) {
    openMenuAndMoveTo(4);
    nav.onSelect();          // 1 回目: 確認待ちに入る
    TEST_ASSERT_TRUE(nav.awaitingConfirm());
    clearDirty();            // dirty を消費してクリーンにする

    ScreenAction action = nav.onSelect();   // 2 回目: 同じ項目を再選択
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScreenAction::None),
                          static_cast<int>(action));
    // 確認待ち・対象は維持される
    TEST_ASSERT_TRUE(nav.awaitingConfirm());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuItem::Rematch),
                          static_cast<int>(nav.confirmTarget()));
    // 画面は Menu のまま
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Screen::Menu),
                          static_cast<int>(nav.screen()));
    // 再選択で dirty が立ち直る（旧実装の markDirty() 相当）
    TEST_ASSERT_TRUE(nav.consumeDirty());
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // 1. 初期状態とメニューインデックスの循環
    RUN_TEST(test_initial_state_is_setup_with_index_zero);
    RUN_TEST(test_cycle_advances_through_all_items);
    RUN_TEST(test_cycle_wraps_from_last_to_zero);
    RUN_TEST(test_cycle_multiple_rounds_returns_to_zero);

    // 2. 各 MenuItem の select 遷移先
    RUN_TEST(test_select_resume_returns_to_active);
    RUN_TEST(test_select_history_goes_to_history_screen);
    RUN_TEST(test_select_set_life_goes_to_setup_screen);
    RUN_TEST(test_select_set_sensitivity_goes_to_sensitivity_screen);
    RUN_TEST(test_select_rematch_enters_confirm);
    RUN_TEST(test_select_about_goes_to_about_screen);
    RUN_TEST(test_select_on_setup_does_nothing);
    RUN_TEST(test_select_on_active_does_nothing);

    // 3. close 時の戻り先
    RUN_TEST(test_close_menu_on_setup_is_noop_and_not_dirty);
    RUN_TEST(test_close_menu_on_active_opens_menu_at_index_zero);
    RUN_TEST(test_close_menu_on_menu_returns_to_active);
    RUN_TEST(test_close_menu_clears_confirm);
    RUN_TEST(test_close_menu_from_subscreens_returns_to_menu);

    // 4. カーソル移動と確認待ちの相互作用
    RUN_TEST(test_cycle_clears_confirm);

    // 5. バリアント側 onLongPressB 用プリミティブ
    RUN_TEST(test_enter_active_marks_dirty);
    RUN_TEST(test_cancel_confirm_does_not_mark_dirty);
    RUN_TEST(test_cancel_confirm_keeps_target_readable);

    // 6. dirty フラグの伝搬
    RUN_TEST(test_select_marks_dirty);
    RUN_TEST(test_close_menu_marks_dirty);
    RUN_TEST(test_cycle_marks_dirty);

    // 7. 非初期状態からの reset() と確認待ち中の再選択
    RUN_TEST(test_reset_from_subscreen_restores_initial_state);
    RUN_TEST(test_reselect_rematch_while_confirming_keeps_state_and_marks_dirty);

    return UNITY_END();
}
