#pragma once

#include <cstddef>
#include <cstdint>

#include "domain/life_change.hpp"

namespace counter::app {

// counter::PlayerId を counter::app からも非修飾で参照できるようにする。
using counter::PlayerId;

enum class Screen : uint8_t { Setup, Active, Menu, History, About, Sensitivity };

enum class MenuItem : uint8_t {
    Resume, History, SetLife, SetSensitivity, Rematch, About
};
constexpr uint8_t kMenuItemCount = 6;

// 画面側では実行できず、アプリ層に実行させたい動作。
// 各入力ハンドラの戻り値として返し、アプリ層が dispatch する。
enum class ScreenAction : uint8_t {
    None,
    StartMatch,    // Setup で確定。setupLife() の値で試合を開始する
    Rematch,       // 確認済み
};

/// 画面遷移とメニュー選択の状態機械。
/// ハードウェアに一切依存しない。M5Unified.h / Arduino.h を
/// include せず、ホスト（pio test -e native）でテストできる。
///
/// 入力メソッド（onNext / onSelect / onLongPressB / onCloseMenu）は
/// ButtonInput が検出したイベントに応じてアプリ層が呼び出す。
/// 戻り値の ScreenAction をアプリ層が domain 層へ dispatch する。
class ScreenState {
public:
    void reset();                       // Setup 画面から開始する

    Screen  screen() const;
    uint8_t menuIndex() const;          // 0..kMenuItemCount-1
    MenuItem menuItem() const;
    bool     awaitingConfirm() const;   // Rematch の長押し確認待ち
    MenuItem confirmTarget() const;

    uint32_t setupLife(PlayerId player) const;
    void     setSetupLife(PlayerId player, uint32_t life);

    uint8_t sensitivityIndex() const;
    void    setSensitivityIndex(uint8_t index);

    // 入力。戻り値はアプリ層が実行すべき動作。
    ScreenAction onNext();          // A 短押し
    ScreenAction onSelect();        // B 短押し
    ScreenAction onLongPressB();    // B 長押し
    ScreenAction onCloseMenu();     // A+B 長押し

    void enterActive();             // 試合開始後にアプリ層が呼ぶ

    /// 再描画が必要か（消費型）。
    /// 状態変化（画面遷移・menuIndex 変化・確認待ち変化・setupLife 変化）
    /// ごとに内部フラグを立て、1 回だけ true を返して落とす。
    /// GestureDetector::consumeStepChanged() と同じ消費型パターン。
    bool consumeDirty();

private:
    // FaB の Classic Constructed 想定。プリセットトグルも 20 / 40。
    static constexpr uint32_t kDefaultLife = 40;

    Screen   screen_      = Screen::Setup;
    uint8_t  menuIndex_   = 0;
    bool     confirming_  = false;       // Rematch の長押し確認待ち
    MenuItem confirmTarget_ = MenuItem::Resume;  // confirming_ が true のときだけ有効
    uint32_t setupLife_[2] = {kDefaultLife, kDefaultLife};  // [0]=Top, [1]=Bottom
    uint8_t  sensitivityIndex_ = 1;      // 感度プリセットインデックス（デフォルト: 10 ライフ/周）
    bool     dirty_       = true;        // 初期状態は描画が必要

    void markDirty();
};

}  // namespace counter::app
