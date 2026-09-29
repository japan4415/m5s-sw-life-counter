// Riftbound（1v1 得点カウンター）-- エントリポイント
//
// Riftbound の Duel（1v1）を対象とする得点カウンター。
// FaB 版 main.cpp と同じ構造で RiftboundAppController を駆動する。
//
// メインループでは delay() を一切使用しない。
// 時刻は millis() で取得し、RiftboundAppController::update() に引数として渡す。

#include <M5Unified.h>
#include "app/riftbound_app_controller.hpp"

counter::app::RiftboundAppController app;

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
