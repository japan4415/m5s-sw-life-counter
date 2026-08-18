// test/test_button/test_button.cpp
//
// ButtonInput のホスト単体テスト（L1）
// 設計の正は docs/05-ui-ux.md の「物理ボタン」節。
// 実装ではなく設計書の要求を検証する。
//
// 状態機械: Idle -> AOnly/BOnly -> BothHeld -> Suppressed
//
// しきい値:
//   - A+B 長押し判定: 1000 ms（実装は >= で境界を含む）
//   - 単独長押し判定: 1000 ms（実装は >= で境界を含む）
//   - A+B 同時押しの合流に時間制限は無い（片方を押したまま考えてから
//     もう片方を押す操作も受け付ける）
//
// 現在 A+B 長押し判定と単独長押し判定は同値（1000 ms）。そのため、
// 片方だけ押し続けると 1000 ms 時点で単独長押しが先に成立し、
// その後もう片方を押しても A+B 長押し（MenuRequested）にはならない。
//
// 単独長押し:
//   AOnly / BOnly 状態で 1000 ms 以上押し続けると ALongPressed / BLongPressed
//   を 1 回だけ返し、Suppressed へ遷移する。その後は離しても短押しイベントを
//   返さない。1000 ms に達する前にもう片方が合流すれば BothHeld になる。
//
// 抑制 (Suppressed):
//   BothHeld を抜ける際、および単独長押し成立時は Suppressed へ遷移し、
//   両方のボタンが離されるまで全イベントを抑制する。メニューを開こうとして
//   失敗した場合や、長押し成立後に指を離した瞬間に Undo やロックが
//   誤発火するのを防ぐため。

#include <unity.h>
#include <cstdint>

#include "input/button_input.hpp"

using counter::input::ButtonInput;
using counter::input::ButtonEvent;

// ========================================================================
// 定数とヘルパ
// ========================================================================

// A+B 長押しの判定しきい値（テスト側で明示的に記述。実装の定数名に依存しない）
static constexpr uint32_t kMenuLongPressMs = 1000;

// 単独ボタン長押しの判定しきい値（テスト側で明示的に記述。実装の定数名に依存しない）
// kMenuLongPressMs と同値だが、意味が異なるため別定数として持つ。
// 将来片方だけ変わる可能性がある。
static constexpr uint32_t kSingleLongPressMs = 1000;

// 実測のタッチ間隔と同等の刻み幅（15 ms）
static constexpr uint32_t kTickMs = 15;

// scoped enum を比較するヘルパマクロ（test_gesture と同じ流儀）
#define ASSERT_EVENT_EQ(expected, actual) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), \
                          static_cast<int>(actual))

// テスト対象のインスタンス（setUp() で毎回 reset）
static ButtonInput btn;

void setUp(void) {
    btn.reset();
}

void tearDown(void) {
    // クリーンアップ不要
}

// ========================================================================
// ヘルパ関数
// ========================================================================

// 指定時間だけ A のみを押し続ける（kTickMs 刻み）。
// 途中で発生した非 None イベント（ALongPressed 等）を返す。
// 何もなければ None。押したままの状態で返る（離すのは呼び出し側）。
static ButtonEvent pressAFor(uint32_t& nowMs, uint32_t durationMs) {
    uint32_t endMs = nowMs + durationMs;
    ButtonEvent last = ButtonEvent::None;
    while (nowMs < endMs) {
        ButtonEvent ev = btn.update(true, false, nowMs);
        if (ev != ButtonEvent::None) {
            last = ev;
        }
        nowMs += kTickMs;
    }
    return last;
}

// 指定時間だけ B のみを押し続ける（kTickMs 刻み）。
// 途中で発生した非 None イベント（BLongPressed 等）を返す。
static ButtonEvent pressBFor(uint32_t& nowMs, uint32_t durationMs) {
    uint32_t endMs = nowMs + durationMs;
    ButtonEvent last = ButtonEvent::None;
    while (nowMs < endMs) {
        ButtonEvent ev = btn.update(false, true, nowMs);
        if (ev != ButtonEvent::None) {
            last = ev;
        }
        nowMs += kTickMs;
    }
    return last;
}

// 指定時間だけ A+B を同時に押し続ける（kTickMs 刻み）。
// MenuRequested が発生したらその時点のイベントを返す。
static ButtonEvent pressBothFor(uint32_t& nowMs, uint32_t durationMs) {
    uint32_t endMs = nowMs + durationMs;
    ButtonEvent last = ButtonEvent::None;
    while (nowMs < endMs) {
        ButtonEvent ev = btn.update(true, true, nowMs);
        if (ev != ButtonEvent::None) {
            last = ev;
        }
        nowMs += kTickMs;
    }
    return last;
}

// 指定時間だけ何も押さない（kTickMs 刻み）。
static ButtonEvent releaseFor(uint32_t& nowMs, uint32_t durationMs) {
    uint32_t endMs = nowMs + durationMs;
    ButtonEvent last = ButtonEvent::None;
    while (nowMs < endMs) {
        ButtonEvent ev = btn.update(false, false, nowMs);
        if (ev != ButtonEvent::None) {
            last = ev;
        }
        nowMs += kTickMs;
    }
    return last;
}

