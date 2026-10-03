// test/test_riftbound_screen/test_riftbound_screen.cpp
//
// Riftbound 版画面状態機械（RiftboundScreenState）のホスト単体テスト（L1）
// 設計の正は docs/16-riftbound-firmware-spec.md の画面状態機械。
//
// 共通 MenuNav（FaB 版と同一のコア）に委譲する部分は FaB / EDH の
// テストで検証済みである。ここでは Riftbound 固有の差分を検証する:
//   - Setup で設定する値が存在しない（A 短押しが何もしない）
//   - メニューのカーソル循環が SetLife をスキップする

#include <unity.h>
#include <cstdint>

#include "app/riftbound_screen_state.hpp"

using counter::riftbound::app::MenuItem;
using counter::riftbound::app::RiftboundScreenState;
using counter::riftbound::app::Screen;
using counter::riftbound::app::ScreenAction;

static RiftboundScreenState sc;

void setUp(void) {
    sc.reset();
    sc.consumeDirty();  // reset が立てた dirty を消費して状態をクリアにする
}

void tearDown(void) {
    // クリーンアップ不要
}

// ========================================================================
// 初期状態
// ========================================================================

// reset 後は Setup 画面から開始する
void test_reset_starts_in_setup(void) {
    TEST_ASSERT_EQUAL(Screen::Setup, sc.screen());
}

// ========================================================================
// Setup 画面
// ========================================================================

// Setup で B 短押しは何もしない（誤開始防止。確定は長押しのみ）
void test_setup_on_select_does_nothing(void) {
    ScreenAction action = sc.onSelect();
    TEST_ASSERT_EQUAL(ScreenAction::None, action);
    TEST_ASSERT_EQUAL(Screen::Setup, sc.screen());
}

// Setup で A 短押しは何もしない（FaB 版の 20/40 プリセットトグルに相当する
// 操作が Riftbound には存在しない。得点は常に 0 から始まるため）
void test_setup_on_next_does_nothing(void) {
    ScreenAction action = sc.onNext();
    TEST_ASSERT_EQUAL(ScreenAction::None, action);
    TEST_ASSERT_EQUAL(Screen::Setup, sc.screen());
}

// Setup で B 長押しすると StartMatch を返して Active に遷移する
void test_setup_long_press_b_starts_match(void) {
    ScreenAction action = sc.onLongPressB();
    TEST_ASSERT_EQUAL(ScreenAction::StartMatch, action);
    TEST_ASSERT_EQUAL(Screen::Active, sc.screen());
}

// Setup で A+B 長押しは何もしない（戻る先が無い）
void test_setup_on_close_menu_does_nothing(void) {
    ScreenAction action = sc.onCloseMenu();
    TEST_ASSERT_EQUAL(ScreenAction::None, action);
    TEST_ASSERT_EQUAL(Screen::Setup, sc.screen());
}

// ========================================================================
// メニューのカーソル循環（SetLife スキップ）
// ========================================================================

// Active からメニューを開くとカーソルは先頭（Resume）
void test_open_menu_from_active_starts_at_resume(void) {
    sc.onLongPressB();          // Setup → Active
    sc.consumeDirty();
    sc.onCloseMenu();           // Active → Menu

    TEST_ASSERT_EQUAL(Screen::Menu, sc.screen());
    TEST_ASSERT_EQUAL(MenuItem::Resume, sc.menuItem());
}

// A 短押しで Resume → History と進む
void test_menu_cycle_resume_to_history(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu, Resume

    sc.onNext();
    TEST_ASSERT_EQUAL(MenuItem::History, sc.menuItem());
}

// History の次は SetLife をスキップして Sensitivity になる
void test_menu_cycle_skips_set_life_forward(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu, Resume

    sc.onNext();                // History
    sc.onNext();                // SetLife をスキップして Sensitivity
    TEST_ASSERT_EQUAL(MenuItem::SetSensitivity, sc.menuItem());
    TEST_ASSERT_NOT_EQUAL(MenuItem::SetLife, sc.menuItem());
}

// About の次は SetLife を経由せず Resume へ戻る（循環）
void test_menu_cycle_skips_set_life_wrap_around(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu, Resume

    sc.onNext();                // History
    sc.onNext();                // Sensitivity
    sc.onNext();                // Rematch
    sc.onNext();                // About
    sc.onNext();                // SetLife をスキップして Resume へ
    TEST_ASSERT_EQUAL(MenuItem::Resume, sc.menuItem());
}

// メニューの全項目を循環しても SetLife に乗らない
void test_menu_cycle_never_lands_on_set_life(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu, Resume

    for (int i = 0; i < 12; ++i) {
        sc.onNext();
        TEST_ASSERT_NOT_EQUAL(MenuItem::SetLife, sc.menuItem());
    }
}

// ========================================================================
// メニューからの遷移
// ========================================================================

// Resume を選ぶと Active へ戻る
void test_select_resume_goes_active(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu, Resume

    ScreenAction action = sc.onSelect();
    TEST_ASSERT_EQUAL(ScreenAction::None, action);
    TEST_ASSERT_EQUAL(Screen::Active, sc.screen());
}

// History を選ぶと履歴画面へ遷移し、B 短押しで Menu に戻る
void test_select_history_and_back(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();
    sc.onNext();                // History

    sc.onSelect();
    TEST_ASSERT_EQUAL(Screen::History, sc.screen());

    sc.onSelect();
    TEST_ASSERT_EQUAL(Screen::Menu, sc.screen());
}

