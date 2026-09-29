#pragma once

// 統合ファームウェアのゲームモード選択制御。
//
// 単一バイナリに FaB / MTG EDH / Riftbound の 3 ゲームモードを収録する
// （docs/17-unified-firmware-spec.md）。本クラスは最上位のディスパッチャとして:
//
//   1. 起動時に NVS（namespace "sys"）から最終選択モードを読み出す
//   2. そのモードに進行中の試合があれば、そのモードを直接起動する
//      （従来の「電源断からの自動復元」挙動を維持する）
//   3. 進行中の試合がなければゲーム選択画面を表示する
//   4. 選択されたモードのコントローラを begin() して駆動する
//   5. メニューの Switch Game による切替要求を受け、ゲーム選択画面へ戻る
//
// 各ゲームモードの試合状態は従来どおり各自の NVS namespace
//（"lifectr" / "edh" / "rift"）に保存されるため、モード切替・電源断で失われない。
//
// 選択画面は静的な全画面描画（カーソル移動時のみ再描画）であり、
// キャンバスを持たない。ボタンのみで操作する（タッチは受け付けない）。

#include <cstdint>

#include "app/i_app_controller.hpp"
#include "infra/edh_storage_nvs.hpp"
#include "infra/haptics_m5.hpp"
#include "infra/mode_store.hpp"
#include "infra/riftbound_storage_nvs.hpp"
#include "infra/storage_nvs.hpp"
#include "input/button_input.hpp"

namespace counter::app {

class AppLauncher {
public:
    /// 3 ゲームモードのコントローラを登録する。
    /// 引数の並びは infra::GameMode の列挙順（Fab, Edh, Riftbound）と
    /// 一致させること。
    void setControllers(IAppController** controllers, uint8_t count);

    /// M5.begin() の後に呼ぶ。モードを決定し、選択画面 or モード起動をする。
    void begin();

    /// メインループから毎フレーム呼ばれる。nowMs は millis() 由来。
    void update(uint32_t nowMs);

private:
    enum class State : uint8_t {
        ModeSelect,  // ゲーム選択画面を表示中
        Running,     // いずれかのモードのコントローラを駆動中
    };

    /// 選択されたモードを起動する。モードを NVS に保存し、
    /// コントローラの begin()（NVS 復元判定を含む）を呼ぶ。
    void enterMode(infra::GameMode mode);

    /// ゲーム選択画面へ遷移する。進行中モードの振動を停止し、画面を描く。
    void showModeSelect();

    /// ゲーム選択画面を描画する（全画面・カーソル移動時に呼ぶ）。
    void drawModeSelect();

    /// 指定モードに進行中の試合があるか（NVS 復元対象か）。
    bool modeHasActiveMatch(infra::GameMode mode) const;

    IAppController* controllers_[infra::kGameModeCount] = {};
    uint8_t controllerCount_ = 0;

    infra::ModeStore modeStore_;
    infra::Haptics haptics_;
    input::ButtonInput buttonInput_;  // 選択画面でのボタン判定

    // --- 各モードの NVS ストア（進行中試合の判定用） ---
    // begin() で全スロットを走査し、modeHasActiveMatch() が参照する。
    // コントローラ側も同一定義（namespace / magic）のストアを持つため、
    // ここでの判定とコントローラの復元結果は常に一致する。
    infra::StorageNvs fabStorage_;
    infra::EdhStorageNvs edhStorage_;
    infra::RiftboundStorageNvs riftboundStorage_;

    State state_ = State::ModeSelect;
    infra::GameMode cursor_ = infra::GameMode::Fab;       // 選択画面のカーソル
    infra::GameMode runningMode_ = infra::GameMode::Fab;  // 駆動中のモード
};

}  // namespace counter::app
