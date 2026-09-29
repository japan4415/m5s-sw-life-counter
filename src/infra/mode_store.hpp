#pragma once

// 統合ファームウェアのゲームモード永続化。
//
// 単一バイナリに 3 ゲームモード（FaB / MTG EDH / Riftbound）を収録する
// （docs/17-unified-firmware-spec.md）。最後に選択されたモードを NVS に
// 保存し、次回起動時のモード決定に使う。
//
// NVS namespace は "sys"（ゲーム状態の "lifectr" / "edh" / "rift" とは分離）。
// 各ゲームモードの試合状態は従来どおり各自の namespace に保存され、
// モード切替・電源断で失われない。
//
// M5Unified.h / Arduino.h（Preferences.h）に依存するため src/infra/ 直下に置く
// （nvs_state_store.hpp と同じ配置規約）。

#include <cstdint>

#include <Arduino.h>
#include <Preferences.h>

namespace counter::infra {

/// ゲームモードの識別子。AppLauncher のコントローラ配列の添字と一致させる。
enum class GameMode : uint8_t {
    Fab = 0,
    Edh = 1,
    Riftbound = 2,
};
constexpr uint8_t kGameModeCount = 3;

class ModeStore {
public:
    /// NVS を初期化する。失敗してもアプリを止めない（load() が既定値を返す）。
    bool begin() {
        Preferences prefs;
        if (!prefs.begin("sys", false)) {
            Serial.println("[ModeStore] NVS の初期化に失敗しました");
            initialized_ = false;
            return false;
        }
        initialized_ = true;
        prefs.end();
        return true;
    }

    /// 保存されたモードを読み出す。未保存・読み出し失敗時は FaB（既定）。
    GameMode load() const {
        Preferences prefs;
        if (!initialized_ || !prefs.begin("sys", true)) {
            return GameMode::Fab;
        }
        const uint8_t raw = prefs.getUChar("mode",
                                           static_cast<uint8_t>(GameMode::Fab));
        prefs.end();
        return (raw < kGameModeCount) ? static_cast<GameMode>(raw)
                                      : GameMode::Fab;
    }

    /// モードを保存する。
    /// @return 書き込み成功時 true。失敗してもアプリの動作は継続する。
    bool save(GameMode mode) const {
        if (!initialized_) {
            return false;
        }
        Preferences prefs;
        if (!prefs.begin("sys", false)) {
            Serial.println("[ModeStore] save: NVS を開けませんでした");
            prefs.end();
            return false;
        }
        const size_t written =
            prefs.putUChar("mode", static_cast<uint8_t>(mode));
        prefs.end();
        if (written == 0) {
            Serial.println("[ModeStore] save: 書き込み失敗");
            return false;
        }
        return true;
    }

private:
    bool initialized_ = false;
};

}  // namespace counter::infra
