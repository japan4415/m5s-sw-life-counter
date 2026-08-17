// test/test_gesture/test_gesture.cpp
//
// ジェスチャーロジックのホスト単体テスト（L1）
// 設計の正は docs/05-ui-ux.md の角度計算仕様と docs/11-testing.md のテストケース。
// 実装ではなく設計書の要求を検証する。

#include <unity.h>
#include <cmath>
#include <cstdint>

#include "domain/life_change.hpp"
#include "input/touch_zone.hpp"
#include "input/gesture_detector.hpp"

using counter::domain::PlayerId;
using counter::input::GestureDetector;
using counter::input::GestureState;
using counter::input::GestureResult;
using counter::input::GesturePreview;
using counter::input::normalizeDeltaDegrees;
using counter::input::radiusFromCenter;
using counter::input::isOnRing;
using counter::input::isInCancelZone;
using counter::input::selectPlayer;
using counter::input::isValidStartAngle;

// ========================================================================
// 定数とヘルパ
// ========================================================================

// 画面中心座標（docs/05: 実測値 468/2 = 234）
static constexpr float kCenterX = 234.0f;
static constexpr float kCenterY = 234.0f;
static constexpr float kPi = 3.14159265358979323846f;

// 座標ヘルパ: 中心からの半径（px）と角度（度）で画面座標を組み立てる。
// マジックナンバーの座標直書きを避け、テストの意図を明確にする。
// 画面座標系: 角度 0 = 右、90 = 下、180 = 左、270 = 上
struct Point {
    int16_t x;
    int16_t y;
};

static Point makePoint(float radiusPx, float angleDeg) {
    float rad = angleDeg * kPi / 180.0f;
    return {
        static_cast<int16_t>(
            std::round(kCenterX + radiusPx * std::cos(rad))),
        static_cast<int16_t>(
            std::round(kCenterY + radiusPx * std::sin(rad)))
    };
}

// scoped enum を比較するヘルパマクロ。
// Unity の TEST_ASSERT_EQUAL は C-style cast で変換するが、
// C++ の scoped enum では明示的キャストが安全。
#define ASSERT_ENUM_EQ(expected, actual) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), \
                          static_cast<int>(actual))

// 感度定数: config::kDegreesPerLife と一致させること。
// 感度変更時はここだけ更新すればテスト全体に反映される。
static constexpr float kTestDegreesPerLife = 36.0f;

// 段階数から必要な移動角度（度）を計算するヘルパ。
static float angleForSteps(int steps) {
    return kTestDegreesPerLife * static_cast<float>(steps);
}

// GestureDetector のインスタンス（setUp() で毎回 reset）
static GestureDetector gd;

void setUp(void) {
    gd.reset();
}

void tearDown(void) {
    // クリーンアップ不要
}

// ========================================================================
// touch_zone.hpp の純関数テスト
// ========================================================================

// 角度の境界またぎ: normalizeDeltaDegrees が +-180 度をまたぐ差分を正しく畳む
// docs/05: 360度/0度の境界をまたぐ際のデルタ値を正規化する
void test_normalize_delta_boundary_crossing(void) {
    // 360度/0度境界の時計回りまたぎ: 350->10 は生の差分 -340 -> +20
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f,
                             normalizeDeltaDegrees(-340.0f));

    // 360度/0度境界の反時計回りまたぎ: 10->350 は生の差分 +340 -> -20
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f,
                             normalizeDeltaDegrees(340.0f));

    // ちょうど 180 度は 180 に（(-180, 180] の右端）
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f,
                             normalizeDeltaDegrees(180.0f));

    // -180 度は +180 に正規化される（(-180, 180] の左端は含まない）
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f,
                             normalizeDeltaDegrees(-180.0f));

    // 小さい正の差分はそのまま
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f,
                             normalizeDeltaDegrees(10.0f));

    // 小さい負の差分はそのまま
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f,
                             normalizeDeltaDegrees(-10.0f));

    // 540 度（1.5 回転）-> 180 度に正規化
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f,
                             normalizeDeltaDegrees(540.0f));

    // -540 度 -> 180 度に正規化
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f,
                             normalizeDeltaDegrees(-540.0f));

    // 0 度はそのまま
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f,
                             normalizeDeltaDegrees(0.0f));
}

// ========================================================================
// GestureDetector のテスト
// ========================================================================