// ========================================================================
// 基本テスト
// ========================================================================

// A を押して離す → UndoRequested が離した瞬間に 1 回だけ返る
void test_a_press_release_returns_undo(void) {
    uint32_t t = 0;

    // A を 100ms 押し続ける（押している間は None）
    pressAFor(t, 100);

    // A を離した瞬間に UndoRequested が返る
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
    t += kTickMs;

    // 2 回目以降は None（1 回だけ）
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// B を押して離す → LockToggleRequested が離した瞬間に 1 回だけ返る
void test_b_press_release_returns_lock_toggle(void) {
    uint32_t t = 0;

    // B を 100ms 押し続ける（押している間は None）
    pressBFor(t, 100);

    // B を離した瞬間に LockToggleRequested が返る
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::LockToggleRequested, ev);
    t += kTickMs;

    // 2 回目以降は None（1 回だけ）
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// 何も押していない間は None が返り続ける
void test_no_press_returns_none(void) {
    uint32_t t = 0;
    for (int i = 0; i < 20; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
        t += kTickMs;
    }
}

// 押している最中は None が返ること（押した瞬間に確定しないこと）
void test_a_held_returns_none_until_release(void) {
    uint32_t t = 0;

    // A を押した瞬間
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, false, t));
    t += kTickMs;

    // 押し続けている間ずっと None
    for (int i = 0; i < 30; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, false, t));
        t += kTickMs;
    }

    // 離した瞬間にイベントが確定する
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, btn.update(false, false, t));
}

// B も同様に押している最中は None
void test_b_held_returns_none_until_release(void) {
    uint32_t t = 0;

    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));
    t += kTickMs;

    for (int i = 0; i < 30; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));
        t += kTickMs;
    }

    ASSERT_EVENT_EQ(ButtonEvent::LockToggleRequested, btn.update(false, false, t));
}

// ========================================================================
// A+B 長押しテスト（本丸）
// ========================================================================

// A と B を同時に押し、しきい値を超えて保持 → MenuRequested が 1 回だけ返る
void test_both_held_past_threshold_returns_menu(void) {
    uint32_t t = 0;

    // A+B を同時に押してしきい値を超えるまで保持
    ButtonEvent ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// MenuRequested が返った後、両方を離すまで他のイベントが返らないこと
// （指を離す瞬間に Undo やロックが誤発火しないこと）
void test_after_menu_fired_no_events_until_both_released(void) {
    uint32_t t = 0;

    // A+B 長押しで MenuRequested を発火させる
    pressBothFor(t, kMenuLongPressMs + 100);

    // まだ両方押している状態で None が返り続ける
    for (int i = 0; i < 10; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));
        t += kTickMs;
    }

    // A だけ離す（B はまだ押している）→ None（LockToggleRequested ではない）
    for (int i = 0; i < 10; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));
        t += kTickMs;
    }

    // B も離す → None（ここで Idle に戻る）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
    t += kTickMs;

    // 完全に離した後も None
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
}

// 両方を離した後、改めて A を押して離すと UndoRequested が返ること
// （状態が正しく Idle に戻ること）
void test_after_menu_state_resets_correctly(void) {
    uint32_t t = 0;

    // A+B 長押しで MenuRequested を発火
    pressBothFor(t, kMenuLongPressMs + 100);

    // 両方離す
    releaseFor(t, 100);

    // 改めて A を押して離す → UndoRequested が返る
    pressAFor(t, 100);
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
}

