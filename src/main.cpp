// Phase 1: ライフカウンター本体 -- エントリポイント
//
// 外周スライドジェスチャーで FaB のライフを増減する。
// Phase 2 で追加予定の機能:
//   - Undo（物理ボタン A）
//   - タッチロック（物理ボタン B）
//   - 初期ライフ設定画面
//   - Rematch / New Game メニュー
//   - 永続化、電源管理は Phase 3 以降
//
// メインループでは delay() を一切使用しない（docs/07-architecture.md）。
// 時刻は millis() で取得し、AppController::update() に引数として渡す。
// AppController 内部では millis() を呼ばない（時刻源をここに集約するため）。

#include <M5Unified.h>
#include "app/app_controller.hpp"

counter::app::AppController app;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    app.begin();
}

void loop() {
    M5.update();
    app.update(millis());
}
