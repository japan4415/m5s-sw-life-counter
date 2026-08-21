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
// タップ判定しきい値の定数テスト
// ========================================================================

// kTapMaxDurationMs はタッチセンサの最大欠測 (270ms) を考慮して
// 600ms に設定されている。300ms では欠測と重なりタップが拒否されうる。
void test_tap_max_duration_accommodates_dropout(void) {
    // 270ms の欠測 + 200ms の物理タップ = 470ms のケースを通すには
    // 閾値が少なくとも 470ms 以上でなければならない
    TEST_ASSERT_TRUE(kTapMaxDurationMs >= 470);
    // かつ長押しと誤判定しないよう 1000ms 未満であるべき
    TEST_ASSERT_TRUE(kTapMaxDurationMs < 1000);
}

void test_tap_max_duration_is_600(void) {
    TEST_ASSERT_EQUAL_UINT32(600, kTapMaxDurationMs);
}

void test_tap_max_move_is_20(void) {
    TEST_ASSERT_EQUAL_INT16(20, kTapMaxMovePx);
}

// ========================================================================
// EDH 用スライド開始角度判定
// ========================================================================

// P1(上) 中心角 270° → 許可
void test_start_angle_p1_center(void) {
    TEST_ASSERT_TRUE(isValidStartAngleEdh(270.0f));
}

// P2(右) 中心角 0° → 許可
void test_start_angle_p2_center(void) {
    TEST_ASSERT_TRUE(isValidStartAngleEdh(0.0f));
}

// P3(下) 中心角 90° → 許可
void test_start_angle_p3_center(void) {
    TEST_ASSERT_TRUE(isValidStartAngleEdh(90.0f));
}

// P4(左) 中心角 180° → 許可
void test_start_angle_p4_center(void) {
    TEST_ASSERT_TRUE(isValidStartAngleEdh(180.0f));
}

// P1/P2 境界 45° → 不感帯で拒否
void test_start_angle_boundary_45(void) {
    TEST_ASSERT_FALSE(isValidStartAngleEdh(45.0f));
}

// P2/P3 境界 135° → 不感帯で拒否
void test_start_angle_boundary_135(void) {
    TEST_ASSERT_FALSE(isValidStartAngleEdh(135.0f));
}

// P3/P4 境界 225° → 不感帯で拒否
void test_start_angle_boundary_225(void) {
    TEST_ASSERT_FALSE(isValidStartAngleEdh(225.0f));
}

// P4/P1 境界 315° → 不感帯で拒否
void test_start_angle_boundary_315(void) {
    TEST_ASSERT_FALSE(isValidStartAngleEdh(315.0f));
}

// 不感帯の端: 45° - 15° = 30° → ちょうど境界（許可側）
void test_start_angle_deadzone_edge_low(void) {
    // kEdhDeadZoneHalfWidthDeg = 15° → 30° は abs(30-45) = 15 で
    // 不感帯の外（<15 でなく ==15 は許可）
    TEST_ASSERT_TRUE(isValidStartAngleEdh(30.0f));
}

// 不感帯の内側: 44° → abs(44-45) = 1 < 15 → 拒否
void test_start_angle_inside_deadzone(void) {
    TEST_ASSERT_FALSE(isValidStartAngleEdh(44.0f));
}

// P2 領域で FaB が拒否する角度: 5° → EDH では許可
void test_start_angle_fab_rejected_but_edh_allows(void) {
    // FaB は 0° 付近を禁止するが、EDH では P2 の中心付近なので許可
    TEST_ASSERT_TRUE(isValidStartAngleEdh(5.0f));
}

// P4 領域で FaB が拒否する角度: 175° → EDH では許可
void test_start_angle_fab_rejected_but_edh_allows_p4(void) {
    TEST_ASSERT_TRUE(isValidStartAngleEdh(175.0f));
}

// 359° → P2 の中心付近、許可
void test_start_angle_near_360(void) {
    TEST_ASSERT_TRUE(isValidStartAngleEdh(359.0f));
}

// ========================================================================
// 座標回転ヘルパー
// ========================================================================

// 真右 (468, 234) → 90° CCW → 真上 (234, 0)
void test_rotate_right_to_top(void) {
    int16_t outX, outY;
    rotateCCW90(468, 234, outX, outY);
    TEST_ASSERT_EQUAL_INT16(234, outX);
    TEST_ASSERT_EQUAL_INT16(0, outY);
}

// 真左 (0, 234) → 90° CCW → 真下 (234, 468)
void test_rotate_left_to_bottom(void) {
    int16_t outX, outY;
    rotateCCW90(0, 234, outX, outY);
    TEST_ASSERT_EQUAL_INT16(234, outX);
    TEST_ASSERT_EQUAL_INT16(468, outY);
}

// 中心 (234, 234) → 回転しても中心のまま
void test_rotate_center_stays(void) {
    int16_t outX, outY;
    rotateCCW90(234, 234, outX, outY);
    TEST_ASSERT_EQUAL_INT16(234, outX);
    TEST_ASSERT_EQUAL_INT16(234, outY);
}

// 真上 (234, 0) → 90° CCW → 真左 (0, 234)
void test_rotate_top_to_left(void) {
    int16_t outX, outY;
    rotateCCW90(234, 0, outX, outY);
    TEST_ASSERT_EQUAL_INT16(0, outX);
    TEST_ASSERT_EQUAL_INT16(234, outY);
}

// ========================================================================
// needsCoordinateRotation
// ========================================================================

void test_needs_rotation_p1_no(void) {
    TEST_ASSERT_FALSE(needsCoordinateRotation(0));
}

void test_needs_rotation_p2_yes(void) {
    TEST_ASSERT_TRUE(needsCoordinateRotation(1));
}

void test_needs_rotation_p3_no(void) {
    TEST_ASSERT_FALSE(needsCoordinateRotation(2));
}

void test_needs_rotation_p4_yes(void) {
    TEST_ASSERT_TRUE(needsCoordinateRotation(3));
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

    // タップしきい値
    RUN_TEST(test_tap_max_duration_accommodates_dropout);
    RUN_TEST(test_tap_max_duration_is_600);
    RUN_TEST(test_tap_max_move_is_20);

    // EDH スライド開始角度判定
    RUN_TEST(test_start_angle_p1_center);
    RUN_TEST(test_start_angle_p2_center);
    RUN_TEST(test_start_angle_p3_center);
    RUN_TEST(test_start_angle_p4_center);
    RUN_TEST(test_start_angle_boundary_45);
    RUN_TEST(test_start_angle_boundary_135);
    RUN_TEST(test_start_angle_boundary_225);
    RUN_TEST(test_start_angle_boundary_315);
    RUN_TEST(test_start_angle_deadzone_edge_low);
    RUN_TEST(test_start_angle_inside_deadzone);
    RUN_TEST(test_start_angle_fab_rejected_but_edh_allows);
    RUN_TEST(test_start_angle_fab_rejected_but_edh_allows_p4);
    RUN_TEST(test_start_angle_near_360);

    // 座標回転ヘルパー
    RUN_TEST(test_rotate_right_to_top);
    RUN_TEST(test_rotate_left_to_bottom);
    RUN_TEST(test_rotate_center_stays);
    RUN_TEST(test_rotate_top_to_left);

    // needsCoordinateRotation
    RUN_TEST(test_needs_rotation_p1_no);
    RUN_TEST(test_needs_rotation_p2_yes);
    RUN_TEST(test_needs_rotation_p3_no);
    RUN_TEST(test_needs_rotation_p4_yes);

    return UNITY_END();
}