// 最低移動角: 6 度未満の移動では確定しない（committed == false）
// docs/05: 累積移動角が 6 度未満の場合は操作として扱わない
void test_minimum_movement_below_threshold(void) {
    // 下側許可領域（90 度）でタッチ開始
    Point p0 = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p0.x, p0.y, 0);
    ASSERT_ENUM_EQ(GestureState::Candidate, gd.state());

    // 約 5 度時計回りに移動（6 度未満）
    Point p1 = makePoint(200.0f, 95.0f);
    gd.onTouchMove(p1.x, p1.y, 100);

    // Candidate のまま（Active にならない）
    ASSERT_ENUM_EQ(GestureState::Candidate, gd.state());

    // 指を離す -> 確定しない
    GestureResult result = gd.onTouchUp(200);
    TEST_ASSERT_FALSE(result.committed);
}

// 方向反転: 反時計回りに 5 段階 -> 時計回りに 2 段階戻して離す -> deltaLife == -3
// docs/05: -5 と +2 の 2 件ではなく、-3 で 1 件として確定
// docs/11: 1 件として記録される
void test_direction_reversal_net_minus_3(void) {
    // 下側許可領域（90 度）でタッチ開始
    Point start = makePoint(200.0f, 90.0f);
    gd.onTouchDown(start.x, start.y, 0);

    // 反時計回りに約 7 度移動して Active にする（角度減少方向）
    Point p1 = makePoint(200.0f, 83.0f);
    gd.onTouchMove(p1.x, p1.y, 100);
    ASSERT_ENUM_EQ(GestureState::Active, gd.state());

    // 反時計回りに累計 angleForSteps(5) = 180 度移動（5 段階分: -180 / 36 = -5）
    // 開始点 90 度 - 180 度 = -90 度（三角関数は負角も正しく処理する）
    Point p2 = makePoint(200.0f, 90.0f - angleForSteps(5));
    gd.onTouchMove(p2.x, p2.y, 1100);

    // 時計回りに angleForSteps(2) = 72 度戻す（累計 -108 / 36 = -3）
    Point p3 = makePoint(200.0f, 90.0f - angleForSteps(5) + angleForSteps(2));
    gd.onTouchMove(p3.x, p3.y, 2100);

    // 指を離す -> -3 で確定
    GestureResult result = gd.onTouchUp(3100);
    TEST_ASSERT_TRUE(result.committed);
    TEST_ASSERT_EQUAL_INT32(-3, result.deltaLife);
}

// 角速度による異常値除外: abs(deltaDeg)/deltaMs > 0.7 のサンプルは累積に加算されない
// docs/05: 0.7 度/ms（700 度/秒）以上のジャンプはそのサンプルを無視する
void test_angular_speed_filtering(void) {
    // 下側許可領域（90 度）でタッチ開始
    Point p0 = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p0.x, p0.y, 0);

    // 正常な移動で Active にする（約 37 度を 100ms -> 0.37 deg/ms < 0.7）
    // angleForSteps(1) = 36 度を超える移動量で、後続の受理分と合わせて 1 段階を確保
    Point p1 = makePoint(200.0f, 127.0f);
    gd.onTouchMove(p1.x, p1.y, 100);
    ASSERT_ENUM_EQ(GestureState::Active, gd.state());

    // 異常な角速度（約 50 度を 10ms -> 5.0 deg/ms > 0.7）-> フィルタされる
    Point p2 = makePoint(200.0f, 177.0f);
    gd.onTouchMove(p2.x, p2.y, 110);

    // 正常な移動（p2->p3 の約 5 度を 100ms -> 0.05 deg/ms < 0.7）-> 受理
    // 注: prevAngle_ は p2 の角度に更新済みなので、差分は p2->p3 間のみ
    Point p3 = makePoint(200.0f, 182.0f);
    gd.onTouchMove(p3.x, p3.y, 210);

    // 指を離す -> フィルタされた p2 の約 50 度分は含まれない
    // 累積: p0->p1 の約 37 度 + p2->p3 の約 5 度 = 約 42 度 -> 1 段階
    GestureResult result = gd.onTouchUp(300);
    TEST_ASSERT_TRUE(result.committed);
    TEST_ASSERT_EQUAL_INT32(1, result.deltaLife);
}

