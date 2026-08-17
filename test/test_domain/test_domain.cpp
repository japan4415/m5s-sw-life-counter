// test/test_domain/test_domain.cpp
//
// ドメインロジックのホスト単体テスト（L1）
// 設計の正は docs/06-domain-model.md の不変条件と docs/11-testing.md のテストケース。
// 実装ではなく設計書の要求を検証する。

#include <unity.h>
#include <climits>
#include <cstdint>

#include "domain/life_change.hpp"
#include "domain/match_state.hpp"
#include "domain/life_service.hpp"

using namespace counter::domain;

// テスト間で共有する MatchState。setUp() で毎回初期化される。
static MatchState ms;

void setUp(void) {
    // 各テスト前に初期ライフ 40/40 で試合を開始する
    startMatch(ms, 40, 40);
}

void tearDown(void) {
    // クリーンアップ不要
}

// ========================================================================
// docs/11-testing.md L1 ドメイン単体テスト表のケース
// ========================================================================

// 40 に -1 → 39: ライフ変更が正しく適用される
void test_life_reduction_40_minus_1(void) {
    LifeChange lc = applyLifeChange(ms, PlayerId::Top, -1, 100);

    TEST_ASSERT_EQUAL_UINT32(39, ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(39, lc.after);
    TEST_ASSERT_EQUAL_UINT32(40, lc.before);
    TEST_ASSERT_EQUAL_INT32(-1, lc.appliedDelta);
    TEST_ASSERT_EQUAL_INT32(-1, lc.requestedDelta);
}

// 0 に -1 → 0: ライフは 0 未満にならない（下限クランプ）
void test_life_zero_clamp(void) {
    // ライフを 0 にする
    applyLifeChange(ms, PlayerId::Top, -40, 100);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].life);

    // 0 からさらに減算 → 0 のまま
    LifeChange lc = applyLifeChange(ms, PlayerId::Top, -1, 200);

    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(0, lc.after);
    TEST_ASSERT_EQUAL_INT32(0, lc.appliedDelta);
    TEST_ASSERT_EQUAL_INT32(-1, lc.requestedDelta);
}

// 40 に +10 → 50: 開始ライフは上限ではない
// FaB のルール上、ライフは開始値を超えて増加できる
void test_life_exceeds_starting_life(void) {
    LifeChange lc = applyLifeChange(ms, PlayerId::Top, +10, 100);

    TEST_ASSERT_EQUAL_UINT32(50, ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(50, lc.after);
    TEST_ASSERT_EQUAL_UINT32(40, lc.before);
    TEST_ASSERT_EQUAL_INT32(+10, lc.appliedDelta);
}

// 2 に -5 → 0、appliedDelta == -2、requestedDelta == -5
// requestedDelta と appliedDelta が正しく記録される
void test_clamp_tracks_applied_and_requested(void) {
    // ライフを 2 にする
    applyLifeChange(ms, PlayerId::Top, -38, 100);
    TEST_ASSERT_EQUAL_UINT32(2, ms.players[toIndex(PlayerId::Top)].life);

    // 2 に -5 → クランプが発生
    LifeChange lc = applyLifeChange(ms, PlayerId::Top, -5, 200);

    TEST_ASSERT_EQUAL_UINT32(0, lc.after);
    TEST_ASSERT_EQUAL_UINT32(2, lc.before);
    TEST_ASSERT_EQUAL_INT32(-2, lc.appliedDelta);
    TEST_ASSERT_EQUAL_INT32(-5, lc.requestedDelta);
}

// 上記（ライフ 2、requestedDelta -5）を Undo → 2 に戻る
// Undo は requestedDelta の逆（+5）ではなく、before への復元（2）
void test_undo_restores_before_not_reverse_delta(void) {
    // ライフを 2 にする
    applyLifeChange(ms, PlayerId::Top, -38, 100);
    TEST_ASSERT_EQUAL_UINT32(2, ms.players[toIndex(PlayerId::Top)].life);

    // 2 に -5 → 0（クランプ発生）
    applyLifeChange(ms, PlayerId::Top, -5, 200);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].life);

    // Undo → before=2 に戻る（+5 でも +2 でもなく、記録された before への復元）
    bool result = undoLast(ms);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT32(2, ms.players[toIndex(PlayerId::Top)].life);
}

