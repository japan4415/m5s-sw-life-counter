// test/test_riftbound_domain/test_riftbound_domain.cpp
//
// Riftbound 得点カウンタードメインロジックのホスト単体テスト（L1）
// 設計の正は docs/16-riftbound-firmware-spec.md のドメインモデルと
// docs/06-domain-model.md（FaB 版と不変条件を共用）。
// 実装ではなく設計書の要求を検証する。

#include <unity.h>
#include <cstdint>

#include "domain/riftbound_score_change.hpp"
#include "domain/riftbound_match_state.hpp"
#include "domain/riftbound_score_service.hpp"

using namespace counter::riftbound;

// テスト間で共有する MatchState。setUp() で毎回初期化される。
static MatchState ms;

void setUp(void) {
    // 各テ前に試合を開始する（得点 0/0、勝利点 8）
    startMatch(ms);
}

void tearDown(void) {
    // クリーンアップ不要
}

// ========================================================================
// 試合開始
// ========================================================================

// startMatch で両プレイヤーの得点が 0、勝利点が 8 になる
void test_start_match_sets_scores_to_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].score);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Bottom)].score);
    TEST_ASSERT_EQUAL_UINT8(kDefaultVictoryScore, ms.victoryScore);
    TEST_ASSERT_TRUE(ms.active);
    TEST_ASSERT_FALSE(ms.touchLocked);
}

// ========================================================================
// 得点変更の基本
// ========================================================================

// 0 に +1 → 1: 得点変更が正しく適用される
void test_score_add_1(void) {
    ScoreChange sc = applyScoreChange(ms, PlayerId::Top, +1, 100);

    TEST_ASSERT_EQUAL_UINT32(1, ms.players[toIndex(PlayerId::Top)].score);
    TEST_ASSERT_EQUAL_UINT32(1, sc.after);
    TEST_ASSERT_EQUAL_UINT32(0, sc.before);
    TEST_ASSERT_EQUAL_INT32(+1, sc.appliedDelta);
    TEST_ASSERT_EQUAL_INT32(+1, sc.requestedDelta);
}

// 0 は下限ではないため減算で 0 にクランプされる
// 得点は 0 未満にならない（下限クランプ）
void test_score_zero_clamp(void) {
    // 0 から -1 → 0 のまま
    ScoreChange sc = applyScoreChange(ms, PlayerId::Top, -1, 100);

    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].score);
    TEST_ASSERT_EQUAL_UINT32(0, sc.after);
    TEST_ASSERT_EQUAL_INT32(0, sc.appliedDelta);
    TEST_ASSERT_EQUAL_INT32(-1, sc.requestedDelta);
}

// 勝利点 8 を超えて +1 → 9: 勝利点は上限ではない
// 本機は記録補助であり、Undo や入力修正の余地を残すため超過を許容する
void test_score_exceeds_victory_score(void) {
    // 勝利点まで加算
    applyScoreChange(ms, PlayerId::Top, +8, 100);
    TEST_ASSERT_EQUAL_UINT32(8, ms.players[toIndex(PlayerId::Top)].score);

    // さらに +1 → 9
    ScoreChange sc = applyScoreChange(ms, PlayerId::Top, +1, 200);
    TEST_ASSERT_EQUAL_UINT32(9, ms.players[toIndex(PlayerId::Top)].score);
    TEST_ASSERT_EQUAL_UINT32(9, sc.after);
    TEST_ASSERT_EQUAL_INT32(+1, sc.appliedDelta);
}

// 2 に -5 → 0、appliedDelta == -2、requestedDelta == -5
// requestedDelta と appliedDelta が正しく記録される
void test_clamp_tracks_applied_and_requested(void) {
    // 得点を 2 にする
    applyScoreChange(ms, PlayerId::Top, +2, 100);
    TEST_ASSERT_EQUAL_UINT32(2, ms.players[toIndex(PlayerId::Top)].score);

    // 2 に -5 → クランプが発生
    ScoreChange sc = applyScoreChange(ms, PlayerId::Top, -5, 200);

    TEST_ASSERT_EQUAL_UINT32(0, sc.after);
    TEST_ASSERT_EQUAL_UINT32(2, sc.before);
    TEST_ASSERT_EQUAL_INT32(-2, sc.appliedDelta);
    TEST_ASSERT_EQUAL_INT32(-5, sc.requestedDelta);
}

