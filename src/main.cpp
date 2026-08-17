// src/main.cpp -- Vibration Feel Test Sketch
//
// Purpose:
//   Interactive sketch to find the minimal intensity+duration combination
//   that feels like a satisfying "click" for life counter +/-1 taps.
//
// IMPORTANT NOTE on M5.Power.setVibration(uint8_t level):
//   The argument is INTENSITY (0-255), NOT duration in milliseconds.
//   - level == 0   -> PWM disabled, motor stops
//   - level 1-255  -> duty12 = (level * 0x0FFF) / 255, output to M5IOE1
//                     pwm_ch1 (GPIO9). Motor runs CONTINUOUSLY until
//                     setVibration(0) is called.
//   Therefore, to vibrate for N ms, you must:
//     1. Call setVibration(level) to start
//     2. Wait N ms (non-blocking, using millis())
//     3. Call setVibration(0) to stop
//   Never use delay() for the wait -- use millis()-based tick in loop().
//
// Controls:
//   Touch screen (tap) -> fire one vibration pulse at current settings
//   Left button (BtnA) -> cycle intensity to next value
//   Right button (BtnB) -> cycle duration to next value
//
// Serial output format (machine-parseable):
//   VIB,<level>,<duration_ms>      vibration fired
//   SET,intensity,<level>          intensity changed
//   SET,duration,<ms>              duration changed
//   HB,<uptime_sec>                heartbeat every 3 s

#include <M5Unified.h>

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------
static constexpr int16_t DISPLAY_W = 468;
static constexpr int16_t DISPLAY_H = 468;
static constexpr int16_t CENTER_X  = 234;
static constexpr int16_t CENTER_Y  = 234;

// Intensity levels to cycle through (0-255 scale)
static constexpr uint8_t INTENSITY_LIST[] = {32, 64, 96, 128, 160, 192, 255};
static constexpr int     INTENSITY_COUNT  = sizeof(INTENSITY_LIST) / sizeof(INTENSITY_LIST[0]);
static constexpr int     INTENSITY_INIT   = 3;  // index of 128

// Duration values to cycle through (milliseconds)
static constexpr uint16_t DURATION_LIST[] = {5, 10, 15, 20, 30, 50, 100};
static constexpr int      DURATION_COUNT  = sizeof(DURATION_LIST) / sizeof(DURATION_LIST[0]);
static constexpr int      DURATION_INIT   = 2;  // index of 15

// Heartbeat interval
static constexpr unsigned long HB_INTERVAL_MS = 3000;

// ---------------------------------------------------------------------------
// Non-blocking vibration controller
// ---------------------------------------------------------------------------
static bool          vibActive    = false;
static unsigned long vibStartMs   = 0;
static unsigned long vibDurationMs = 0;

/// Start a vibration pulse (non-blocking).
/// The motor runs at `level` intensity and will be stopped by tickVibration()
/// after `durationMs` milliseconds have elapsed.
static void startVibration(uint8_t level, uint16_t durationMs) {
    M5.Power.setVibration(level);
    vibActive     = true;
    vibStartMs    = millis();
    vibDurationMs = durationMs;
}

/// Must be called every loop() iteration.
/// Stops the motor once the requested duration has elapsed.
static void tickVibration() {
    if (vibActive && (millis() - vibStartMs >= vibDurationMs)) {
        M5.Power.setVibration(0);
        vibActive = false;
    }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int  intensityIdx = INTENSITY_INIT;
static int  durationIdx  = DURATION_INIT;
static bool prevTouching = false;   // for rising-edge touch detection
static unsigned long lastHbMs = 0;
static bool needRedraw = true;      // flag to trigger screen repaint

// ---------------------------------------------------------------------------
// Display rendering
// ---------------------------------------------------------------------------

/// Draw the current settings on screen. Called only when needRedraw is true.
static void drawScreen() {
    uint8_t  curLevel = INTENSITY_LIST[intensityIdx];
    uint16_t curDur   = DURATION_LIST[durationIdx];

    M5.Display.fillScreen(TFT_BLACK);

    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    // Title
    M5.Display.setTextSize(3);
    M5.Display.drawString("VIB TEST", CENTER_X, 80);

    // Intensity value
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("INTENSITY", CENTER_X, 150);
    M5.Display.setTextSize(5);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", curLevel);
        M5.Display.drawString(buf, CENTER_X, 195);
    }

    // Duration value
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("DURATION", CENTER_X, 255);
    M5.Display.setTextSize(5);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u ms", curDur);
        M5.Display.drawString(buf, CENTER_X, 300);
    }

    // Instructions
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString("TAP to test", CENTER_X, 370);

    M5.Display.setTextSize(1.5);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString("L btn: intensity", CENTER_X, 405);
    M5.Display.drawString("R btn: duration", CENTER_X, 430);

    needRedraw = false;
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);

    // Wait for USB-CDC host connection (max 3 s, non-blocking busy-wait)
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) {
        // busy-wait
    }

    // Ensure motor is off at startup
    M5.Power.setVibration(0);

    Serial.println("=== Vibration Feel Test Sketch ===");
    Serial.printf("INIT,rotation=%d,w=%d,h=%d\n",
                  M5.Display.getRotation(),
                  M5.Display.width(),
                  M5.Display.height());
    Serial.println("---");
    Serial.println("Controls:");
    Serial.println("  Touch screen -> fire vibration pulse");
    Serial.println("  Left button  -> cycle intensity");
    Serial.println("  Right button -> cycle duration");
    Serial.println("---");
    Serial.printf("SET,intensity,%u\n", INTENSITY_LIST[intensityIdx]);
    Serial.printf("SET,duration,%u\n", DURATION_LIST[durationIdx]);
    Serial.flush();

    lastHbMs = millis();
    needRedraw = true;
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void loop() {
    M5.update();

    unsigned long now = millis();

    // Non-blocking vibration stop check
    tickVibration();

    // --- Touch: rising-edge detection (fire only on press, not hold) ---
    auto touchCount = M5.Touch.getCount();
    bool touching = false;
    if (touchCount > 0) {
        auto detail = M5.Touch.getDetail(0);
        touching = detail.isPressed();
    }

    if (touching && !prevTouching) {
        // Tap detected -- fire vibration if not already active
        if (!vibActive) {
            uint8_t  level = INTENSITY_LIST[intensityIdx];
            uint16_t dur   = DURATION_LIST[durationIdx];
            startVibration(level, dur);
            Serial.printf("VIB,%u,%u\n", level, dur);
        }
    }
    prevTouching = touching;

    // --- Left button (BtnA): cycle intensity ---
    if (M5.BtnA.wasPressed()) {
        intensityIdx = (intensityIdx + 1) % INTENSITY_COUNT;
        uint8_t newLevel = INTENSITY_LIST[intensityIdx];
        Serial.printf("SET,intensity,%u\n", newLevel);
        needRedraw = true;
    }

    // --- Right button (BtnB): cycle duration ---
    if (M5.BtnB.wasPressed()) {
        durationIdx = (durationIdx + 1) % DURATION_COUNT;
        uint16_t newDur = DURATION_LIST[durationIdx];
        Serial.printf("SET,duration,%u\n", newDur);
        needRedraw = true;
    }

    // --- Redraw screen only when settings changed ---
    if (needRedraw) {
        drawScreen();
    }

    // --- Heartbeat every 3 s ---
    if (now - lastHbMs >= HB_INTERVAL_MS) {
        lastHbMs = now;
        Serial.printf("HB,%lu\n", now / 1000);
    }
}
