#pragma once

#include <cstdint>

// FaB / EDH / Riftbound の各ゲームモードで同一の画面・メニュー語彙。
// 旧来は各バリアントの screen_state ヘッダに重複定義されていたが、
// Phase 3 の共通化（MenuNav 抽出）でここに統合した。
//
// 注意: enum 値の並び・既存数値は変更しない（追記のみ許容）。
// Renderer やアプリ層がインデックス直参照しているため並び替えは禁止。
// 統合ファームウェアではメニュー項目の追加が enum 値の追記で行われる
// （メニュー選択状態は NVS に永続化しないため、値の追記は保存データと
// 干渉しない）。

namespace counter::app {

enum class Screen : uint8_t { Setup, Active, Menu, History, About, Sensitivity };

enum class MenuItem : uint8_t {
    Resume, History, SetLife, SetSensitivity, Rematch, About,
    SwitchGame,  // 統合ファームウェア: ゲーム選択画面へ戻る（長押し確認あり）
};
constexpr uint8_t kMenuItemCount = 7;

// 画面側では実行できず、アプリ層に実行させたい動作。
// 各入力ハンドラの戻り値として返し、アプリ層が dispatch する。
enum class ScreenAction : uint8_t {
    None,
    StartMatch,    // Setup で確定。setupLife() の値で試合を開始する
    Rematch,       // 確認済み
    SwitchGame,    // 確認済み。ゲーム選択画面（AppLauncher）へ戻る
};

}  // namespace counter::app
