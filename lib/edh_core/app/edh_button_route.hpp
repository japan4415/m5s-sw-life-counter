#pragma once

// EDH ボタンルーティング表。
//
// 画面 x ButtonEvent からどの EdhScreenState メソッドを呼ぶかを決定する
// 純関数。ハードウェアに一切依存せず、ホストテストで網羅的に検証できる。
//
// handleButtonEvent の switch ロジックをここに集約することで、
// 「ScreenState 単体テストは通るのにアプリ層のルーティングが間違っている」
// という類のバグをテストで検出可能にする。

#include <cstdint>

#include "app/edh_screen_state.hpp"
#include "input/button_input.hpp"

namespace counter::edh::app {

/// resolveButtonRoute が返す値。
/// どの EdhScreenState メソッドを呼ぶかを示す。
enum class ButtonRoute : uint8_t {
    Ignore,       // ScreenState メソッドを呼ばない
    OnNext,       // onNext()
    OnSelect,     // onSelect()
    OnLongPressB, // onLongPressB()
    OnCloseMenu,  // onCloseMenu()
};

/// 画面 x ボタンイベント -> 呼ぶべき ScreenState メソッド。
///
/// Active 画面の Undo / LockToggle はアプリ層で直接処理するため
/// この関数では Ignore を返す。アプリ層は Ignore を受け取った場合でも
/// Active 固有の処理を別途行う。
///
/// FaB 版 app_controller.cpp のルーティングと一致させること:
///   - Active + MenuRequested -> OnCloseMenu (メニューを開く)
///   - Setup + BLongPressed   -> OnLongPressB (試合開始)
///   - Menu  + BLongPressed   -> OnLongPressB (確認の確定)
inline ButtonRoute resolveButtonRoute(Screen screen,
                                      input::ButtonEvent event) {
    switch (screen) {

    case Screen::Active:
        // Active 画面では MenuRequested のみ ScreenState に委譲する。
        // Undo / LockToggle はアプリ層が直接処理する。
        if (event == input::ButtonEvent::MenuRequested) {
            return ButtonRoute::OnCloseMenu;
        }
        return ButtonRoute::Ignore;

    case Screen::Setup:
        switch (event) {
        case input::ButtonEvent::UndoRequested:
            return ButtonRoute::OnNext;
        case input::ButtonEvent::BLongPressed:
            return ButtonRoute::OnLongPressB;
        default:
            return ButtonRoute::Ignore;
        }

    case Screen::Menu:
        switch (event) {
        case input::ButtonEvent::UndoRequested:
            return ButtonRoute::OnNext;
        case input::ButtonEvent::LockToggleRequested:
            return ButtonRoute::OnSelect;
        case input::ButtonEvent::MenuRequested:
            return ButtonRoute::OnCloseMenu;
        case input::ButtonEvent::BLongPressed:
            return ButtonRoute::OnLongPressB;
        default:
            return ButtonRoute::Ignore;
        }

    case Screen::Sensitivity:
        switch (event) {
        case input::ButtonEvent::UndoRequested:
            return ButtonRoute::OnNext;
        case input::ButtonEvent::LockToggleRequested:
            return ButtonRoute::OnSelect;
        case input::ButtonEvent::MenuRequested:
            return ButtonRoute::OnCloseMenu;
        default:
            return ButtonRoute::Ignore;
        }

    default:
        // History, About
        switch (event) {
        case input::ButtonEvent::LockToggleRequested:
            return ButtonRoute::OnSelect;
        case input::ButtonEvent::MenuRequested:
            return ButtonRoute::OnCloseMenu;
        default:
            return ButtonRoute::Ignore;
        }
    }
}

}  // namespace counter::edh::app
