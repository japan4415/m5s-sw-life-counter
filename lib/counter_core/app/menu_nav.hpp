#pragma once

#include <cstdint>

#include "app/screen_types.hpp"

// FaB（ScreenState）と EDH（EdhScreenState）で正規化後コード差分ゼロだった
// 画面遷移・メニュー選択ロジックを集約した合成用クラス（Phase 3 抽出）。
//
// - 両 ScreenState がメンバとして保持し、共通ハンドラを委譲する。
//   継承・仮想関数は使わない合成 + 委譲ベース。
// - ハードウェアに一切依存しない。M5Unified.h / Arduino.h を include せず、
//   ホスト（pio test -e native）でテストできる。
// - バリアント固有の状態（setupLife のモデル差、EDH のビュー状態、感度）は
//   各 ScreenState 側が保持する。このクラスは持たない。
// - 再描画要求（dirty フラグ）も両バリアント共通のためここに集約する。
//   バリアント固有状態の変更時は markDirty() を呼んで同じフラグを立てる。

namespace counter::app {

/// 画面遷移とメニュー選択の共通コア状態機械。
///
/// 入力メソッドのうち onSelect / onCloseMenu は FaB / EDH で完全一致している
/// ため本体をここに持つ。onNext / onLongPressB はバリアント差があるため
/// 各 ScreenState 側に残し、cycleMenuItem / cancelConfirm / enterActive の
/// プリミティブ経由でこのクラスの状態を操作する。
class MenuNav {
public:
    /// Setup 画面から開始する初期状態へ戻す。dirty も立つ。
    void reset() {
        screen_        = Screen::Setup;
        menuIndex_     = 0;
        confirming_    = false;
        confirmTarget_ = MenuItem::Resume;
        dirty_         = true;
    }

    // --- 状態アクセサ ---

    Screen  screen() const {
        return screen_;
    }

    uint8_t menuIndex() const {          // 0..kMenuItemCount-1
        return menuIndex_;
    }

    MenuItem menuItem() const {
        return static_cast<MenuItem>(menuIndex_);
    }

    bool awaitingConfirm() const {       // Rematch の長押し確認待ち
        return confirming_;
    }

    MenuItem confirmTarget() const {
        return confirmTarget_;
    }

    // --- Menu 画面でのカーソル移動（各バリアント onNext() の Menu ケースから委譲） ---

    /// 確認待ちを解除してカーソルを次の項目へ循環させる。
    void cycleMenuItem() {
        // 確認待ちを解除する。別の項目へカーソルが移ったのに
        // 前の項目の確認が残っていると、意図しない Rematch を
        // 長押しで実行してしまう危険があるため。
        confirming_ = false;
        menuIndex_  = (menuIndex_ + 1) % kMenuItemCount;
        markDirty();
    }

    // --- 共通入力ハンドラ（FaB / EDH で完全一致） ---

    /// B 短押し。戻り値はアプリ層が実行すべき動作。
    ScreenAction onSelect() {
        switch (screen_) {

        case Screen::Setup:
            // 誤って試合を始めないため、確定は長押し（onLongPressB）のみとする。
            // 短押しで開始できてしまうと、プリセット切り替え（A ボタン）と
            // 間違えて B を押した場合に意図しない試合が始まってしまう。
            return ScreenAction::None;

        case Screen::Menu: {
            const auto item = static_cast<MenuItem>(menuIndex_);
            switch (item) {
            case MenuItem::Resume:
                screen_ = Screen::Active;
                markDirty();
                return ScreenAction::None;

            case MenuItem::History:
                screen_ = Screen::History;
                markDirty();
                return ScreenAction::None;

            case MenuItem::SetLife:
                // Setup 画面へ遷移する。現在のライフを setupLife に写す作業は
                // アプリ層が setSetupLife() を呼んで行う。MenuNav は
                // domain::MatchState に依存しないため、ここでは画面遷移だけ行う。
                screen_ = Screen::Setup;
                markDirty();
                return ScreenAction::None;

            case MenuItem::SetSensitivity:
                // Sensitivity 画面へ遷移する。SetLife → Setup と同じパターン。
                // 現在の感度値はバリアント側が保持しているため、
                // アプリ層での追加処理は不要。
                screen_ = Screen::Sensitivity;
                markDirty();
                return ScreenAction::None;

            case MenuItem::Rematch:
                // 確認待ちにする。長押しで実行を確定する 2 段階操作。
                // 対戦中にうっかりメニューから即 Rematch してしまうのを防ぐ。
                confirming_    = true;
                confirmTarget_ = MenuItem::Rematch;
                markDirty();
                return ScreenAction::None;

            case MenuItem::About:
                screen_ = Screen::About;
                markDirty();
                return ScreenAction::None;
            }
            // MenuItem の全値を switch で網羅しているため到達しない。
            // コンパイラ警告を抑止するためのフォールバック。
            return ScreenAction::None;
        }

        case Screen::History:
            // History からは Menu に戻る。
            screen_ = Screen::Menu;
            markDirty();
            return ScreenAction::None;

        case Screen::About:
            // About からは Menu に戻る。
            screen_ = Screen::Menu;
            markDirty();
            return ScreenAction::None;

        case Screen::Sensitivity:
            // Sensitivity からは Menu に戻る。
            // 感度の反映と NVS 保存はアプリ層が画面遷移を検出して行う。
            screen_ = Screen::Menu;
            markDirty();
            return ScreenAction::None;

        default:
            // Active: onSelect() に割り当てられた動作はない。
            return ScreenAction::None;
        }
    }

