// test/test_edh_touch/test_edh_touch.cpp
//
// EDH タッチゾーン判定のホスト単体テスト
// 仕様の正は docs/15-edh-firmware-spec.md

#include <unity.h>
#include <cstdint>

#include "input/edh_touch_zone.hpp"
#include "app_config.hpp"

using namespace counter::edh;

void setUp(void) {
}

void tearDown(void) {
}

// ========================================================================
// 4 扇形の判定
// ========================================================================

// 中心 = (234, 234)

// P1 (上): dy < 0, |dy| > |dx|
void test_sector_p1_top(void) {
    // 真上: (234, 100) → dx=0, dy=-134
    TEST_ASSERT_EQUAL_UINT8(0, selectSector(234, 100));
}

// P2 (右): dx > 0, |dx| >= |dy|
void test_sector_p2_right(void) {
    // 真右: (400, 234) → dx=166, dy=0
    TEST_ASSERT_EQUAL_UINT8(1, selectSector(400, 234));
}

// P3 (下): dy > 0, |dy| > |dx|
void test_sector_p3_bottom(void) {
    // 真下: (234, 400) → dx=0, dy=166
    TEST_ASSERT_EQUAL_UINT8(2, selectSector(234, 400));
}

// P4 (左): dx < 0, |dx| >= |dy|
void test_sector_p4_left(void) {
    // 真左: (50, 234) → dx=-184, dy=0
    TEST_ASSERT_EQUAL_UINT8(3, selectSector(50, 234));
}

// 右上象限（dx > 0, dy < 0）で |dy| > |dx| → P1
void test_sector_upper_right_steep(void) {
    // (254, 134) → dx=20, dy=-100
    TEST_ASSERT_EQUAL_UINT8(0, selectSector(254, 134));
}

// 右上象限で |dx| > |dy| → P2
void test_sector_upper_right_shallow(void) {
    // (334, 214) → dx=100, dy=-20
    TEST_ASSERT_EQUAL_UINT8(1, selectSector(334, 214));
}

// 左下象限（dx < 0, dy > 0）で |dy| > |dx| → P3
void test_sector_lower_left_steep(void) {
    // (214, 334) → dx=-20, dy=100
    TEST_ASSERT_EQUAL_UINT8(2, selectSector(214, 334));
}

// 左下象限で |dx| > |dy| → P4
void test_sector_lower_left_shallow(void) {
    // (134, 254) → dx=-100, dy=20
    TEST_ASSERT_EQUAL_UINT8(3, selectSector(134, 254));
}

// ========================================================================
// 境界付近
// ========================================================================

// 45 度線上（|dx| == |dy|）: 右上 → P2（左右を優先する実装）
void test_sector_boundary_upper_right_45deg(void) {
    // (284, 184) → dx=50, dy=-50
    uint8_t result = selectSector(284, 184);
    // |dx| == |dy| → else 分岐 → dx > 0 → P2
    TEST_ASSERT_EQUAL_UINT8(1, result);
}

// 45 度線上: 左下 → P4
void test_sector_boundary_lower_left_45deg(void) {
    // (184, 284) → dx=-50, dy=50
    uint8_t result = selectSector(184, 284);
    // |dx| == |dy| → else 分岐 → dx < 0 → P4
    TEST_ASSERT_EQUAL_UINT8(3, result);
}

// 45 度線上: 右下 → P2
void test_sector_boundary_lower_right_45deg(void) {
    // (284, 284) → dx=50, dy=50
    uint8_t result = selectSector(284, 284);
    TEST_ASSERT_EQUAL_UINT8(1, result);
}

// 45 度線上: 左上 → P4
void test_sector_boundary_upper_left_45deg(void) {
    // (184, 184) → dx=-50, dy=-50
    uint8_t result = selectSector(184, 184);
    TEST_ASSERT_EQUAL_UINT8(3, result);
}

// 中心点そのもの → dx=0, dy=0 → else 分岐 → dx > 0 ではない → P4
void test_sector_center(void) {
    uint8_t result = selectSector(234, 234);
    // dx=0, dy=0: |dy| > |dx| は false → else → dx > 0 は false → P4
    TEST_ASSERT_EQUAL_UINT8(3, result);
}

// ========================================================================
// 半径判定
// ========================================================================

// 外周リング上（半径 >= 165）
void test_outer_ring_on_boundary(void) {
    // (234, 234 - 165) = (234, 69) → 半径 165
    TEST_ASSERT_TRUE(isOnOuterRing(234, 69));
}

// 外周リング外（半径 < 165）
void test_outer_ring_inside(void) {
    // (234, 234 - 164) = (234, 70) → 半径 164
    TEST_ASSERT_TRUE(isInInnerZone(234, 70));
}

// キャンセル領域（半径 < 145）
void test_cancel_zone(void) {
    // (234, 234 - 144) = (234, 90) → 半径 144
    TEST_ASSERT_TRUE(isInCancelZone(234, 90));
}

// キャンセル領域境界外（半径 >= 145）
void test_cancel_zone_boundary(void) {
    // (234, 234 - 145) = (234, 89) → 半径 145
    TEST_ASSERT_FALSE(isInCancelZone(234, 89));
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // 4 扇形の判定
    RUN_TEST(test_sector_p1_top);
    RUN_TEST(test_sector_p2_right);
    RUN_TEST(test_sector_p3_bottom);
    RUN_TEST(test_sector_p4_left);
    RUN_TEST(test_sector_upper_right_steep);
    RUN_TEST(test_sector_upper_right_shallow);
    RUN_TEST(test_sector_lower_left_steep);
    RUN_TEST(test_sector_lower_left_shallow);

    // 境界付近
    RUN_TEST(test_sector_boundary_upper_right_45deg);
    RUN_TEST(test_sector_boundary_lower_left_45deg);
    RUN_TEST(test_sector_boundary_lower_right_45deg);
    RUN_TEST(test_sector_boundary_upper_left_45deg);
    RUN_TEST(test_sector_center);

    // 半径判定
    RUN_TEST(test_outer_ring_on_boundary);
    RUN_TEST(test_outer_ring_inside);
    RUN_TEST(test_cancel_zone);
    RUN_TEST(test_cancel_zone_boundary);

    return UNITY_END();
}
