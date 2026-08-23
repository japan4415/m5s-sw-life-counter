#pragma once

#include <cstddef>
#include <cstdint>

#include "app/menu_nav.hpp"
#include "domain/life_change.hpp"

namespace counter::app {

// counter::PlayerId を counter::app からも非修飾で参照できるようにする。
using counter::PlayerId;

// Screen / MenuItem / kMenuItemCount / ScreenAction は
// app/screen_types.hpp に統合された（Phase 3 共通化）。
// menu_nav.hpp 経由でこの名前空間に直接見えているため、
// 呼び出し側の変更は不要。

/// FaB 版の画面遷移とメニュー選択の状態機械。
/// ハードウェアに一切依存しない。M5Unified.h / Arduino.h を
/// include せず、ホスト（pio test -e native）でテストできる。
///
/// メニュー遷移の共通部は MenuNav（合成・委譲）に集約されており、
/// このクラスは FaB 固有の状態（プレイヤー別 setupLife・感度）と
/// バリアント差のある入力ハンドラ（onNext / onLongPressB）だけを保持する。
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

    /// 再描画が必要か（消費型）。MenuNav の dirty フラグに委譲する。
    /// 状態変化（画面遷移・menuIndex 変化・確認待ち変化・setupLife 変化）
    /// ごとにフラグが立ち、1 回だけ true を返して落とす。
    /// GestureDetector::consumeStepChanged() と同じ消費型パターン。
    bool consumeDirty();

private:
    // FaB の Classic Constructed 想定。プリセットトグルも 20 / 40。
    static constexpr uint32_t kDefaultLife = 40;

    MenuNav  nav_;                                          // 共通の画面遷移コア
    uint32_t setupLife_[2] = {kDefaultLife, kDefaultLife};  // [0]=Top, [1]=Bottom
    uint8_t  sensitivityIndex_ = 1;      // 感度プリセットインデックス（デフォルト: 10 ライフ/周）
};

}  // namespace counter::app