// スライドで -8 した後 Undo → 操作前（40）へ戻る
// docs/06: 1 回のスライド全体が 1 件として履歴に登録される
void test_undo_after_slide_minus_8(void) {
    // スライドで -8（1 件として記録される）
    applyLifeChange(ms, PlayerId::Top, -8, 100);
    TEST_ASSERT_EQUAL_UINT32(32, ms.players[toIndex(PlayerId::Top)].life);

    // Undo → 40（スライド開始前）に戻る
    bool result = undoLast(ms);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[toIndex(PlayerId::Top)].life);
}

// 履歴 65 件 → 最新 64 件だけ残る
// RingBuffer<LifeChange, 64> の容量制約
void test_history_65_entries_keeps_latest_64(void) {
    // 65 件の履歴を作る（+1 を 65 回）
    for (int i = 0; i < 65; ++i) {
        applyLifeChange(ms, PlayerId::Top, +1,
                        static_cast<uint32_t>(i) * 100);
    }
    // リングバッファの最大容量は 64 件
    TEST_ASSERT_EQUAL(64, static_cast<int>(ms.history.size()));

    // ライフは 40 + 65 = 105
    TEST_ASSERT_EQUAL_UINT32(105, ms.players[toIndex(PlayerId::Top)].life);

    // 最も古い（65 件目に押し出された 1 件目）は Undo 不可 ——
    // 64 回 Undo すると全て成功し、65 回目は false
    for (int i = 0; i < 64; ++i) {
        TEST_ASSERT_TRUE(undoLast(ms));
    }
    TEST_ASSERT_FALSE(undoLast(ms));

    // 1 件分だけ Undo できなかったので、ライフは 40+1 = 41
    TEST_ASSERT_EQUAL_UINT32(41, ms.players[toIndex(PlayerId::Top)].life);
}

// UINT32_MAX 付近で加算してもオーバーフローしない
// int64_t キャストによるオーバーフロー回避
void test_uint32_max_no_overflow(void) {
    // 開始ライフを UINT32_MAX - 1 に設定
    startMatch(ms, UINT32_MAX - 1, 40);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 1,
                             ms.players[toIndex(PlayerId::Top)].life);

    // +10 → UINT32_MAX にクランプされる（+1 しか適用されない）
    LifeChange lc = applyLifeChange(ms, PlayerId::Top, +10, 100);

    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, lc.after);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 1, lc.before);
    TEST_ASSERT_EQUAL_INT32(+10, lc.requestedDelta);
    TEST_ASSERT_EQUAL_INT32(1, lc.appliedDelta);
}

// 上下交換でライフと開始ライフの両方が正しく交換される
// docs/06: Swap Sides の整合性（不変条件 10）
void test_swap_sides_exchanges_life_and_starting_life(void) {
    // 非対称な開始ライフで試合を開始
    startMatch(ms, 20, 40);
    // Top のライフだけ変更
    applyLifeChange(ms, PlayerId::Top, -5, 100);

    // 交換前の状態を確認
    TEST_ASSERT_EQUAL_UINT32(15, ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(20,
                             ms.players[toIndex(PlayerId::Top)].startingLife);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[toIndex(PlayerId::Bottom)].life);
    TEST_ASSERT_EQUAL_UINT32(40,
                             ms.players[toIndex(PlayerId::Bottom)].startingLife);

    swapSides(ms);

    // 交換後: ライフと開始ライフの両方が入れ替わる
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(40,
                             ms.players[toIndex(PlayerId::Top)].startingLife);
    TEST_ASSERT_EQUAL_UINT32(15, ms.players[toIndex(PlayerId::Bottom)].life);
    TEST_ASSERT_EQUAL_UINT32(20,
                             ms.players[toIndex(PlayerId::Bottom)].startingLife);
}

