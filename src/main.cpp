// 統合ライフ/スコアカウンター -- エントリポイント
//
// 単一のバイナリに 3 ゲームモードを収録する（docs/17-unified-firmware-spec.md）:
//   - for FaB（Flesh and Blood, 1 対 1 ライフカウンター）
//   - for MTG EDH（統率者戦, 4 プレイヤー）
//   - for Riftbound（1 対 1 得点カウンター）
//
// AppLauncher が起動時にモードを決定し（NVS 復元 or ゲーム選択画面）、
// 選択されたコントローラのみを駆動する。メニューの Switch Game で
// 電源を切らずにモードを切り替えられる。
//
// メインループでは delay() を一切使用しない（docs/07-architecture.md）。
// 時刻は millis() で取得し、AppLauncher::update() に引数として渡す。
// 各 AppController 内部では millis() を呼ばない（時刻源をここに集約するため）。

#include <M5Unified.h>

#include "app/app_controller.hpp"
#include "app/app_launcher.hpp"
#include "app/edh_app_controller.hpp"
#include "app/riftbound_app_controller.hpp"

namespace {

// 3 モードのコントローラ。静確保（動的メモリ確保を行わない）。
// 未選択モードのコントローラは begin() されず、canvas も確保されない
// （選択時に begin() されるため、メモリ消費は選択モード分のみ）。
counter::app::AppController fabApp;
counter::app::EdhAppController edhApp;
counter::app::RiftboundAppController riftboundApp;

counter::app::IAppController* controllers[counter::infra::kGameModeCount] = {
    &fabApp,        // infra::GameMode::Fab
    &edhApp,        // infra::GameMode::Edh
    &riftboundApp,  // infra::GameMode::Riftbound
};

counter::app::AppLauncher launcher;

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    launcher.setControllers(controllers,
                            static_cast<uint8_t>(counter::infra::kGameModeCount));
    launcher.begin();
}

void loop() {
    M5.update();
    launcher.update(millis());
}
