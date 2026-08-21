#pragma once

#include <cstdint>

#include "domain/edh_life_change.hpp"  // kPlayerCount, kSourceNone

namespace counter::edh::app {

enum class Screen : uint8_t { Setup, Active, Menu, History, About, Sensitivity };

enum class MenuItem : uint8_t {
    Resume, History, SetLife, SetSensitivity, Rematch, About
};
constexpr uint8_t kMenuItemCount = 6;

// 各プレイヤーのビュー状態
enum class PlayerView : uint8_t {
    LifeView,       // 通常のライフ表示
    CmdDamageView,  // 統率者ダメージ一覧表示
};

// 画面側では実行できず、アプリ層に実行させたい動作。
enum class ScreenAction : uint8_t {
    None,
    StartMatch,    // Setup で確定。setupLife() の値で試合を開始する
    Rematch,       // 確認済み
};

/// EDH 版の画面遷移とメニュー選択の状態機械。
/// ハードウェアに一切依存しない。ホスト（pio test -e native）でテストできる。
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

    /// 再描画が必要か（消費型）
    bool consumeDirty();

private:
    // EDH の初期ライフは 40
    static constexpr uint32_t kDefaultLife = 40;

    // 無操作タイムアウト（ms）。CmdDamageView からの自動復帰用。
    // 仕様書では 10 秒（設計値、実機調整前提）。
    static constexpr uint32_t kViewTimeoutMs = 10000;

    Screen   screen_      = Screen::Setup;
    uint8_t  menuIndex_   = 0;
    bool     confirming_  = false;
    MenuItem confirmTarget_ = MenuItem::Resume;
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

    bool dirty_ = true;

    void markDirty();
    void resetViewStates();  // 全プレイヤーのビューを LifeView に戻す
};

}  // namespace counter::edh::app
