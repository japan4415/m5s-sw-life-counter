// test/test_edh_domain/test_edh_domain.cpp
//
// EDH ドメインロジックのホスト単体テスト
// 仕様の正は docs/15-edh-firmware-spec.md

#include <unity.h>
#include <cstdint>

#include "domain/edh_life_change.hpp"
#include "domain/edh_match_state.hpp"
#include "domain/edh_life_service.hpp"

using namespace counter::edh;

static MatchState ms;

void setUp(void) {
    startMatch(ms, 40);
}

void tearDown(void) {
}

// ========================================================================
// ライフ増減とクランプ
// ========================================================================

// 40 に -1 → 39
void test_life_reduction(void) {
    LifeChange lc = applyLifeChange(ms, 0, -1, 100);

    TEST_ASSERT_EQUAL_UINT32(39, ms.players[0].life);
    TEST_ASSERT_EQUAL_UINT32(39, lc.lifeAfter);
    TEST_ASSERT_EQUAL_UINT32(40, lc.lifeBefore);
    TEST_ASSERT_EQUAL_INT16(-1, lc.delta);
    TEST_ASSERT_EQUAL_UINT8(kSourceNone, lc.sourceIndex);
}

// 40 に +10 → 50（開始ライフは上限ではない）
void test_life_exceeds_starting(void) {
    LifeChange lc = applyLifeChange(ms, 1, +10, 100);

    TEST_ASSERT_EQUAL_UINT32(50, ms.players[1].life);
    TEST_ASSERT_EQUAL_UINT32(50, lc.lifeAfter);
}

// 0 に -1 → 0（下限クランプ）
void test_life_zero_clamp(void) {
    applyLifeChange(ms, 0, -40, 100);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[0].life);

    LifeChange lc = applyLifeChange(ms, 0, -1, 200);
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[0].life);
    TEST_ASSERT_EQUAL_UINT32(0, lc.lifeAfter);
}

// delta == 0 のとき履歴が積まれない
void test_zero_delta_no_history(void) {
    size_t histBefore = ms.history.size();
    LifeChange lc = applyLifeChange(ms, 0, 0, 100);

    TEST_ASSERT_EQUAL(static_cast<int>(histBefore),
                      static_cast<int>(ms.history.size()));
    TEST_ASSERT_EQUAL_INT16(0, lc.delta);
}

// 4 プレイヤーが独立してライフを持つ
void test_four_players_independent_life(void) {
    applyLifeChange(ms, 0, -5, 100);
    applyLifeChange(ms, 1, -10, 200);
    applyLifeChange(ms, 2, +3, 300);
    // P4 は変更なし

    TEST_ASSERT_EQUAL_UINT32(35, ms.players[0].life);
    TEST_ASSERT_EQUAL_UINT32(30, ms.players[1].life);
    TEST_ASSERT_EQUAL_UINT32(43, ms.players[2].life);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[3].life);
}

// ========================================================================
// 統率者ダメージのライフ連動
// ========================================================================