    /// A+B 長押し。戻り値はアプリ層が実行すべき動作。
    ScreenAction onCloseMenu() {
        switch (screen_) {

        case Screen::Setup:
            // Setup には戻る先が無い。何もしない（dirty も立たない）。
            return ScreenAction::None;

        case Screen::Active:
            // Active から Menu を開く。menuIndex を先頭（Resume）に戻す。
            screen_    = Screen::Menu;
            menuIndex_ = 0;
            confirming_ = false;
            markDirty();
            return ScreenAction::None;

        case Screen::Menu:
            // Menu から Active へ戻る。確認待ちも解除する。
            confirming_ = false;
            screen_     = Screen::Active;
            markDirty();
            return ScreenAction::None;

        case Screen::History:
            // History から Menu へ戻る。
            screen_ = Screen::Menu;
            markDirty();
            return ScreenAction::None;

        case Screen::About:
            // About から Menu へ戻る。
            screen_ = Screen::Menu;
            markDirty();
            return ScreenAction::None;

        case Screen::Sensitivity:
            // Sensitivity から Menu へ戻る。
            screen_ = Screen::Menu;
            markDirty();
            return ScreenAction::None;
        }
        // Screen の全値を switch で網羅しているため到達しない。
        return ScreenAction::None;
    }

    // --- バリアント側ハンドラ（onNext / onLongPressB）から使うプリミティブ ---

    /// Active へ遷移して dirty を立てる。
    /// アプリ層が試合開始後に呼ぶ場合と、バリアント側 onLongPressB の
    /// Setup 確定・Rematch 確定から呼ばれる場合がある。
    void enterActive() {
        screen_ = Screen::Active;
        markDirty();
    }

    /// 確認待ちのみ解除する。dirty は立てない
    /// （元 onLongPressB の「Rematch 以外の確認先では再描画しない」挙動を維持）。
    void cancelConfirm() {
        confirming_ = false;
    }

    // --- 再描画要求（消費型） ---

    /// 再描画が必要か（消費型）。
    /// 状態変化（画面遷移・menuIndex 変化・確認待ち変化・バリアント固有状態の変化）
    /// ごとに内部フラグを立て、1 回だけ true を返して落とす。
    /// GestureDetector::consumeStepChanged() と同じ消費型パターン。
    bool consumeDirty() {
        const bool wasDirty = dirty_;
        dirty_ = false;
        return wasDirty;
    }

    /// バリアント固有状態（setupLife・感度・EDH ビュー状態）の変更時に
    /// アプリ層・バリアント側から呼ぶ。
    void markDirty() {
        dirty_ = true;
    }

private:
    Screen   screen_      = Screen::Setup;
    uint8_t  menuIndex_   = 0;
    bool     confirming_  = false;               // Rematch の長押し確認待ち
    MenuItem confirmTarget_ = MenuItem::Resume;   // confirming_ が true のときだけ有効
    bool     dirty_       = true;                // 初期状態は描画が必要
};

}  // namespace counter::app