// 欠測後の正当な操作が棄却されないこと（270ms 欠測の回帰テスト）
// Phase 0 実測で最大 270ms のサンプル間隔が確認されたことに基づく
// docs/11: 角速度が正常範囲なら棄却されず、累積角度に正しく加算される
void test_gap_tolerance_270ms_regression(void) {
    // 下側許可領域（90 度）でタッチ開始
    Point p0 = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p0.x, p0.y, 0);

    // 正常な最初の移動で Active にする
    Point p1 = makePoint(200.0f, 97.0f);
    gd.onTouchMove(p1.x, p1.y, 100);
    ASSERT_ENUM_EQ(GestureState::Active, gd.state());

    // 270ms の欠測後に約 63 度の変化
    // 角速度: 63 / 270 = 0.233 deg/ms -- 実測の快適操作時角速度に相当
    // 0.7 未満のため正当な操作として受理されるべき
    Point p2 = makePoint(200.0f, 160.0f);
    gd.onTouchMove(p2.x, p2.y, 370);

    // 欠測後の移動が累積に加算されていることを確認
    GestureResult result = gd.onTouchUp(400);
    TEST_ASSERT_TRUE(result.committed);
    // 累積: 約 7 + 約 63 = 約 70 度 -> 1 段階（70 / 36 = 1.94 切り捨て）
    // 欠測サンプルなしでは約 7 度 / 36 = 0 段階で不成立のため、受理を証明できる
    TEST_ASSERT_EQUAL_INT32(1, result.deltaLife);
}

// 中央引き込みでキャンセル: 半径 145px 未満に入ったら Cancelled、
// 離しても committed == false
// docs/05: 半径 145px 未満に達すると操作全体をキャンセルする
void test_cancel_by_center_pull_in(void) {
    // 下側許可領域（90 度）でタッチ開始
    Point p0 = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p0.x, p0.y, 0);
    ASSERT_ENUM_EQ(GestureState::Candidate, gd.state());

    // Active に遷移させる（約 15 度時計回りに移動）
    Point p1 = makePoint(200.0f, 105.0f);
    gd.onTouchMove(p1.x, p1.y, 100);
    ASSERT_ENUM_EQ(GestureState::Active, gd.state());

    // 中央方向へ引き込む（半径 100px < 145px -> キャンセル領域）
    Point center = makePoint(100.0f, 105.0f);
    gd.onTouchMove(center.x, center.y, 200);
    ASSERT_ENUM_EQ(GestureState::Cancelled, gd.state());

    // 指を離しても確定しない
    GestureResult result = gd.onTouchUp(300);
    TEST_ASSERT_FALSE(result.committed);
}

// 対象プレイヤーの固定: 下半分で開始して上半分へ移動しても player が変わらない
// docs/05: タッチ開始時に決定した対象プレイヤーを操作終了まで固定
// docs/06: 不変条件 9 -- スライド中に上下境界を越えても対象は変わらない
void test_player_fixed_during_slide(void) {
    // 下側許可領域（90 度、y > 234）でタッチ開始 -> Bottom が選択
    Point start = makePoint(200.0f, 90.0f);
    gd.onTouchDown(start.x, start.y, 0);

    // Active にする
    Point p1 = makePoint(200.0f, 97.0f);
    gd.onTouchMove(p1.x, p1.y, 100);

    // 上半分（角度 270 度 = 画面上方、y < 234）へ大きく移動
    Point p2 = makePoint(200.0f, 270.0f);
    gd.onTouchMove(p2.x, p2.y, 2000);

    // プレビューのプレイヤーが変わらず Bottom のまま
    GesturePreview preview = gd.preview();
    ASSERT_ENUM_EQ(PlayerId::Bottom, preview.player);

    // 確定結果のプレイヤーも Bottom のまま
    GestureResult result = gd.onTouchUp(3000);
    ASSERT_ENUM_EQ(PlayerId::Bottom, result.player);
}

// リング外からの開始は無視: 半径 165 未満でタッチ開始しても Candidate にならない
// docs/05: 外周リング下限 165px 未満では操作として認めない
void test_ring_outside_start_ignored(void) {
    // リング外（半径 100px < 165px）でタッチ開始
    Point p = makePoint(100.0f, 45.0f);
    gd.onTouchDown(p.x, p.y, 0);

    // Idle のまま（Candidate にならない）
    ASSERT_ENUM_EQ(GestureState::Idle, gd.state());

    // その後リング上に移動しても反応しない
    Point p2 = makePoint(200.0f, 90.0f);
    gd.onTouchMove(p2.x, p2.y, 100);
    ASSERT_ENUM_EQ(GestureState::Idle, gd.state());

    // 離しても committed == false
    GestureResult result = gd.onTouchUp(200);
    TEST_ASSERT_FALSE(result.committed);
}