// A を先に押し、少し遅れて B を押した場合も A+B として成立すること
// （人間の指では完全同時押しは不可能）
void test_a_first_then_b_joins_both_held(void) {
    uint32_t t = 0;

    // A を先に 50ms 押す
    pressAFor(t, 50);

    // B が合流して A+B 状態になる。しきい値を超えるまで保持
    ButtonEvent ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// B を先に押し、少し遅れて A を押した場合も A+B として成立すること
void test_b_first_then_a_joins_both_held(void) {
    uint32_t t = 0;

    // B を先に 50ms 押す
    pressBFor(t, 50);

    // A が合流して A+B 状態になる。しきい値を超えるまで保持
    ButtonEvent ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// A+B を押したがしきい値に届かず A を先に離した場合、
// B を離した瞬間にも LockToggleRequested が誤発火しないこと。
// BothHeld → Suppressed → 両方離されて Idle。全経路で None。
void test_both_a_released_first_suppresses_all_events(void) {
    uint32_t t = 0;

    // A+B を 500ms だけ押す（しきい値に届かない）
    pressBothFor(t, 500);

    // A だけ離す（B はまだ押している）
    // 実装: BothHeld → Suppressed, return None
    ButtonEvent ev = btn.update(false, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // B がまだ押されている → Suppressed のまま None
    ev = btn.update(false, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // B も離す → Suppressed → Idle, None（LockToggleRequested ではない）
    // メニューを開こうとして失敗しただけなので、意図しない操作を起こさない
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// A+B を押したがしきい値に届かず B を先に離した場合、
// A を離した瞬間にも UndoRequested が誤発火しないこと。
// BothHeld → Suppressed → 両方離されて Idle。全経路で None。
void test_both_b_released_first_suppresses_all_events(void) {
    uint32_t t = 0;

    // A+B を 500ms だけ押す（しきい値未到達）
    pressBothFor(t, 500);

    // B だけ離す（A はまだ押している）
    // 実装: BothHeld → Suppressed, return None
    ButtonEvent ev = btn.update(true, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // A がまだ押されている → Suppressed のまま None
    ev = btn.update(true, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // A も離す → Suppressed → Idle, None（UndoRequested ではない）
    // メニューを開こうとして失敗しただけなので、意図しない Undo を起こさない
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// A+B 同時押し後のしきい値未到達で、両方同時に離した場合。
// BothHeld → Suppressed → 即 Idle（両方離されているため）。全経路で None。
void test_both_released_simultaneously_before_threshold(void) {
    uint32_t t = 0;

    // A+B を 500ms だけ押す（しきい値未到達）
    pressBothFor(t, 500);

    // 両方同時に離す → BothHeld → Suppressed → Idle, None
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // その後も None（Idle に戻っている）
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// MenuRequested は 1 回だけ返り、そのまま押し続けても再発火しない
void test_menu_fires_only_once(void) {
    uint32_t t = 0;
    int menuCount = 0;

    // A+B を十分長く押し続ける（しきい値の 3 倍）
    uint32_t endMs = kMenuLongPressMs * 3;
    while (t < endMs) {
        ButtonEvent ev = btn.update(true, true, t);
        if (ev == ButtonEvent::MenuRequested) {
            menuCount++;
        }
        t += kTickMs;
    }

    // MenuRequested は正確に 1 回だけ
    TEST_ASSERT_EQUAL_INT(1, menuCount);
}

// ========================================================================
// 境界値テスト
// ========================================================================

// しきい値ちょうどの時刻で MenuRequested が発火すること（>= なので含む）
void test_threshold_exact_fires_menu(void) {
    uint32_t t = 0;

    // A+B を同時に押した瞬間（Idle → BothHeld, pressMs_ = 0）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));

    // ちょうど kMenuLongPressMs 後（nowMs - pressMs_ == kMenuLongPressMs >= kMenuLongPressMs → true）
    t = kMenuLongPressMs;
    ButtonEvent ev = btn.update(true, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// しきい値の 1ms 手前では MenuRequested が発火しないこと
void test_threshold_minus_one_does_not_fire(void) {
    uint32_t t = 0;

    // A+B を同時に押す
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));

    // kMenuLongPressMs - 1 後（nowMs - pressMs_ == 999 < 1000 → false）
    t = kMenuLongPressMs - 1;
    ButtonEvent ev = btn.update(true, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);

    // ここで離すと None（BothHeld 解除）
    ev = btn.update(false, false, t + 1);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// reset() を呼ぶと状態が初期化され、進行中の押下が確定しないこと
void test_reset_clears_state(void) {
    uint32_t t = 0;

    // A を押し始める
    btn.update(true, false, t);
    t += kTickMs;
    btn.update(true, false, t);
    t += kTickMs;

    // 途中で reset()
    btn.reset();

    // A を離しても UndoRequested は返らない（状態が Idle にリセットされたため）
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// reset() で A+B 長押し進行中の状態もクリアされること
void test_reset_clears_both_held_state(void) {
    uint32_t t = 0;

    // A+B をしきい値の半分だけ押す（しきい値未到達）
    pressBothFor(t, kMenuLongPressMs / 2);

    // reset()
    btn.reset();

    // A+B を改めて kMenuLongPressMs - 100（= 900ms）だけ押す
    // （リセット前の押下時間は加算されない）
    pressBothFor(t, kMenuLongPressMs - 100);

    // まだしきい値未到達なので None
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));

    // 離しても None（しきい値未到達）
    ButtonEvent ev = btn.update(false, false, t + kTickMs);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// 同じ nowMs で複数回 update() を呼んでも壊れないこと
void test_same_now_ms_multiple_calls_no_crash(void) {
    uint32_t t = 100;

    // 同じ時刻で何回呼んでも壊れない（Idle 状態）
    for (int i = 0; i < 10; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
    }

    // A を押して同じ時刻で複数回呼ぶ
    btn.update(true, false, t);
    btn.update(true, false, t);
    btn.update(true, false, t);

    // 離す
    t += kTickMs;
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
}

// 同じ nowMs で A+B を複数回 update しても壊れないこと
void test_same_now_ms_both_pressed_no_crash(void) {
    uint32_t t = 0;

    // A+B を押して同じ時刻で繰り返し呼ぶ
    for (int i = 0; i < 10; ++i) {
        btn.update(true, true, t);
    }

    // しきい値丁度まで進める
    t = kMenuLongPressMs;
    ButtonEvent ev = btn.update(true, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// nowMs が単調増加する前提で、長時間経過（数分）でも正しく動くこと
void test_long_duration_works_correctly(void) {
    // 3 分後（180,000 ms）に操作を開始
    uint32_t t = 180000;

    // A を押して離す → 正常に UndoRequested
    btn.update(true, false, t);
    t += 100;
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
    t += kTickMs;

    // さらに 5 分後（300,000 ms）に A+B 長押し
    t = 480000;
    btn.update(true, true, t);

    // しきい値を超えた時刻で確認
    t += kMenuLongPressMs;
    ev = btn.update(true, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// uint32_t の大きな値（約 49 日相当、2^31 付近）でも動作すること
void test_large_now_ms_values(void) {
    uint32_t t = 2147483000u; // 2^31 付近

    btn.update(true, false, t);
    t += 100;
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
}

// ========================================================================
// 複合シナリオ
// ========================================================================

// A 短押し → B 短押し → A+B 長押し → A 短押しの連続操作
void test_sequential_operations(void) {
    uint32_t t = 0;

    // 1. A 短押し
    pressAFor(t, 100);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, btn.update(false, false, t));
    t += kTickMs;
    releaseFor(t, 50);

    // 2. B 短押し
    pressBFor(t, 100);
    ASSERT_EVENT_EQ(ButtonEvent::LockToggleRequested, btn.update(false, false, t));
    t += kTickMs;
    releaseFor(t, 50);

    // 3. A+B 長押し
    ButtonEvent ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);

    // 両方離す
    releaseFor(t, 100);

    // 4. A 短押し（状態が正しくリセットされていること）
    pressAFor(t, 100);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, btn.update(false, false, t));
}

// A を先に押し、200ms 遅れて B を押した場合も A+B として成立すること。
// 合流に時間制限は設けない設計（片方を押したまま考えてからもう片方を押す操作も可）。
void test_a_then_b_with_large_delay_still_works(void) {
    uint32_t t = 0;

    // A を 200ms 先に押す
    pressAFor(t, 200);

    // B が合流 → 合流に時間制限が無いので A+B として成立する
    ButtonEvent ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// A を 10 秒押し続けると kSingleLongPressMs 時点で ALongPressed が成立し、
// その後離しても UndoRequested（短押し）は返らないこと
void test_a_held_past_1000ms_fires_long_press_not_undo(void) {
    uint32_t t = 0;

    // A を 10 秒（10000ms）押し続ける
    // kSingleLongPressMs 時点で ALongPressed が発火し Suppressed へ遷移する
    ButtonEvent ev = pressAFor(t, 10000);
    ASSERT_EVENT_EQ(ButtonEvent::ALongPressed, ev);

    // 離した瞬間に UndoRequested は返らない（Suppressed → Idle, None）
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // Idle に戻った後も None
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// B を 10 秒押し続けると kSingleLongPressMs 時点で BLongPressed が成立し、
// その後離しても LockToggleRequested（短押し）は返らないこと
void test_b_held_past_1000ms_fires_long_press_not_lock_toggle(void) {
    uint32_t t = 0;

    // B を 10 秒（10000ms）押し続ける
    // kSingleLongPressMs 時点で BLongPressed が発火し Suppressed へ遷移する
    ButtonEvent ev = pressBFor(t, 10000);
    ASSERT_EVENT_EQ(ButtonEvent::BLongPressed, ev);

    // 離した瞬間に LockToggleRequested は返らない（Suppressed → Idle, None）
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // Idle に戻った後も None
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
}

// Suppressed 状態で A だけ離し、B を押し続けてからもう一度 A を押しても
// イベントが返らないこと（Suppressed は両方離されるまで維持される）
void test_suppressed_partial_release_and_repress(void) {
    uint32_t t = 0;

    // A+B 長押しで MenuRequested を発火（→ Suppressed）
    pressBothFor(t, kMenuLongPressMs + 100);

    // A だけ離す（B はまだ押している）
    for (int i = 0; i < 5; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));
        t += kTickMs;
    }

    // A を再度押す（両方押した状態に戻る）→ まだ Suppressed なので None
    for (int i = 0; i < 5; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));
        t += kTickMs;
    }

    // A だけ離す → None
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));
    t += kTickMs;

    // B も離す → None（Idle に戻る）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
}

// しきい値未到達で Suppressed を経由した後、改めて操作すると正常にイベントが返ること。
// Suppressed → 両方離して Idle → A 短押し → UndoRequested
void test_after_suppressed_state_resets_correctly(void) {
    uint32_t t = 0;

    // A+B を 500ms だけ押す（しきい値未到達）
    pressBothFor(t, 500);

    // 両方離す（Suppressed → Idle）
    releaseFor(t, 100);

    // 改めて A を押して離す → 正常に UndoRequested が返る
    pressAFor(t, 100);
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
    t += kTickMs;

    // 改めて B を押して離す → 正常に LockToggleRequested が返る
    pressBFor(t, 100);
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::LockToggleRequested, ev);
}

// A+B 同時押し後のしきい値未到達で、両方同時に離して何も起きないこと
void test_both_held_then_simultaneous_release_no_spurious_events(void) {
    uint32_t t = 0;

    // A+B を 500ms 押す（しきい値未到達）
    pressBothFor(t, 500);

    // 両方同時に離す
    ButtonEvent ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // しばらく何もしない → None が続く
    for (int i = 0; i < 10; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
        t += kTickMs;
    }
}

// BothHeld 状態で pressMs_ がリセットされることの確認
// A を先に 500ms 押してから B が合流した場合、
// BothHeld の pressMs_ は B が合流した時点からカウントされる
void test_both_held_timer_starts_from_join(void) {
    uint32_t t = 0;

    // A を 500ms 先に押す
    pressAFor(t, 500);  // t は 510 付近（kTickMs=15 で 33 回ループ後 t=495+15=510）

    uint32_t joinTime = t;  // B が合流した時刻を記録

    // B が合流して BothHeld に遷移。しきい値 - 100ms 押す（まだ到達しない）
    pressBothFor(t, kMenuLongPressMs - 100);

    // まだ MenuRequested は出ていない → 最後の update は None のはず
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));
    t += kTickMs;

    // あと 200ms 追加で押す → しきい値を超えて MenuRequested
    ButtonEvent ev = pressBothFor(t, 200);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// ========================================================================
// 単独ボタン長押しテスト
// ========================================================================

// A を kSingleLongPressMs 以上押し続けると ALongPressed が 1 回だけ返ること。
// そのまま押し続けても 2 回目は返らない。
void test_a_single_long_press_fires_once(void) {
    uint32_t t = 0;
    int longPressCount = 0;

    // A をしきい値の 3 倍（3000ms）押し続ける
    uint32_t endMs = kSingleLongPressMs * 3;
    while (t < endMs) {
        ButtonEvent ev = btn.update(true, false, t);
        if (ev == ButtonEvent::ALongPressed) {
            longPressCount++;
        }
        t += kTickMs;
    }

    // ALongPressed は正確に 1 回だけ
    TEST_ASSERT_EQUAL_INT(1, longPressCount);
}

// B を kSingleLongPressMs 以上押し続けると BLongPressed が 1 回だけ返ること。
void test_b_single_long_press_fires_once(void) {
    uint32_t t = 0;
    int longPressCount = 0;

    // B をしきい値の 3 倍（3000ms）押し続ける
    uint32_t endMs = kSingleLongPressMs * 3;
    while (t < endMs) {
        ButtonEvent ev = btn.update(false, true, t);
        if (ev == ButtonEvent::BLongPressed) {
            longPressCount++;
        }
        t += kTickMs;
    }

    // BLongPressed は正確に 1 回だけ
    TEST_ASSERT_EQUAL_INT(1, longPressCount);
}

// A 単独長押しの境界値: kSingleLongPressMs - 1 (= 999ms) では成立しないこと。
// 離した瞬間に UndoRequested（短押し）が返ること。
void test_a_single_long_press_boundary_999ms_no_fire(void) {
    uint32_t t = 0;

    // A を t=0 で押す（Idle → AOnly, pressMs_=0）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, false, t));

    // t=kSingleLongPressMs-1 で update（nowMs - pressMs_ = 999 < 1000 → しきい値未達）
    t = kSingleLongPressMs - 1;
    ButtonEvent ev = btn.update(true, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);

    // 離す → UndoRequested（短押しとして成立する）
    t += 1;
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
}

// A 単独長押しの境界値: kSingleLongPressMs (= 1000ms) ちょうどで成立すること（>= なので含む）
void test_a_single_long_press_boundary_1000ms_fires(void) {
    uint32_t t = 0;

    // A を t=0 で押す（Idle → AOnly, pressMs_=0）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, false, t));

    // t=kSingleLongPressMs で update（nowMs - pressMs_ = 1000 >= 1000 → true）
    t = kSingleLongPressMs;
    ButtonEvent ev = btn.update(true, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::ALongPressed, ev);
}

// B 単独長押しの境界値: kSingleLongPressMs - 1 (= 999ms) では成立しないこと。
void test_b_single_long_press_boundary_999ms_no_fire(void) {
    uint32_t t = 0;

    // B を t=0 で押す（Idle → BOnly, pressMs_=0）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));

    // t=kSingleLongPressMs-1 で update（nowMs - pressMs_ = 999 < 1000 → しきい値未達）
    t = kSingleLongPressMs - 1;
    ButtonEvent ev = btn.update(false, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);

    // 離す → LockToggleRequested（短押しとして成立する）
    t += 1;
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::LockToggleRequested, ev);
}

// B 単独長押しの境界値: kSingleLongPressMs (= 1000ms) ちょうどで成立すること（>= なので含む）
void test_b_single_long_press_boundary_1000ms_fires(void) {
    uint32_t t = 0;

    // B を t=0 で押す（Idle → BOnly, pressMs_=0）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));

    // t=kSingleLongPressMs で update（nowMs - pressMs_ = 1000 >= 1000 → true）
    t = kSingleLongPressMs;
    ButtonEvent ev = btn.update(false, true, t);
    ASSERT_EVENT_EQ(ButtonEvent::BLongPressed, ev);
}

// A 単独長押し成立後、両方離して Idle に戻り、
// 改めて A 短押しが正常に動くこと
void test_a_long_press_then_idle_short_press_works(void) {
    uint32_t t = 0;

    // A を kSingleLongPressMs + 100 ms 押す → ALongPressed → Suppressed
    ButtonEvent ev = pressAFor(t, kSingleLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::ALongPressed, ev);

    // A を離す（Suppressed → Idle）
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // しばらく何もしない
    releaseFor(t, 50);

    // 改めて A を短押しして離す → UndoRequested が返る
    pressAFor(t, 100);
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
}

// B 単独長押し成立後、両方離して Idle に戻り、
// 改めて B 短押しが正常に動くこと
void test_b_long_press_then_idle_short_press_works(void) {
    uint32_t t = 0;

    // B を kSingleLongPressMs + 100 ms 押す → BLongPressed → Suppressed
    ButtonEvent ev = pressBFor(t, kSingleLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::BLongPressed, ev);

    // B を離す（Suppressed → Idle）
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);
    t += kTickMs;

    // しばらく何もしない
    releaseFor(t, 50);

    // 改めて B を短押しして離す → LockToggleRequested が返る
    pressBFor(t, 100);
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::LockToggleRequested, ev);
}

// kSingleLongPressMs 以内にもう片方（B）を押せば BothHeld になること。
// 単独長押しに取られないこと。
void test_a_before_1000ms_b_joins_both_held(void) {
    uint32_t t = 0;

    // A を kSingleLongPressMs - 100 ms 押す（しきい値に届かないので AOnly のまま）
    pressAFor(t, kSingleLongPressMs - 100);

    // B が合流 → AOnly → BothHeld。単独長押しに取られない
    ButtonEvent ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// B を kSingleLongPressMs - 100 ms 押してから A が合流しても BothHeld になること。
void test_b_before_1000ms_a_joins_both_held(void) {
    uint32_t t = 0;

    // B を kSingleLongPressMs - 100 ms 押す（しきい値に届かないので BOnly のまま）
    pressBFor(t, kSingleLongPressMs - 100);

    // A が合流 → BOnly → BothHeld
    ButtonEvent ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// kSingleLongPressMs を超えて A 単独長押しが成立（Suppressed）した後は、
// もう片方（B）を押しても BothHeld にはならないこと
void test_a_after_1000ms_suppressed_b_no_both_held(void) {
    uint32_t t = 0;

    // A を kSingleLongPressMs + 100 ms 押す → kSingleLongPressMs で ALongPressed → Suppressed
    ButtonEvent ev = pressAFor(t, kSingleLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::ALongPressed, ev);

    // Suppressed 状態で B も押す → BothHeld にはならず None
    for (int i = 0; i < 10; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));
        t += kTickMs;
    }

    // A だけ離す → まだ B が押されているので Suppressed のまま
    for (int i = 0; i < 5; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, true, t));
        t += kTickMs;
    }

    // B も離す → Idle に戻る
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
    t += kTickMs;

    // Idle に戻ったことを確認（A 短押しが正常に動く）
    pressAFor(t, 100);
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
}

