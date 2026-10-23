#pragma once

// 統合ファームウェアのゲームモード切り替え用インターフェース。
//
// 単一のバイナリに FaB / MTG EDH / Riftbound の 3 ゲームモードを収録する
// （docs/17-unified-firmware-spec.md）。AppLauncher が起動時にモードを選択し、
// 選択されたコントローラのみを駆動する。各コントローラはこのインターフェース
// を実装することで、AppLauncher は具象クラスを知らずに済む。
//
// 注意: lib/counter_core の既存方針（ScreenState / MenuNav など入力ハンドラの
// 共通化）では継承・仮想関数を使わない合成 + 委譲を採用してきた。これは
// 「同一バイナリ内に複数実装が共存しない」前提によるものである。統合
// ファームウェアでは 3 実装が同一バイナリに共存し、実行時に 1 つを選択して
// 駆動するため、この境界のみポリモーフィズムを使用する。

#include <cstdint>

namespace counter::app {

class IAppController {
public:
    virtual ~IAppController() = default;

    /// モード起動時に AppLauncher が呼ぶ。
    /// NVS 復元の判定と初期画面の描画を行う。
    virtual void begin() = 0;

    /// メインループから毎フレーム呼ばれる。nowMs は millis() 由来。
    virtual void update(uint32_t nowMs) = 0;

    /// メニューの Switch Game によるゲーム切替要求を消費型で取得する。
    /// 要求があれば true を 1 回だけ返す。AppLauncher は true を受けたら
    /// 該当コントローラの update() の呼び出しを止め、ゲーム選択画面へ戻る。
    virtual bool consumeSwitchRequested() = 0;
};

}  // namespace counter::app