// ========================================================================
// 追加で検証するケース（タスク指示による）
// ========================================================================

// requestedDelta == 0 のとき履歴が積まれないこと
void test_zero_delta_no_history_entry(void) {
    size_t histBefore = ms.history.size();
    uint32_t seqBefore = ms.nextSequence;

    LifeChange lc = applyLifeChange(ms, PlayerId::Top, 0, 100);

    // 履歴が増えていない
    TEST_ASSERT_EQUAL(static_cast<int>(histBefore),
                      static_cast<int>(ms.history.size()));
    // nextSequence がインクリメントされていない
    TEST_ASSERT_EQUAL_UINT32(seqBefore, ms.nextSequence);
    // 返却値は noop を表す
    TEST_ASSERT_EQUAL_INT32(0, lc.appliedDelta);
    TEST_ASSERT_EQUAL_INT32(0, lc.requestedDelta);
    TEST_ASSERT_EQUAL_UINT32(40, lc.before);
    TEST_ASSERT_EQUAL_UINT32(40, lc.after);
}

// sequence が単調増加すること
// docs/06: 不変条件 4
void test_sequence_monotonically_increases(void) {
    LifeChange lc1 = applyLifeChange(ms, PlayerId::Top, -1, 100);
    LifeChange lc2 = applyLifeChange(ms, PlayerId::Top, -1, 200);
    LifeChange lc3 = applyLifeChange(ms, PlayerId::Bottom, +1, 300);

    TEST_ASSERT_TRUE(lc2.sequence > lc1.sequence);
    TEST_ASSERT_TRUE(lc3.sequence > lc2.sequence);
}