// 時計回りで deltaLife が正になること（Phase 0 実測で確定した符号の回帰テスト）
// docs/05: 時計回り = ライフ増加、atan2() の符号反転は不要
void test_clockwise_positive_delta_regression(void) {
    // 下側許可領域（90 度）でタッチ開始
    Point p0 = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p0.x, p0.y, 0);

    // 時計回りに angleForSteps(3) = 108 度移動（画面座標系で角度増加方向 = 時計回り）
    Point p1 = makePoint(200.0f, 90.0f + angleForSteps(3));
    gd.onTouchMove(p1.x, p1.y, 500);

    // 指を離す -> deltaLife は正の値
    GestureResult result = gd.onTouchUp(600);
    TEST_ASSERT_TRUE(result.committed);
    TEST_ASSERT_TRUE(result.deltaLife > 0);
    // angleForSteps(3) = 108 度 / 36 度per ライフ = 3 段階
    TEST_ASSERT_EQUAL_INT32(3, result.deltaLife);
}

// consumeStepChanged() が消費型であること（2 回目は false）
void test_consume_step_changed_is_consumable(void) {
    // 下側許可領域（90 度）でタッチ開始
    Point p0 = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p0.x, p0.y, 0);

    // angleForSteps(1) = 36 度以上移動してステップ変化を発生させる
    // 36 度/ライフなので、累積 40 度で prevStep_ 0 -> 1 に変化
    Point p1 = makePoint(200.0f, 130.0f);
    gd.onTouchMove(p1.x, p1.y, 200);

    // 1 回目: true（ステップ変化あり）
    TEST_ASSERT_TRUE(gd.consumeStepChanged());

    // 2 回目: false（消費済み -- ワンショット）
    TEST_ASSERT_FALSE(gd.consumeStepChanged());
}

// deltaMs == 0 のサンプルでゼロ除算やクラッシュが起きないこと
// docs/05: deltaMs が 0 のサンプルはゼロ除算を避けて無視する
void test_delta_ms_zero_no_crash(void) {
    // 下側許可領域（90 度）でタッチ開始（時刻 100ms）
    Point p0 = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p0.x, p0.y, 100);
    ASSERT_ENUM_EQ(GestureState::Candidate, gd.state());

    // 同じ時刻（100ms）で大きく移動 -> deltaMs == 0 -> スキップされるはず
    Point p1 = makePoint(200.0f, 120.0f);
    gd.onTouchMove(p1.x, p1.y, 100);

    // 角度が累積されないので Candidate のまま
    ASSERT_ENUM_EQ(GestureState::Candidate, gd.state());

    // 指を離す -> 確定しない（クラッシュしないことが最重要）
    GestureResult result = gd.onTouchUp(200);
    TEST_ASSERT_FALSE(result.committed);
}

// ========================================================================
// 開始許可領域のテスト
// docs/05: 画面左右の端（各 20 度）は上下どちらのプレイヤーか曖昧なため開始を禁じる
// ========================================================================

// 下側許可領域（20-160 度）から開始 -> Candidate に遷移し、Bottom が選択される
void test_start_zone_bottom_allowed(void) {
    // 角度 90 度（画面下方、許可領域の中央）でタッチ開始
    Point p = makePoint(200.0f, 90.0f);
    gd.onTouchDown(p.x, p.y, 0);

    ASSERT_ENUM_EQ(GestureState::Candidate, gd.state());

    // 拒否されていないこと
    TEST_ASSERT_FALSE(gd.consumeRejectedStart());

    // プレイヤーが Bottom であること（y > 234）
    GestureResult result = gd.onTouchUp(100);
    ASSERT_ENUM_EQ(PlayerId::Bottom, result.player);
}

// 上側許可領域（200-340 度）から開始 -> Candidate に遷移し、Top が選択される
void test_start_zone_top_allowed(void) {
    // 角度 270 度（画面上方、許可領域の中央）でタッチ開始
    Point p = makePoint(200.0f, 270.0f);
    gd.onTouchDown(p.x, p.y, 0);

    ASSERT_ENUM_EQ(GestureState::Candidate, gd.state());

    // 拒否されていないこと
    TEST_ASSERT_FALSE(gd.consumeRejectedStart());

    // プレイヤーが Top であること（y < 234）
    GestureResult result = gd.onTouchUp(100);
    ASSERT_ENUM_EQ(PlayerId::Top, result.player);
}