// requestedDelta == 0 は履歴を積まない
void test_zero_delta_does_not_push_history(void) {
    ScoreChange sc = applyScoreChange(ms, PlayerId::Top, 0, 100);

    TEST_ASSERT_EQUAL_UINT32(0, sc.appliedDelta);
    TEST_ASSERT_TRUE(ms.history.empty());
}

// 2 プレイヤーの得点は独立している
void test_players_are_independent(void) {
    applyScoreChange(ms, PlayerId::Top, +3, 100);
    applyScoreChange(ms, PlayerId::Bottom, +1, 200);

    TEST_ASSERT_EQUAL_UINT32(3, ms.players[toIndex(PlayerId::Top)].score);
    TEST_ASSERT_EQUAL_UINT32(1, ms.players[toIndex(PlayerId::Bottom)].score);
}

// ========================================================================
// Undo
// ========================================================================

// 上記（得点 2、requestedDelta -5）を Undo → 2 に戻る
// Undo は requestedDelta の逆（+5）ではなく、before への復元（2）
void test_undo_restores_before_not_reverse_delta(void) {
    // 得点を 2 にする
    applyScoreChange(ms, PlayerId::Top, +2, 100);
    TEST_ASSERT_EQUAL_UINT32(2, ms.players[toIndex(PlayerId::Top)].score);

    // 2 に -5 → 0（クランプ発生）
    applyScoreChange(ms, PlayerId::Top, -5, 200);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].score);

    // Undo → before=2 に戻る
    bool result = undoLast(ms);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT32(2, ms.players[toIndex(PlayerId::Top)].score);
}

// スライドで +3 した後 Undo → 操作前（0）へ戻る
// 1 回のスライド全体が 1 件として履歴に登録される
void test_undo_after_slide_plus_3(void) {
    applyScoreChange(ms, PlayerId::Top, +3, 100);
    TEST_ASSERT_EQUAL_UINT32(3, ms.players[toIndex(PlayerId::Top)].score);

    bool result = undoLast(ms);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].score);
}

// 履歴が空のとき Undo は false を返し状態を変えない
void test_undo_on_empty_history_returns_false(void) {
    TEST_ASSERT_FALSE(undoLast(ms));
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].score);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Bottom)].score);
}

// Undo は 1 件ずつ巻き戻る（2 件積んだら 2 回で空になる）
void test_undo_pops_one_entry_at_a_time(void) {
    applyScoreChange(ms, PlayerId::Top, +1, 100);
    applyScoreChange(ms, PlayerId::Bottom, +2, 200);

    // 1 回目の Undo: Bottom の変更（+2）が before=0 へ復元される。
    // Top の +1 は残る。
    TEST_ASSERT_TRUE(undoLast(ms));
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Bottom)].score);
    TEST_ASSERT_EQUAL_UINT32(1, ms.players[toIndex(PlayerId::Top)].score);

    // 2 回目の Undo: Top の変更（+1）が before=0 へ復元される
    TEST_ASSERT_TRUE(undoLast(ms));
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].score);

    TEST_ASSERT_FALSE(undoLast(ms));
}

// ========================================================================
// sequence と履歴容量
// ========================================================================

// sequence は単調増加する
void test_sequence_is_monotonic(void) {
    ScoreChange s1 = applyScoreChange(ms, PlayerId::Top, +1, 100);
    ScoreChange s2 = applyScoreChange(ms, PlayerId::Top, +1, 200);
    ScoreChange s3 = applyScoreChange(ms, PlayerId::Bottom, +1, 300);

    TEST_ASSERT_EQUAL_UINT32(0, s1.sequence);
    TEST_ASSERT_EQUAL_UINT32(1, s2.sequence);
    TEST_ASSERT_EQUAL_UINT32(2, s3.sequence);
    TEST_ASSERT_EQUAL_UINT32(3, ms.nextSequence);
}