// B 単独長押しが成立（Suppressed）した後は、
// もう片方（A）を押しても BothHeld にはならないこと
void test_b_after_1000ms_suppressed_a_no_both_held(void) {
    uint32_t t = 0;

    // B を kSingleLongPressMs + 100 ms 押す → kSingleLongPressMs で BLongPressed → Suppressed
    ButtonEvent ev = pressBFor(t, kSingleLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::BLongPressed, ev);

    // Suppressed 状態で A も押す → BothHeld にはならず None
    for (int i = 0; i < 10; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));
        t += kTickMs;
    }

    // 両方離す → Idle に戻る
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
}

// ========================================================================
// 同値しきい値の境界テスト
// kMenuLongPressMs == kSingleLongPressMs == 1000 ms の場合に
// 境界がぶつかるケースを検証する。
// ========================================================================

// A を kSingleLongPressMs 未満（985ms = kSingleLongPressMs - kTickMs）で
// B が合流し、そこから kMenuLongPressMs 以上保持すると MenuRequested が返ること。
// BothHeld に入った時点で pressMs_ がリセットされるため、合計では
// kSingleLongPressMs を超えるが、単独長押しにはならない。
void test_equal_thresholds_a_under_then_b_joins_menu(void) {
    uint32_t t = 0;

    // A を kSingleLongPressMs - kTickMs（= 985ms）だけ押す。
    // 15ms 刻みなので kSingleLongPressMs には届かず ALongPressed は発火しない
    ButtonEvent ev = pressAFor(t, kSingleLongPressMs - kTickMs);
    ASSERT_EVENT_EQ(ButtonEvent::None, ev);

    // B が合流。AOnly → BothHeld に入り、pressMs_ がリセットされる。
    // そこから kMenuLongPressMs + 100ms 保持する
    ev = pressBothFor(t, kMenuLongPressMs + 100);
    ASSERT_EVENT_EQ(ButtonEvent::MenuRequested, ev);
}