// 左側禁止領域（160-200 度）から開始 -> Idle のまま、ジェスチャーが始まらない
void test_start_zone_left_forbidden(void) {
    // 角度 180 度（画面左端、禁止領域の中央）でタッチ開始
    Point p = makePoint(200.0f, 180.0f);
    gd.onTouchDown(p.x, p.y, 0);

    // Idle のまま（Candidate にならない）
    ASSERT_ENUM_EQ(GestureState::Idle, gd.state());

    // 禁止領域で拒否されたことを検知できる
    TEST_ASSERT_TRUE(gd.consumeRejectedStart());

    // 移動しても反応しない
    Point p2 = makePoint(200.0f, 120.0f);
    gd.onTouchMove(p2.x, p2.y, 100);
    ASSERT_ENUM_EQ(GestureState::Idle, gd.state());

    // 離しても committed == false
    GestureResult result = gd.onTouchUp(200);
    TEST_ASSERT_FALSE(result.committed);
}

// 右側禁止領域（340-20 度、0 度をまたぐ）から開始 -> Idle のまま
void test_start_zone_right_forbidden(void) {
    // 角度 350 度（画面右上端、禁止領域）でタッチ開始
    // selectPlayer: sin(350) < 0 -> y < 234 -> Top
    // isValidStartAngle(~350, Top): 350 > 340 -> false
    Point p = makePoint(200.0f, 350.0f);
    gd.onTouchDown(p.x, p.y, 0);

    // Idle のまま
    ASSERT_ENUM_EQ(GestureState::Idle, gd.state());

    // 拒否されたことを検知できる
    TEST_ASSERT_TRUE(gd.consumeRejectedStart());

    // 離しても committed == false
    GestureResult result = gd.onTouchUp(100);
    TEST_ASSERT_FALSE(result.committed);
}

// consumeRejectedStart() が消費型であること（2 回目は false）
void test_consume_rejected_start_is_consumable(void) {
    // 禁止領域（角度 0 度 = 画面右端）でタッチ開始
    // selectPlayer(234) -> Bottom, isValidStartAngle(0, Bottom) -> false
    Point p = makePoint(200.0f, 0.0f);
    gd.onTouchDown(p.x, p.y, 0);

    // Idle のまま
    ASSERT_ENUM_EQ(GestureState::Idle, gd.state());

    // 1 回目: true（拒否された）
    TEST_ASSERT_TRUE(gd.consumeRejectedStart());

    // 2 回目: false（消費済み -- ワンショット）
    TEST_ASSERT_FALSE(gd.consumeRejectedStart());
}

// 開始許可領域の境界値テスト（isValidStartAngle 純関数）
// docs/05: 上側 200-340 度、下側 20-160 度、禁止 160-200 度 / 340-20 度
// 実装: >= と <= で境界を含む
void test_start_zone_boundary_values(void) {
    // -- 許可領域の境界値（含まれること）--

    // 下側: 20 度（許可領域の左端）
    TEST_ASSERT_TRUE(isValidStartAngle(20.0f, PlayerId::Bottom));
    // 下側: 160 度（許可領域の右端）
    TEST_ASSERT_TRUE(isValidStartAngle(160.0f, PlayerId::Bottom));
    // 上側: 200 度（許可領域の左端）
    TEST_ASSERT_TRUE(isValidStartAngle(200.0f, PlayerId::Top));
    // 上側: 340 度（許可領域の右端）
    TEST_ASSERT_TRUE(isValidStartAngle(340.0f, PlayerId::Top));

    // -- 許可領域の中央（明確に許可）--

    TEST_ASSERT_TRUE(isValidStartAngle(90.0f, PlayerId::Bottom));
    TEST_ASSERT_TRUE(isValidStartAngle(270.0f, PlayerId::Top));

    // -- 禁止領域（境界のすぐ外）--

    // 下側 20 度の外（右側禁止帯）
    TEST_ASSERT_FALSE(isValidStartAngle(19.0f, PlayerId::Bottom));
    // 下側 160 度の外（左側禁止帯）
    TEST_ASSERT_FALSE(isValidStartAngle(161.0f, PlayerId::Bottom));
    // 上側 200 度の外（左側禁止帯）
    TEST_ASSERT_FALSE(isValidStartAngle(199.0f, PlayerId::Top));
    // 上側 340 度の外（右側禁止帯）
    TEST_ASSERT_FALSE(isValidStartAngle(341.0f, PlayerId::Top));

    // -- 禁止領域の中央 --

    // 左側禁止帯の中央（180 度）-- どちらのプレイヤーでも禁止
    TEST_ASSERT_FALSE(isValidStartAngle(180.0f, PlayerId::Bottom));
    TEST_ASSERT_FALSE(isValidStartAngle(180.0f, PlayerId::Top));

    // 右側禁止帯の中央（0 度）-- どちらのプレイヤーでも禁止
    TEST_ASSERT_FALSE(isValidStartAngle(0.0f, PlayerId::Bottom));
    TEST_ASSERT_FALSE(isValidStartAngle(0.0f, PlayerId::Top));

    // 右側禁止帯の他の角度
    TEST_ASSERT_FALSE(isValidStartAngle(355.0f, PlayerId::Top));
    TEST_ASSERT_FALSE(isValidStartAngle(10.0f, PlayerId::Bottom));
}

