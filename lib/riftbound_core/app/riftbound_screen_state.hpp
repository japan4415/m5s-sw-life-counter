#pragma once

#include <cstdint>

#include "app/menu_nav.hpp"           // 共通の画面遷移コア + counter::app の共用 enum

namespace counter::riftbound::app {

// Screen / MenuItem / kMenuItemCount / ScreenAction は counter_core の
// app/screen_types.hpp に統合されている（Phase 3 共通化）。
// 旧来どおり非修飾名（Screen::Menu 等）で参照できるよう
// using 宣言でこの名前空間へ再エクスポートする。
using counter::app::Screen;
using counter::app::MenuItem;
using counter::app::kMenuItemCount;
using counter::app::ScreenAction;

// 共通の画面遷移コアもメンバ宣言（MenuNav nav_;）で使うため再エクスポートする。
using counter::app::MenuNav;

/// Riftbound 版の画面遷移とメニュー選択の状態機械。
/// ハードウェアに一切依存しない。M5Unified.h / Arduino.h を
/// include せず、ホスト（pio test -e native）でテストできる。
///
/// メニュー遷移の共通部は MenuNav（合成・委譲）に集約されており、
/// このクラスは Riftbound 固有の状態（感度のみ。setupLife に相当する
/// 状態は存在しない）とバリアント差のある入力ハンドラ（onNext）だけを保持する。
///
/// FaB 版 ScreenState との差分:
/// - 得点は常に 0 から始まるため setupLife（開始値設定）を持たない。
///   Setup 画面は試合開始の確認のみに使う（A 短押しのプリセットトグルも無し）。
/// - メニューの SetLife は意味を持たないため、カーソル循環でスキップする。
///   共通 MenuNav は FaB との enum 互換のため 6 項目固定であり、
///   項目自体は削除せず「選択対象から外す」方式で実装する。
class RiftboundScreenState {
public:
    void reset();                       // Setup 画面から開始する

    Screen  screen() const;
    uint8_t menuIndex() const;          // 0..kMenuItemCount-1（SetLife を含む 6 値）
    MenuItem menuItem() const;
    bool     awaitingConfirm() const;   // Rematch の長押し確認待ち
    MenuItem confirmTarget() const;

    uint8_t sensitivityIndex() const;
    void    setSensitivityIndex(uint8_t index);

    // 入力。戻り値はアプリ層が実行すべき動作。
    ScreenAction onNext();          // A 短押し
    ScreenAction onSelect();        // B 短押し
    ScreenAction onLongPressB();    // B 長押し
    ScreenAction onCloseMenu();     // A+B 長押し

    void enterActive();             // 試合開始後にアプリ層が呼ぶ

    /// 再描画が必要か（消費型）。MenuNav の dirty フラグに委譲する。
    bool consumeDirty();

private:
    MenuNav  nav_;                        // 共通の画面遷移コア
    uint8_t  sensitivityIndex_ = 1;       // 感度プリセットインデックス（デフォルト: 10 得点/周）
};

}  // namespace counter::riftbound::app