// A を正確に kSingleLongPressMs（= 1000ms）押すと ALongPressed が先に成立し
// （Suppressed へ遷移）、その後 B を押しても MenuRequested にならないこと。
// 同値であるため、片方だけ押し続けると単独長押しが先に成立する。
void test_equal_thresholds_a_exact_then_b_no_menu(void) {
    uint32_t t = 0;

    // A を t=0 で押す（Idle → AOnly, pressMs_=0）
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, false, t));

    // t=kSingleLongPressMs: nowMs - pressMs_ = 1000 >= 1000 → ALongPressed → Suppressed
    t = kSingleLongPressMs;
    ButtonEvent ev = btn.update(true, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::ALongPressed, ev);
    t += kTickMs;

    // Suppressed 状態で B も押す → BothHeld にはならず None
    for (int i = 0; i < 10; ++i) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));
        t += kTickMs;
    }

    // kMenuLongPressMs 以上保持しても MenuRequested にならない
    uint32_t endMs = t + kMenuLongPressMs + 100;
    while (t < endMs) {
        ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(true, true, t));
        t += kTickMs;
    }

    // 両方離す → Idle に戻る
    ASSERT_EVENT_EQ(ButtonEvent::None, btn.update(false, false, t));
    t += kTickMs;

    // Idle に戻ったことを確認（A 短押しが正常に動く）
    pressAFor(t, 100);
    ev = btn.update(false, false, t);
    ASSERT_EVENT_EQ(ButtonEvent::UndoRequested, ev);
}

