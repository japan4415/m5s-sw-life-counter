// EDH（統率者戦）ライフカウンター -- エントリポイント
//
// 4 プレイヤーの統率者戦向けファームウェア。
// FaB 版 main.cpp と同じ構造で EdhAppController を駆動する。
//
// メインループでは delay() を一切使用しない。
// 時刻は millis() で取得し、EdhAppController::update() に引数として渡す。

#include <M5Unified.h>
#include "app/edh_app_controller.hpp"

counter::app::EdhAppController app;

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