// Sensitivity を選ぶと感度画面へ遷移する
void test_select_sensitivity(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();
    sc.onNext();                // History
    sc.onNext();                // Sensitivity（SetLife スキップ）

    sc.onSelect();
    TEST_ASSERT_EQUAL(Screen::Sensitivity, sc.screen());
}

// About を選ぶと About 画面へ遷移する
void test_select_about(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();
    sc.onNext();                // History
    sc.onNext();                // Sensitivity
    sc.onNext();                // Rematch
    sc.onNext();                // About

    sc.onSelect();
    TEST_ASSERT_EQUAL(Screen::About, sc.screen());
}

// Rematch は確認待ちになり、長押しで確定する
void test_rematch_requires_long_press_confirm(void) {
    sc.onLongPressB();
    sc.consumeDirty();
    sc.onCloseMenu();
    sc.onNext();                // History
    sc.onNext();                // Sensitivity
    sc.onNext();                // Rematch

    // B 短押し: 確認待ちになるだけで実行はしない
    ScreenAction action = sc.onSelect();
    TEST_ASSERT_EQUAL(ScreenAction::None, action);
    TEST_ASSERT_TRUE(sc.awaitingConfirm());

    // 確認待ちで A 短押し（カーソル移動）すると確認待ちが解除される
    sc.onNext();
    TEST_ASSERT_FALSE(sc.awaitingConfirm());

    // もう一度 Rematch を選んで確認待ちにし、B 長押しで確定
    sc.onNext();                // Resume（About から循環）
    sc.onNext();                // History
    sc.onNext();                // Sensitivity（SetLife スキップ）
    sc.onNext();                // Rematch
    sc.onSelect();
    TEST_ASSERT_TRUE(sc.awaitingConfirm());

    action = sc.onLongPressB();
    TEST_ASSERT_EQUAL(ScreenAction::Rematch, action);
    TEST_ASSERT_EQUAL(Screen::Active, sc.screen());
}

// ========================================================================
// 感度プリセット
// ========================================================================

// Sensitivity 画面で A 短押しするとプリセットが循環する（1 → 2 → 0 → 1）
void test_sensitivity_preset_cycle(void) {
    sc.setSensitivityIndex(1);

    sc.onNext();  // Setup では無効
    TEST_ASSERT_EQUAL_UINT8(1, sc.sensitivityIndex());

    sc.onLongPressB();          // Active
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu
    sc.onNext();                // History
    sc.onNext();                // Sensitivity
    sc.onSelect();              // Sensitivity 画面へ

    sc.onNext();
    TEST_ASSERT_EQUAL_UINT8(2, sc.sensitivityIndex());
    sc.onNext();
    TEST_ASSERT_EQUAL_UINT8(0, sc.sensitivityIndex());
    sc.onNext();
    TEST_ASSERT_EQUAL_UINT8(1, sc.sensitivityIndex());
}

// setSensitivityIndex で dirty が立ち、consumeDirty で 1 回だけ消費される
void test_dirty_flag_is_one_shot(void) {
    sc.setSensitivityIndex(2);
    TEST_ASSERT_TRUE(sc.consumeDirty());
    TEST_ASSERT_FALSE(sc.consumeDirty());
}

// ========================================================================
// A+B 長押し（メニューを閉じる）
// ========================================================================

// Menu で A+B 長押しすると Active へ戻る
void test_close_menu_from_menu_goes_active(void) {
    sc.onLongPressB();          // Active
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu

    sc.onCloseMenu();
    TEST_ASSERT_EQUAL(Screen::Active, sc.screen());
}

// History で A+B 長押しすると Menu へ戻る
void test_close_menu_from_history_goes_menu(void) {
    sc.onLongPressB();          // Active
    sc.consumeDirty();
    sc.onCloseMenu();           // Menu
    sc.onNext();                // History
    sc.onSelect();              // History 画面

    sc.onCloseMenu();
    TEST_ASSERT_EQUAL(Screen::Menu, sc.screen());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_reset_starts_in_setup);
    RUN_TEST(test_setup_on_select_does_nothing);
    RUN_TEST(test_setup_on_next_does_nothing);
    RUN_TEST(test_setup_long_press_b_starts_match);
    RUN_TEST(test_setup_on_close_menu_does_nothing);
    RUN_TEST(test_open_menu_from_active_starts_at_resume);
    RUN_TEST(test_menu_cycle_resume_to_history);
    RUN_TEST(test_menu_cycle_skips_set_life_forward);
    RUN_TEST(test_menu_cycle_skips_set_life_wrap_around);
    RUN_TEST(test_menu_cycle_never_lands_on_set_life);
    RUN_TEST(test_select_resume_goes_active);
    RUN_TEST(test_select_history_and_back);
    RUN_TEST(test_select_sensitivity);
    RUN_TEST(test_select_about);
    RUN_TEST(test_rematch_requires_long_press_confirm);
    RUN_TEST(test_sensitivity_preset_cycle);
    RUN_TEST(test_dirty_flag_is_one_shot);
    RUN_TEST(test_close_menu_from_menu_goes_active);
    RUN_TEST(test_close_menu_from_history_goes_menu);

    return UNITY_END();
}