// ========================================================================
// heldMs() テスト
// ========================================================================

// Idle 状態では heldMs() は 0 を返すこと
void test_held_ms_idle_returns_zero(void) {
    uint32_t t = 100;

    // 何も押していない → Idle
    btn.update(false, false, t);
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));

    t += kTickMs;
    btn.update(false, false, t);
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));
}

// AOnly 状態では heldMs() は A を押してからの経過時間を返すこと
void test_held_ms_a_only_returns_elapsed(void) {
    uint32_t t = 0;

    // A を t=0 で押す（Idle → AOnly, pressMs_=0）
    btn.update(true, false, t);

    // 直後は 0
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));

    t += kTickMs;
    btn.update(true, false, t);
    // t=15 時点。pressMs_=0 なので 15ms 経過
    TEST_ASSERT_EQUAL_UINT32(15, btn.heldMs(t));

    t += kTickMs;
    btn.update(true, false, t);
    // t=30 時点。30ms 経過
    TEST_ASSERT_EQUAL_UINT32(30, btn.heldMs(t));

    // 200ms まで進める
    t = 200;
    btn.update(true, false, t);
    TEST_ASSERT_EQUAL_UINT32(200, btn.heldMs(t));
}

// BOnly 状態では heldMs() は B を押してからの経過時間を返すこと
void test_held_ms_b_only_returns_elapsed(void) {
    uint32_t t = 50;

    // B を t=50 で押す（Idle → BOnly, pressMs_=50）
    btn.update(false, true, t);
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));

    t += 100;
    btn.update(false, true, t);
    // t=150, pressMs_=50 → 100ms 経過
    TEST_ASSERT_EQUAL_UINT32(100, btn.heldMs(t));
}

