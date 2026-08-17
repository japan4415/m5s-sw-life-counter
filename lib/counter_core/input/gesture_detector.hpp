#pragma once

#include <cstdint>

#include "domain/life_change.hpp"

namespace counter::input {

enum class GestureState : uint8_t { Idle, Candidate, Active, Cancelled };

/// 指を離したときの結果
struct GestureResult {
    bool     committed;   // ライフ変更を確定すべきか
    PlayerId player;
    int32_t  deltaLife;   // 確定するライフ変化量
};

/// スライド中のプレビュー
struct GesturePreview {
    bool     active;
    PlayerId player;
    int32_t  deltaLife;
};

/// 外周スライドジェスチャーの状態機械。
/// ハードウェアに一切依存せず、時刻は引数で受け取る。
/// これにより Native テスト環境でコンパイル・実行できる。
class GestureDetector {
public:
    void reset();

    /// 指が触れた。ミリ秒は呼び出し側が渡す。
    void onTouchDown(int16_t x, int16_t y, uint32_t nowMs);

    /// 指が動いた。
    void onTouchMove(int16_t x, int16_t y, uint32_t nowMs);

    /// 指が離れた。確定内容を返す。
    GestureResult onTouchUp(uint32_t nowMs);

    GestureState state() const;
    GesturePreview preview() const;

    /// 直前の onTouchMove でライフ段階が変化したか（振動を鳴らす判断に使う）
    bool consumeStepChanged();

    /// 直前の onTouchDown が開始禁止領域で拒否されたか（警告振動を鳴らす判断に使う）
    bool consumeRejectedStart();

private:
    GestureState state_       = GestureState::Idle;
    PlayerId     player_      = PlayerId::Top;
    float        prevAngle_   = 0.0f;  // 前回サンプルの角度（度）
    float        accumDeg_    = 0.0f;  // 累積角度変化量（度）
    int32_t      prevStep_    = 0;     // 前回のライフ段階数
    bool         stepChanged_    = false; // 直前の onTouchMove で段階が変わったか
    bool         rejectedStart_ = false; // 直前の onTouchDown が禁止領域で拒否されたか
    uint32_t     prevMs_        = 0;     // 前回サンプルの時刻（ms）
};

}  // namespace counter::input
