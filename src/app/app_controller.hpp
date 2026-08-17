#pragma once

// Phase 1: ライフカウンター本体 -- アプリケーション制御層
//
// 外周スライドジェスチャーによるライフ増減の統合制御を行う。
// Phase 2 で追加予定の機能（Undo、タッチロック、初期ライフ設定画面、メニュー）は
// まだ実装されていない。
//
// docs/07-architecture.md のレイヤ構成に従い、AppController は最上位に位置し、
// domain / input / ui / infra の各層を統合する。

#include "domain/match_state.hpp"
#include "input/gesture_detector.hpp"
#include "infra/haptics_m5.hpp"
#include "ui/renderer.hpp"

namespace counter::app {

class AppController {
public:
    void begin();

    /// メインループから毎フレーム呼ばれる。
    /// nowMs は main.cpp の millis() から渡される値。
    /// 内部で millis() を呼ばないことで、時刻源を main に集約する。
    void update(uint32_t nowMs);

private:
    domain::MatchState state_{};
    input::GestureDetector gesture_;
    ui::Renderer renderer_;
    infra::Haptics haptics_;

    // --- タッチ状態の立ち上がり／立ち下がり検出 ---
    // 前フレームの押下状態と座標を保持し、エッジ検出に使う。
    // M5.Touch は押下状態しか提供しないため、自前で差分を取る必要がある。
    bool prevTouching_ = false;
    int16_t prevTouchX_ = 0;
    int16_t prevTouchY_ = 0;

    // --- プレビュー変化検出 ---
    // 毎フレーム描画を避けるため、前回のプレビュー状態を保持して差分のみ描画する。
    // 部分再描画でも約 5.0 ms かかるため（docs/07 実測）、変化がないのに呼ぶと無駄になる。
    input::GesturePreview prevPreview_{};
    input::GestureState prevGestureState_ = input::GestureState::Idle;
};

}  // namespace counter::app