// BothHeld 状態では heldMs() は後から合流した時点からの経過時間を返すこと。
// A を先に押してから B が合流した場合、BothHeld に入った時点（= B が押された時点）
// から計測される。
void test_held_ms_both_held_from_join_time(void) {
    uint32_t t = 0;

    // A を t=0 で押す
    btn.update(true, false, t);

    // AOnly 状態。t=100 で heldMs は 100
    t = 100;
    btn.update(true, false, t);
    TEST_ASSERT_EQUAL_UINT32(100, btn.heldMs(t));

    // t=100 で B が合流 → BothHeld, pressMs_ = 100
    btn.update(true, true, t);

    // BothHeld に入った直後。heldMs = t - pressMs_ = 100 - 100 = 0
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));

    // 500ms 経過後
    t = 600;
    btn.update(true, true, t);
    // heldMs = 600 - 100 = 500
    TEST_ASSERT_EQUAL_UINT32(500, btn.heldMs(t));
}

// Suppressed 状態では heldMs() は 0 を返すこと
void test_held_ms_suppressed_returns_zero(void) {
    uint32_t t = 0;

    // A を kSingleLongPressMs + 100 ms 押す → kSingleLongPressMs で ALongPressed → Suppressed
    pressAFor(t, kSingleLongPressMs + 100);

    // Suppressed 状態。ボタンはまだ押されているが heldMs は 0
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));

    // 時間が進んでも 0
    t += 500;
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));
}