// 統率者ダメージ +5 → ダメージ 5、ライフ 35
void test_commander_damage_increases_with_life_linked(void) {
    LifeChange lc = applyCommanderDamage(ms, 0, 1, +5, 100);

    TEST_ASSERT_EQUAL_UINT8(5, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(35, ms.players[0].life);
    TEST_ASSERT_EQUAL_UINT8(0, lc.cmdDmgBefore);
    TEST_ASSERT_EQUAL_UINT8(5, lc.cmdDmgAfter);
    TEST_ASSERT_EQUAL_UINT32(40, lc.lifeBefore);
    TEST_ASSERT_EQUAL_UINT32(35, lc.lifeAfter);
    TEST_ASSERT_EQUAL_UINT8(1, lc.sourceIndex);
}

// 統率者ダメージ -3 → ダメージ減少、ライフ回復
void test_commander_damage_decrease_restores_life(void) {
    applyCommanderDamage(ms, 0, 1, +5, 100);
    TEST_ASSERT_EQUAL_UINT8(5, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(35, ms.players[0].life);

    LifeChange lc = applyCommanderDamage(ms, 0, 1, -3, 200);

    TEST_ASSERT_EQUAL_UINT8(2, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(38, ms.players[0].life);
    TEST_ASSERT_EQUAL_UINT8(5, lc.cmdDmgBefore);
    TEST_ASSERT_EQUAL_UINT8(2, lc.cmdDmgAfter);
}

// ========================================================================
// クランプ時の連動量（仕様書の具体例）
// ========================================================================

// 統率者ダメージ 2 で -5 → ダメージ 0、ライフ +2
void test_commander_damage_clamp_linked_life(void) {
    // ダメージを 2 にする
    applyCommanderDamage(ms, 0, 1, +2, 100);
    TEST_ASSERT_EQUAL_UINT8(2, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(38, ms.players[0].life);

    // ダメージ 2 で -5 → ダメージ 0 にクランプ、ライフは +2 のみ
    LifeChange lc = applyCommanderDamage(ms, 0, 1, -5, 200);

    TEST_ASSERT_EQUAL_UINT8(0, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[0].life);
    TEST_ASSERT_EQUAL_UINT8(2, lc.cmdDmgBefore);
    TEST_ASSERT_EQUAL_UINT8(0, lc.cmdDmgAfter);
    TEST_ASSERT_EQUAL_UINT32(38, lc.lifeBefore);
    TEST_ASSERT_EQUAL_UINT32(40, lc.lifeAfter);
}

// 統率者ダメージ 99 上限クランプ
void test_commander_damage_upper_clamp(void) {
    // ダメージを 98 にする
    applyCommanderDamage(ms, 0, 1, +98, 100);
    TEST_ASSERT_EQUAL_UINT8(98, ms.players[0].commanderDamageFrom[1]);
    // ライフは 40 - 98 → 0 にクランプされている
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[0].life);

    // +5 → 99 にクランプ（実際の変化量は +1）
    LifeChange lc = applyCommanderDamage(ms, 0, 1, +5, 200);

    TEST_ASSERT_EQUAL_UINT8(99, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT8(98, lc.cmdDmgBefore);
    TEST_ASSERT_EQUAL_UINT8(99, lc.cmdDmgAfter);
    // ライフ連動は実際の変化量 1 の符号反転 → -1 だが、ライフ 0 なので 0 のまま
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[0].life);
    TEST_ASSERT_EQUAL_UINT32(0, lc.lifeBefore);
    TEST_ASSERT_EQUAL_UINT32(0, lc.lifeAfter);
    // delta にはクランプ後の実際のダメージ変化量 +1 が入る（要求 +5 ではない）
    TEST_ASSERT_EQUAL_INT16(1, lc.delta);
}

// 統率者ダメージ上限クランプ時のライフ連動量
void test_commander_damage_upper_clamp_life_link(void) {
    // 初期ライフ 100 で開始して上限クランプを確認しやすくする
    startMatch(ms, 100);

    // ダメージを 97 にする
    applyCommanderDamage(ms, 0, 1, +97, 100);
    TEST_ASSERT_EQUAL_UINT8(97, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(3, ms.players[0].life);  // 100 - 97 = 3

    // +5 → 99 にクランプ（実際の変化量 +2）、ライフ連動は -2
    LifeChange lc = applyCommanderDamage(ms, 0, 1, +5, 200);

    TEST_ASSERT_EQUAL_UINT8(99, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(1, ms.players[0].life);  // 3 - 2 = 1
    TEST_ASSERT_EQUAL_UINT32(3, lc.lifeBefore);
    TEST_ASSERT_EQUAL_UINT32(1, lc.lifeAfter);
}

// 統率者ダメージ delta == 0 で履歴が積まれない
void test_commander_damage_zero_delta_no_history(void) {
    size_t histBefore = ms.history.size();
    LifeChange lc = applyCommanderDamage(ms, 0, 1, 0, 100);

    TEST_ASSERT_EQUAL(static_cast<int>(histBefore),
                      static_cast<int>(ms.history.size()));
    TEST_ASSERT_EQUAL_INT16(0, lc.delta);
}

// ========================================================================
// Undo
// ========================================================================

// 通常ライフ操作の Undo
void test_undo_normal_life(void) {
    applyLifeChange(ms, 0, -5, 100);
    TEST_ASSERT_EQUAL_UINT32(35, ms.players[0].life);

    bool result = undoLast(ms);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[0].life);
}

// 統率者ダメージ操作の Undo（ライフと統率者ダメージの両方が戻る）
void test_undo_commander_damage(void) {
    applyCommanderDamage(ms, 0, 1, +5, 100);
    TEST_ASSERT_EQUAL_UINT8(5, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(35, ms.players[0].life);

    bool result = undoLast(ms);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT8(0, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[0].life);
}

// クランプを含む統率者ダメージ操作の Undo
void test_undo_commander_damage_with_clamp(void) {
    // ダメージ 2 → -5 でクランプ → Undo
    applyCommanderDamage(ms, 0, 1, +2, 100);
    applyCommanderDamage(ms, 0, 1, -5, 200);
    TEST_ASSERT_EQUAL_UINT8(0, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[0].life);

    bool result = undoLast(ms);
    TEST_ASSERT_TRUE(result);
    // -5 操作の前の状態に戻る（ダメージ 2、ライフ 38）
    TEST_ASSERT_EQUAL_UINT8(2, ms.players[0].commanderDamageFrom[1]);
    TEST_ASSERT_EQUAL_UINT32(38, ms.players[0].life);
}

// 空の履歴での Undo
void test_undo_empty_history(void) {
    bool result = undoLast(ms);
    TEST_ASSERT_FALSE(result);
}

// 通常操作と統率者操作が混在した Undo
void test_undo_mixed_operations(void) {
    applyLifeChange(ms, 0, -3, 100);        // ライフ 37
    applyCommanderDamage(ms, 0, 2, +4, 200); // ダメージ 4、ライフ 33
    applyLifeChange(ms, 1, +5, 300);         // P2 ライフ 45

    // P2 のライフ操作を Undo
    undoLast(ms);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[1].life);

    // P1 の統率者ダメージ操作を Undo
    undoLast(ms);
    TEST_ASSERT_EQUAL_UINT8(0, ms.players[0].commanderDamageFrom[2]);
    TEST_ASSERT_EQUAL_UINT32(37, ms.players[0].life);

    // P1 のライフ操作を Undo
    undoLast(ms);
    TEST_ASSERT_EQUAL_UINT32(40, ms.players[0].life);
}

// ========================================================================
// 敗北判定
// ========================================================================

// ライフ 0 で敗北
void test_defeated_by_life_zero(void) {
    applyLifeChange(ms, 0, -40, 100);
    TEST_ASSERT_TRUE(isDefeated(ms, 0));
}

// ライフ 1 では非敗北
void test_not_defeated_with_life_1(void) {
    applyLifeChange(ms, 0, -39, 100);
    TEST_ASSERT_FALSE(isDefeated(ms, 0));
}

// 統率者ダメージ 21 で敗北
void test_defeated_by_commander_damage_21(void) {
    applyCommanderDamage(ms, 0, 1, +21, 100);
    TEST_ASSERT_TRUE(isDefeated(ms, 0));
}

// 統率者ダメージ 20 では非敗北
void test_not_defeated_by_commander_damage_20(void) {
    applyCommanderDamage(ms, 0, 1, +20, 100);
    TEST_ASSERT_FALSE(isDefeated(ms, 0));
}

// 複数の被弾元から 20 ずつ受けても非敗北（各被弾元は 21 未満）
void test_not_defeated_by_split_commander_damage(void) {
    applyCommanderDamage(ms, 0, 1, +20, 100);
    applyCommanderDamage(ms, 0, 2, +20, 200);
    applyCommanderDamage(ms, 0, 3, +20, 300);
    // ライフは 0 にクランプされるので敗北だが、統率者ダメージ単体では非敗北を確認
    // → ライフ 0 で敗北してしまうので、初期ライフを大きくする
    startMatch(ms, 100);
    applyCommanderDamage(ms, 0, 1, +20, 100);
    applyCommanderDamage(ms, 0, 2, +20, 200);
    applyCommanderDamage(ms, 0, 3, +20, 300);
    // ライフ 100 - 60 = 40、各被弾元 20 → 非敗北
    TEST_ASSERT_FALSE(isDefeated(ms, 0));
}

// 初期状態では非敗北
void test_not_defeated_initial(void) {
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        TEST_ASSERT_FALSE(isDefeated(ms, i));
    }
}

// ========================================================================
// startMatch / rematch
// ========================================================================

// startMatch で 4 人に初期ライフが設定される
void test_start_match_initial_life(void) {
    startMatch(ms, 30);
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        TEST_ASSERT_EQUAL_UINT32(30, ms.players[i].life);
        TEST_ASSERT_EQUAL_UINT32(30, ms.players[i].startingLife);
    }
}

// rematch で開始ライフに戻り、統率者ダメージもクリアされる
void test_rematch_resets_all(void) {
    applyLifeChange(ms, 0, -10, 100);
    applyCommanderDamage(ms, 1, 2, +15, 200);

    rematch(ms);

    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        TEST_ASSERT_EQUAL_UINT32(40, ms.players[i].life);
        for (uint8_t j = 0; j < kPlayerCount; ++j) {
            TEST_ASSERT_EQUAL_UINT8(0, ms.players[i].commanderDamageFrom[j]);
        }
    }
    TEST_ASSERT_TRUE(ms.history.empty());
}

// sequence が単調増加する
void test_sequence_monotonically_increases(void) {
    LifeChange lc1 = applyLifeChange(ms, 0, -1, 100);
    LifeChange lc2 = applyCommanderDamage(ms, 1, 0, +3, 200);
    LifeChange lc3 = applyLifeChange(ms, 2, +1, 300);

    TEST_ASSERT_TRUE(lc2.sequence > lc1.sequence);
    TEST_ASSERT_TRUE(lc3.sequence > lc2.sequence);
}

// ========================================================================
// delta にクランプ後の実際の変化量が格納されること
// ========================================================================

// 統率者ダメージ 2 で -5 → delta == -2（要求 -5 ではなく実際の変化量）
void test_delta_reflects_clamped_cmd_damage(void) {
    applyCommanderDamage(ms, 0, 1, +2, 100);
    LifeChange lc = applyCommanderDamage(ms, 0, 1, -5, 200);

    // クランプ後の実際のダメージ変化量は -2（2 → 0）
    TEST_ASSERT_EQUAL_INT16(-2, lc.delta);
    TEST_ASSERT_EQUAL_UINT8(0, lc.cmdDmgAfter);
}

// 統率者ダメージ上限クランプ: delta == +2（要求 +5、97 → 99）
void test_delta_reflects_upper_clamped_cmd_damage(void) {
    startMatch(ms, 100);
    applyCommanderDamage(ms, 0, 1, +97, 100);

    LifeChange lc = applyCommanderDamage(ms, 0, 1, +5, 200);

    // クランプ後の実際のダメージ変化量は +2（97 → 99）
    TEST_ASSERT_EQUAL_INT16(2, lc.delta);
    TEST_ASSERT_EQUAL_UINT8(99, lc.cmdDmgAfter);
}

// ライフ 0 付近でのクランプ: delta == -2（要求 -5、ライフ 2 → 0）
void test_delta_reflects_clamped_life(void) {
    applyLifeChange(ms, 0, -38, 100);  // ライフ 2
    TEST_ASSERT_EQUAL_UINT32(2, ms.players[0].life);

    LifeChange lc = applyLifeChange(ms, 0, -5, 200);

    // クランプ後の実際のライフ変化量は -2（2 → 0）
    TEST_ASSERT_EQUAL_INT16(-2, lc.delta);
    TEST_ASSERT_EQUAL_UINT32(0, lc.lifeAfter);
    TEST_ASSERT_EQUAL_UINT32(2, lc.lifeBefore);
}

// ライフ 0 で -1 → delta == 0（変化なし）
void test_delta_zero_when_life_already_zero(void) {
    applyLifeChange(ms, 0, -40, 100);  // ライフ 0
    TEST_ASSERT_EQUAL_UINT32(0, ms.players[0].life);

    LifeChange lc = applyLifeChange(ms, 0, -1, 200);

    // 既に 0 なのでクランプ後の変化量は 0
    TEST_ASSERT_EQUAL_INT16(0, lc.delta);
}

// クランプなしの通常操作: delta == 要求量と一致
void test_delta_matches_request_without_clamp(void) {
    LifeChange lc = applyLifeChange(ms, 0, -3, 100);
    TEST_ASSERT_EQUAL_INT16(-3, lc.delta);

    LifeChange lc2 = applyCommanderDamage(ms, 0, 1, +7, 200);
    TEST_ASSERT_EQUAL_INT16(7, lc2.delta);
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // ライフ増減とクランプ
    RUN_TEST(test_life_reduction);
    RUN_TEST(test_life_exceeds_starting);
    RUN_TEST(test_life_zero_clamp);
    RUN_TEST(test_zero_delta_no_history);
    RUN_TEST(test_four_players_independent_life);

    // 統率者ダメージのライフ連動
    RUN_TEST(test_commander_damage_increases_with_life_linked);
    RUN_TEST(test_commander_damage_decrease_restores_life);

    // クランプ時の連動量
    RUN_TEST(test_commander_damage_clamp_linked_life);
    RUN_TEST(test_commander_damage_upper_clamp);
    RUN_TEST(test_commander_damage_upper_clamp_life_link);
    RUN_TEST(test_commander_damage_zero_delta_no_history);

    // Undo
    RUN_TEST(test_undo_normal_life);
    RUN_TEST(test_undo_commander_damage);
    RUN_TEST(test_undo_commander_damage_with_clamp);
    RUN_TEST(test_undo_empty_history);
    RUN_TEST(test_undo_mixed_operations);

    // 敗北判定
    RUN_TEST(test_defeated_by_life_zero);
    RUN_TEST(test_not_defeated_with_life_1);
    RUN_TEST(test_defeated_by_commander_damage_21);
    RUN_TEST(test_not_defeated_by_commander_damage_20);
    RUN_TEST(test_not_defeated_by_split_commander_damage);
    RUN_TEST(test_not_defeated_initial);

    // startMatch / rematch
    RUN_TEST(test_start_match_initial_life);
    RUN_TEST(test_rematch_resets_all);
    RUN_TEST(test_sequence_monotonically_increases);

    // delta にクランプ後の実際の変化量が格納されること
    RUN_TEST(test_delta_reflects_clamped_cmd_damage);
    RUN_TEST(test_delta_reflects_upper_clamped_cmd_damage);
    RUN_TEST(test_delta_reflects_clamped_life);
    RUN_TEST(test_delta_zero_when_life_already_zero);
    RUN_TEST(test_delta_matches_request_without_clamp);

    return UNITY_END();
}
