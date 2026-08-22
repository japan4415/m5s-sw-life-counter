#pragma once

#include <cstdint>

// FaB / EDH 両バリアントで同一の画面・メニュー語彙。
// 旧来は各バリアントの screen_state ヘッダに重複定義されていたが、
// Phase 3 の共通化（MenuNav 抽出）でここに統合した。
//
// 注意: enum 値の並び・数値は既存ファームウェアとの互換性のため一切変更しない。
// Renderer やアプリ層がインデックス直参照しているため並び替えは禁止。

namespace counter::app {

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

}  // namespace counter::app