// A+B 長押しによる Suppressed でも heldMs() は 0 を返すこと
void test_held_ms_suppressed_after_menu_returns_zero(void) {
    uint32_t t = 0;

    // A+B を長押しして MenuRequested → Suppressed
    pressBothFor(t, kMenuLongPressMs + 100);

    // Suppressed 状態。heldMs は 0
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));
}

// heldMs() が update() を呼ばなくても正しく動くこと（読み取り専用の確認）
void test_held_ms_is_read_only(void) {
    uint32_t t = 0;

    // A を押す
    btn.update(true, false, t);

    // update() を呼ばずに heldMs() を複数回呼んでも同じ値
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));

    // 異なる nowMs で呼ぶと異なる値（状態は変わらない）
    TEST_ASSERT_EQUAL_UINT32(100, btn.heldMs(t + 100));
    TEST_ASSERT_EQUAL_UINT32(200, btn.heldMs(t + 200));

    // 元の t で呼んでも変わらず 0（副作用がないことの確認）
    TEST_ASSERT_EQUAL_UINT32(0, btn.heldMs(t));
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // 基本テスト
    RUN_TEST(test_a_press_release_returns_undo);
    RUN_TEST(test_b_press_release_returns_lock_toggle);
    RUN_TEST(test_no_press_returns_none);
    RUN_TEST(test_a_held_returns_none_until_release);
    RUN_TEST(test_b_held_returns_none_until_release);

    // A+B 長押しテスト
    RUN_TEST(test_both_held_past_threshold_returns_menu);
    RUN_TEST(test_after_menu_fired_no_events_until_both_released);
    RUN_TEST(test_after_menu_state_resets_correctly);
    RUN_TEST(test_a_first_then_b_joins_both_held);
    RUN_TEST(test_b_first_then_a_joins_both_held);
    RUN_TEST(test_both_a_released_first_suppresses_all_events);
    RUN_TEST(test_both_b_released_first_suppresses_all_events);
    RUN_TEST(test_both_released_simultaneously_before_threshold);
    RUN_TEST(test_menu_fires_only_once);

    // 境界値テスト
    RUN_TEST(test_threshold_exact_fires_menu);
    RUN_TEST(test_threshold_minus_one_does_not_fire);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_reset_clears_both_held_state);
    RUN_TEST(test_same_now_ms_multiple_calls_no_crash);
    RUN_TEST(test_same_now_ms_both_pressed_no_crash);
    RUN_TEST(test_long_duration_works_correctly);
    RUN_TEST(test_large_now_ms_values);

    // 複合シナリオ
    RUN_TEST(test_sequential_operations);
    RUN_TEST(test_a_then_b_with_large_delay_still_works);
    RUN_TEST(test_a_held_past_1000ms_fires_long_press_not_undo);
    RUN_TEST(test_b_held_past_1000ms_fires_long_press_not_lock_toggle);
    RUN_TEST(test_suppressed_partial_release_and_repress);
    RUN_TEST(test_after_suppressed_state_resets_correctly);
    RUN_TEST(test_both_held_then_simultaneous_release_no_spurious_events);
    RUN_TEST(test_both_held_timer_starts_from_join);

    // 単独ボタン長押しテスト
    RUN_TEST(test_a_single_long_press_fires_once);
    RUN_TEST(test_b_single_long_press_fires_once);
    RUN_TEST(test_a_single_long_press_boundary_999ms_no_fire);
    RUN_TEST(test_a_single_long_press_boundary_1000ms_fires);
    RUN_TEST(test_b_single_long_press_boundary_999ms_no_fire);
    RUN_TEST(test_b_single_long_press_boundary_1000ms_fires);
    RUN_TEST(test_a_long_press_then_idle_short_press_works);
    RUN_TEST(test_b_long_press_then_idle_short_press_works);
    RUN_TEST(test_a_before_1000ms_b_joins_both_held);
    RUN_TEST(test_b_before_1000ms_a_joins_both_held);
    RUN_TEST(test_a_after_1000ms_suppressed_b_no_both_held);
    RUN_TEST(test_b_after_1000ms_suppressed_a_no_both_held);

    // 同値しきい値の境界テスト
    RUN_TEST(test_equal_thresholds_a_under_then_b_joins_menu);
    RUN_TEST(test_equal_thresholds_a_exact_then_b_no_menu);

    // heldMs() テスト
    RUN_TEST(test_held_ms_idle_returns_zero);
    RUN_TEST(test_held_ms_a_only_returns_elapsed);
    RUN_TEST(test_held_ms_b_only_returns_elapsed);
    RUN_TEST(test_held_ms_both_held_from_join_time);
    RUN_TEST(test_held_ms_suppressed_returns_zero);
    RUN_TEST(test_held_ms_suppressed_after_menu_returns_zero);
    RUN_TEST(test_held_ms_is_read_only);

    return UNITY_END();
}
