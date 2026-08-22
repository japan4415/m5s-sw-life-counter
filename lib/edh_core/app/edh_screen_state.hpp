#pragma once

#include <cstdint>

#include "app/menu_nav.hpp"           // 共通の画面遷移コア + counter::app の共用 enum
#include "domain/edh_life_change.hpp"  // kPlayerCount, kSourceNone

namespace counter::edh::app {

// Screen / MenuItem / kMenuItemCount / ScreenAction は counter_core の
// app/screen_types.hpp に統合された（Phase 3 共通化）。
// 旧来どおり非修飾名（Screen::Menu 等）でも修飾名
// （counter::edh::app::ScreenAction 等）でも参照できるよう
// using 宣言でこの名前空間へ再エクスポートする。
using counter::app::Screen;
using counter::app::MenuItem;
using counter::app::kMenuItemCount;
using counter::app::ScreenAction;

// 共通の画面遷移コアもメンバ宣言（MenuNav nav_;）で使うため再エクスポートする。
using counter::app::MenuNav;

// 各プレイヤーのビュー状態
enum class PlayerView : uint8_t {
    LifeView,       // 通常のライフ表示
    CmdDamageView,  // 統率者ダメージ一覧表示
};

/// EDH 版の画面遷移とメニュー選択の状態機械。
/// ハードウェアに一切依存しない。ホスト（pio test -e native）でテストできる。
///
/// メニュー遷移の共通部は MenuNav（合成・委譲）に集約されており、
/// このクラスは EDH 固有の状態（4 人共通 setupLife・各プレイヤーのビュー・
/// 被弾元選択・タイムアウト）とバリアント差のある入力ハンドラ
/// （onNext / onLongPressB）だけを保持する。
///
/// ACTIVE 内のビュー状態として、各プレイヤーのビュー（LifeView / CmdDamageView）と
/// CmdDamageView 中の被弾元選択を管理する。
/// CmdDamageView を開けるのは同時に 1 プレイヤーのみ（排他制御）。
class EdhScreenState {
public:
    void reset();

    Screen  screen() const;
    uint8_t menuIndex() const;
    MenuItem menuItem() const;
    bool     awaitingConfirm() const;
    MenuItem confirmTarget() const;

    uint32_t setupLife() const;
    void     setSetupLife(uint32_t life);

    uint8_t sensitivityIndex() const;
    void    setSensitivityIndex(uint8_t index);

    // ビュー状態アクセサ
    PlayerView playerView(uint8_t playerIndex) const;

    // CmdDamageView を開いているプレイヤー。kSourceNone = 誰も開いていない
    uint8_t cmdDamageViewPlayer() const;

    // CmdDamageView で選択中の被弾元。kSourceNone = 未選択
    uint8_t selectedSource() const;

    // 内側タップ: 対象プレイヤーの扇形がタップされた。
    // CmdDamageView が開いていなければ開く。開いている本人なら閉じる。
    // 他プレイヤーの扇形へのタップは無視する（被弾元はスライドで決まる）。
    // nowMs はタイムアウト管理用のシステム稼働時間
    void onInnerTap(uint8_t playerIndex, uint32_t nowMs);

    // 操作中の被弾元を設定する（スライド開始時にアプリ層が呼ぶ）。
    // CmdDamageView が開いていないとき、または自分自身の場合は無視する。
    void selectSource(uint8_t sourceIndex, uint32_t nowMs);

    // 操作中の被弾元をクリアする（スライド確定・キャンセル後にアプリ層が呼ぶ）。
    void clearSource();

    // 無操作タイムアウトの確認。nowMs が最終操作時刻から閾値を超えたら LifeView へ復帰
    void checkTimeout(uint32_t nowMs);

    // 操作が行われたことを通知する（タイムアウトタイマーをリセットする）
    void notifyActivity(uint32_t nowMs);

    // ボタン入力。戻り値はアプリ層が実行すべき動作。
    ScreenAction onNext();
    ScreenAction onSelect();
    ScreenAction onLongPressB();
    ScreenAction onCloseMenu();

    void enterActive();

    /// 再描画が必要か（消費型）。MenuNav の dirty フラグに委譲する。
    bool consumeDirty();

private:
    // EDH の初期ライフは 40
    static constexpr uint32_t kDefaultLife = 40;

    // 無操作タイムアウト（ms）。CmdDamageView からの自動復帰用。
    // 仕様書では 10 秒（設計値、実機調整前提）。
    static constexpr uint32_t kViewTimeoutMs = 10000;

    MenuNav  nav_;                        // 共通の画面遷移コア
    uint32_t setupLife_   = kDefaultLife;  // 4 人共通の初期ライフ
    uint8_t  sensitivityIndex_ = 1;       // デフォルト: 10 ライフ/周

    // ACTIVE 内のビュー状態
    PlayerView playerViews_[kPlayerCount] = {
        PlayerView::LifeView, PlayerView::LifeView,
        PlayerView::LifeView, PlayerView::LifeView
    };
    uint8_t cmdDamageViewPlayer_ = kSourceNone;  // CmdDamageView を開いているプレイヤー
    uint8_t selectedSource_      = kSourceNone;  // 選択中の被弾元

    uint32_t lastActivityMs_ = 0;  // 最終操作時刻（タイムアウト計算用）

    void resetViewStates();  // 全プレイヤーのビューを LifeView に戻す
};

}  // namespace counter::edh::app