// 感度回帰テスト: kDegreesPerLife = 36 度/ライフの境界値
// 一周 360 度がちょうど 10 段階になることを境界値テストで検証する:
//   angleForSteps(1) - 1 度（= 35 度）では 0 段階、
//   angleForSteps(1) 度（= 36 度）でちょうど 1 段階。
// 36 x 10 = 360 なので、1 段階 = 36 度が正しければ一周 = 10 段階も保証される。
void test_sensitivity_36deg_boundary(void) {
    // --- 35 度では 0 段階（境界未到達）---
    {
        gd.reset();
        Point p0 = makePoint(200.0f, 90.0f);
        gd.onTouchDown(p0.x, p0.y, 0);

        // angleForSteps(1) - 1 = 35 度時計回りに移動（35 / 36 = 0.97 -> 切り捨て 0）
        Point p1 = makePoint(200.0f, 90.0f + angleForSteps(1) - 1.0f);
        gd.onTouchMove(p1.x, p1.y, 500);

        // |35| >= 6 なので Active にはなるが、段階は 0 のため確定しない
        ASSERT_ENUM_EQ(GestureState::Active, gd.state());
        GestureResult result = gd.onTouchUp(600);
        TEST_ASSERT_FALSE(result.committed);
    }

    // --- 36 度でちょうど 1 段階（境界到達）---
    {
        gd.reset();
        Point p0 = makePoint(200.0f, 90.0f);
        gd.onTouchDown(p0.x, p0.y, 0);

        // angleForSteps(1) = 36 度時計回りに移動（36 / 36 = 1.0 -> 切り捨て 1）
        Point p1 = makePoint(200.0f, 90.0f + angleForSteps(1));
        gd.onTouchMove(p1.x, p1.y, 500);

        GestureResult result = gd.onTouchUp(600);
        TEST_ASSERT_TRUE(result.committed);
        TEST_ASSERT_EQUAL_INT32(1, result.deltaLife);
    }
}

// ========================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // touch_zone 純関数テスト
    RUN_TEST(test_normalize_delta_boundary_crossing);

    // GestureDetector テスト
    RUN_TEST(test_minimum_movement_below_threshold);
    RUN_TEST(test_direction_reversal_net_minus_3);
    RUN_TEST(test_angular_speed_filtering);
    RUN_TEST(test_gap_tolerance_270ms_regression);
    RUN_TEST(test_cancel_by_center_pull_in);
    RUN_TEST(test_player_fixed_during_slide);
    RUN_TEST(test_ring_outside_start_ignored);
    RUN_TEST(test_clockwise_positive_delta_regression);
    RUN_TEST(test_consume_step_changed_is_consumable);
    RUN_TEST(test_delta_ms_zero_no_crash);

    // 感度回帰テスト
    RUN_TEST(test_sensitivity_36deg_boundary);

    // 開始許可領域テスト
    RUN_TEST(test_start_zone_bottom_allowed);
    RUN_TEST(test_start_zone_top_allowed);
    RUN_TEST(test_start_zone_left_forbidden);
    RUN_TEST(test_start_zone_right_forbidden);
    RUN_TEST(test_consume_rejected_start_is_consumable);
    RUN_TEST(test_start_zone_boundary_values);

    return UNITY_END();
}