// 履歴 65 件 → 最新 64 件だけ残る
// RingBuffer<ScoreChange, 64> の容量制約
void test_history_65_entries_keeps_latest_64(void) {
    for (int i = 0; i < 65; ++i) {
        applyScoreChange(ms, PlayerId::Top, +1, static_cast<uint32_t>(100 + i));
    }

    TEST_ASSERT_EQUAL(64, static_cast<int>(ms.history.size()));

    // 最新（index 0）は 65 件目の +1
    TEST_ASSERT_EQUAL_UINT32(65, ms.history[0].after);
    // 最古（index 63）は 2 件目の +1（1 件目は押し出されている）
    TEST_ASSERT_EQUAL_UINT32(2, ms.history[63].after);
}

// ========================================================================
// 勝利点到達判定
// ========================================================================

// 7 点では未到達、8 点で到達、9 点も到達のまま
void test_has_reached_victory_threshold(void) {
    applyScoreChange(ms, PlayerId::Top, +7, 100);
    TEST_ASSERT_FALSE(hasReachedVictory(ms, PlayerId::Top));

    applyScoreChange(ms, PlayerId::Top, +1, 200);
    TEST_ASSERT_TRUE(hasReachedVictory(ms, PlayerId::Top));

    applyScoreChange(ms, PlayerId::Top, +1, 300);
    TEST_ASSERT_TRUE(hasReachedVictory(ms, PlayerId::Top));
}

// 勝利点到達はプレイヤーごとに独立している
void test_has_reached_victory_per_player(void) {
    applyScoreChange(ms, PlayerId::Top, +8, 100);

    TEST_ASSERT_TRUE(hasReachedVictory(ms, PlayerId::Top));
    TEST_ASSERT_FALSE(hasReachedVictory(ms, PlayerId::Bottom));
}

// ========================================================================
// Rematch
// ========================================================================

// Rematch で得点が 0 に戻り、勝利点は維持される
void test_rematch_resets_scores_keeps_victory_score(void) {
    applyScoreChange(ms, PlayerId::Top, +8, 100);
    applyScoreChange(ms, PlayerId::Bottom, +3, 200);

    rematch(ms);

    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Top)].score);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[toIndex(PlayerId::Bottom)].score);
    TEST_ASSERT_EQUAL_UINT8(kDefaultVictoryScore, ms.victoryScore);
    TEST_ASSERT_TRUE(ms.active);
    TEST_ASSERT_FALSE(ms.touchLocked);
}

// Rematch で履歴がクリアされる
void test_rematch_clears_history(void) {
    applyScoreChange(ms, PlayerId::Top, +3, 100);

    rematch(ms);

    TEST_ASSERT_TRUE(ms.history.empty());
    TEST_ASSERT_EQUAL_UINT32(0, ms.nextSequence);

    // 履歴クリア後は Undo できない
    TEST_ASSERT_FALSE(undoLast(ms));
}

// ========================================================================
// NVS レコードサイズの静的検証
// ========================================================================

// MatchState が NVS 単一エントリ上限（4000 bytes）を超えないことの
// 防衛線。NvsStateStore 側にも static_assert があるが、ここでは
// 履歴容量 64 件の設計判断がレイアウトを壊していないことを検証する。
void test_match_state_fits_nvs_entry_limit(void) {
    TEST_ASSERT_TRUE(sizeof(MatchState) <= 4000);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_start_match_sets_scores_to_zero);
    RUN_TEST(test_score_add_1);
    RUN_TEST(test_score_zero_clamp);
    RUN_TEST(test_score_exceeds_victory_score);
    RUN_TEST(test_clamp_tracks_applied_and_requested);
    RUN_TEST(test_zero_delta_does_not_push_history);
    RUN_TEST(test_players_are_independent);
    RUN_TEST(test_undo_restores_before_not_reverse_delta);
    RUN_TEST(test_undo_after_slide_plus_3);
    RUN_TEST(test_undo_on_empty_history_returns_false);
    RUN_TEST(test_undo_pops_one_entry_at_a_time);
    RUN_TEST(test_sequence_is_monotonic);
    RUN_TEST(test_history_65_entries_keeps_latest_64);
    RUN_TEST(test_has_reached_victory_threshold);
    RUN_TEST(test_has_reached_victory_per_player);
    RUN_TEST(test_rematch_resets_scores_keeps_victory_score);
    RUN_TEST(test_rematch_clears_history);
    RUN_TEST(test_match_state_fits_nvs_entry_limit);

    return UNITY_END();
}