// 空の履歴で undoLast() が false を返し、状態を変えないこと
void test_undo_empty_history_returns_false(void) {
    uint32_t topLife = ms.players[toIndex(PlayerId::Top)].life;
    uint32_t bottomLife = ms.players[toIndex(PlayerId::Bottom)].life;

    bool result = undoLast(ms);

    TEST_ASSERT_FALSE(result);
    // 状態が変わっていないこと
    TEST_ASSERT_EQUAL_UINT32(topLife,
                             ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(bottomLife,
                             ms.players[toIndex(PlayerId::Bottom)].life);
}

// 不変条件: appliedDelta == (int32_t)after - (int32_t)before が常に成り立つ
// docs/06: 不変条件 2, 5
void test_invariant_applied_delta_equals_diff(void) {
    // 通常の減算
    LifeChange lc1 = applyLifeChange(ms, PlayerId::Top, -3, 100);
    TEST_ASSERT_EQUAL_INT32(
        static_cast<int32_t>(lc1.after) - static_cast<int32_t>(lc1.before),
        lc1.appliedDelta);

    // 通常の加算
    LifeChange lc2 = applyLifeChange(ms, PlayerId::Top, +5, 200);
    TEST_ASSERT_EQUAL_INT32(
        static_cast<int32_t>(lc2.after) - static_cast<int32_t>(lc2.before),
        lc2.appliedDelta);

    // 下限クランプ発生時
    startMatch(ms, 3, 40);
    LifeChange lc3 = applyLifeChange(ms, PlayerId::Top, -10, 300);
    TEST_ASSERT_EQUAL_INT32(
        static_cast<int32_t>(lc3.after) - static_cast<int32_t>(lc3.before),
        lc3.appliedDelta);

    // 上限クランプ発生時
    startMatch(ms, UINT32_MAX - 1, 40);
    LifeChange lc4 = applyLifeChange(ms, PlayerId::Top, +10, 400);
    TEST_ASSERT_EQUAL_INT32(
        static_cast<int32_t>(lc4.after) - static_cast<int32_t>(lc4.before),
        lc4.appliedDelta);
}

// rematch() で開始ライフに戻り、履歴がクリアされること
void test_rematch_restores_starting_life_clears_history(void) {
    // 非対称な開始ライフで試合を開始
    startMatch(ms, 20, 40);
    applyLifeChange(ms, PlayerId::Top, -5, 100);
    applyLifeChange(ms, PlayerId::Bottom, -10, 200);

    // 変更後の状態を確認
    TEST_ASSERT_EQUAL_UINT32(15, ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(30, ms.players[toIndex(PlayerId::Bottom)].life);
    TEST_ASSERT_TRUE(ms.history.size() > 0);

    rematch(ms);

    // 開始ライフに戻る
    TEST_ASSERT_EQUAL_UINT32(20, ms.players[toIndex(PlayerId::Top)].life);
    TEST_ASSERT_EQUAL_UINT32(20,
                             ms.players[toIndex(PlayerId::Top)].startingLife);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[toIndex(PlayerId::Bottom)].life);
    TEST_ASSERT_EQUAL_UINT32(40,
                             ms.players[toIndex(PlayerId::Bottom)].startingLife);
    // 履歴がクリアされている
    TEST_ASSERT_TRUE(ms.history.empty());
}

// RingBuffer の operator[](0) が最新を返すこと
// docs/06: 論理添字 0 = 最新、size()-1 = 最古
void test_ringbuffer_index_zero_returns_newest(void) {
    applyLifeChange(ms, PlayerId::Top, -1, 100);  // 40 -> 39
    applyLifeChange(ms, PlayerId::Top, -2, 200);  // 39 -> 37
    applyLifeChange(ms, PlayerId::Top, -3, 300);  // 37 -> 34

    // [0] = 最新（-3 の変更）
    TEST_ASSERT_EQUAL_INT32(-3, ms.history[0].requestedDelta);
    TEST_ASSERT_EQUAL_UINT32(37, ms.history[0].before);
    TEST_ASSERT_EQUAL_UINT32(34, ms.history[0].after);

    // [1] = 1 つ前（-2 の変更）
    TEST_ASSERT_EQUAL_INT32(-2, ms.history[1].requestedDelta);
    TEST_ASSERT_EQUAL_UINT32(39, ms.history[1].before);
    TEST_ASSERT_EQUAL_UINT32(37, ms.history[1].after);

    // [2] = 最古（-1 の変更）
    TEST_ASSERT_EQUAL_INT32(-1, ms.history[2].requestedDelta);
    TEST_ASSERT_EQUAL_UINT32(40, ms.history[2].before);
    TEST_ASSERT_EQUAL_UINT32(39, ms.history[2].after);
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // docs/11-testing.md L1 ドメイン単体テスト表のケース
    RUN_TEST(test_life_reduction_40_minus_1);
    RUN_TEST(test_life_zero_clamp);
    RUN_TEST(test_life_exceeds_starting_life);
    RUN_TEST(test_clamp_tracks_applied_and_requested);
    RUN_TEST(test_undo_restores_before_not_reverse_delta);
    RUN_TEST(test_undo_after_slide_minus_8);
    RUN_TEST(test_history_65_entries_keeps_latest_64);
    RUN_TEST(test_uint32_max_no_overflow);
    RUN_TEST(test_swap_sides_exchanges_life_and_starting_life);

    // 追加で検証するケース
    RUN_TEST(test_zero_delta_no_history_entry);
    RUN_TEST(test_sequence_monotonically_increases);
    RUN_TEST(test_undo_empty_history_returns_false);
    RUN_TEST(test_invariant_applied_delta_equals_diff);
    RUN_TEST(test_rematch_restores_starting_life_clears_history);
    RUN_TEST(test_ringbuffer_index_zero_returns_newest);

    return UNITY_END();
}
