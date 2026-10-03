#pragma once

// Riftbound（1v1 得点カウンター）本体 -- アプリケーション制御層
//
// 外周スライドジェスチャーによる得点増減の統合制御を行う。
// 構造は FaB 版 AppController と同一であり、差分は次の 3 点:
//   - ドメインが得点（0 から加算、勝利点 8 到達で警告）である
//   - Setup 画面で設定する値が存在しないため、Setup ではタッチを
//     受け付けない（FaB 版は外周スライドで開始ライフを調整する）
//   - メニューに SetLife が存在しないため、Setup への再入処理が不要
//
// docs/07-architecture.md のレイヤ構成に従い、AppController は最上位に位置し、
// domain / input / ui / infra の各層を統合する。

#include "app/riftbound_screen_state.hpp"  // counter::app::Screen / ScreenAction もここ経由で可視化される
#include "domain/riftbound_match_state.hpp"
#include "input/button_input.hpp"
#include "input/gesture_detector.hpp"
#include "infra/haptics_m5.hpp"
#include "infra/riftbound_storage_nvs.hpp"
#include "ui/riftbound_renderer.hpp"

namespace counter::app {

class RiftboundAppController {
public:
    void begin();

    /// メインループから毎フレーム呼ばれる。
    /// nowMs は main_riftbound.cpp の millis() から渡される値。
    /// 内部で millis() を呼ばないことで、時刻源を main に集約する。
    void update(uint32_t nowMs);

private:
    riftbound::MatchState state_{};
    riftbound::app::RiftboundScreenState screenState_;
    input::GestureDetector gesture_;
    input::ButtonInput buttonInput_;
    ui::RiftboundRenderer renderer_;
    infra::Haptics haptics_;
    infra::RiftboundStorageNvs storage_;

    // --- タッチ状態の立ち上がり／立ち下がり検出 ---
    // 前フレームの押下状態と座標を保持し、エッジ検出に使う。
    bool prevTouching_ = false;
    int16_t prevTouchX_ = 0;
    int16_t prevTouchY_ = 0;

    // --- プレビュー変化検出 ---
    // 毎フレーム描画を避けるため、前回のプレビュー状態を保持して差分のみ描画する。
    input::GesturePreview prevPreview_{};
    input::GestureState prevGestureState_ = input::GestureState::Idle;

    // --- ロック中タッチ警告の連発防止 ---
    bool lockTouchWarned_ = false;

    // --- 長押し進捗の再描画抑制 ---
    uint8_t prevHoldPercent_ = 0;

    // --- 診断ログの間引き用 ---
    uint32_t lastHeldLogMs_ = 0;

    // --- ボタンイベント処理 ---
    void handleButtonEvent(input::ButtonEvent event, uint32_t nowMs);

    /// ScreenAction をドメイン層に反映する。
    void executeScreenAction(ScreenAction action);

    /// 進行中のジェスチャーを破棄する。
    void cancelOngoingGesture();

    /// 現在の画面に対応する描画メソッドを呼ぶ。
    void drawCurrentScreen(uint32_t nowMs);

    /// 得点を確定したときの描画と NVS 保存を行う（Active 画面専用）。
    void commitScore(riftbound::PlayerId player, int32_t delta,
                     uint32_t nowMs);
};

}  // namespace counter::app
