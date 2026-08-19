#pragma once

// EDH（統率者戦）ファームウェアのアプリケーション制御層。
//
// FaB 版 AppController と同じ構造（begin() / update()）で EDH 版を実装する。
// 入力パイプライン: ボタン入力 -> タッチ入力 -> ジェスチャー検出 -> ドメイン更新 -> 描画
//
// EDH 固有の制御:
// - 4 扇形のタッチ判定 (selectSector)
// - 内側タップによるビュー切替・被弾元選択
// - 統率者ダメージビュー中の外周スライドで統率者ダメージ操作
// - 無操作 10 秒でライフビューへ復帰

#include "domain/edh_match_state.hpp"
#include "domain/edh_life_change.hpp"
#include "app/edh_screen_state.hpp"
#include "input/button_input.hpp"
#include "input/gesture_detector.hpp"
#include "infra/haptics_m5.hpp"
#include "infra/edh_storage_nvs.hpp"
#include "ui/edh_renderer.hpp"

namespace counter::app {

class EdhAppController {
public:
    void begin();

    /// メインループから毎フレーム呼ばれる。
    /// nowMs は main_edh.cpp の millis() から渡される値。
    void update(uint32_t nowMs);

private:
    edh::MatchState state_{};
    edh::app::EdhScreenState screenState_;
    input::GestureDetector gesture_;
    input::ButtonInput buttonInput_;
    ui::EdhRenderer renderer_;
    infra::Haptics haptics_;
    infra::EdhStorageNvs storage_;

    // --- タッチ状態の検出 ---
    bool prevTouching_ = false;
    int16_t prevTouchX_ = 0;
    int16_t prevTouchY_ = 0;

    // --- 内側タップ判定用 ---
    // タッチ開始時の座標と時刻を記録し、離した瞬間に移動量と時間で判定する。
    int16_t tapStartX_ = 0;
    int16_t tapStartY_ = 0;
    uint32_t tapStartMs_ = 0;
    bool innerTouchStarted_ = false;  // 内側領域でタッチが開始されたか

    // --- プレビュー変化検出 ---
    input::GesturePreview prevPreview_{};
    input::GestureState prevGestureState_ = input::GestureState::Idle;

    // --- ロック中タッチ警告の連発防止 ---
    bool lockTouchWarned_ = false;

    // --- 長押し進捗の再描画抑制 ---
    uint8_t prevHoldPercent_ = 0;

    // --- スライド中の対象プレイヤー ---
    // スライド開始位置の扇形のプレイヤーが操作対象。
    // 統率者ダメージビュー中に自扇形から開始したスライドかどうかの判定にも使う。
    uint8_t slidePlayerIndex_ = 0;

    // --- ボタンイベント処理 ---
    void handleButtonEvent(input::ButtonEvent event, uint32_t nowMs);

    /// EdhScreenState の ScreenAction を実行する。
    void executeScreenAction(edh::app::ScreenAction action);

    /// 進行中のジェスチャーを破棄する。
    void cancelOngoingGesture();

    /// 現在の画面に対応する描画メソッドを呼ぶ。
    void drawCurrentScreen(uint32_t nowMs);

    /// 内側タップの判定と処理。
    /// touchUp 時に呼ばれ、タップだった場合は true を返す。
    bool handleInnerTap(int16_t x, int16_t y, uint32_t nowMs);
};

}  // namespace counter::app
