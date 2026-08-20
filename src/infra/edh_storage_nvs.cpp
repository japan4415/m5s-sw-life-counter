// EDH（統率者戦）ファームウェアの NVS 永続化の実装。
//
// 既存 storage_nvs.cpp と同じ 16 スロットローテーション + CRC32 方式。
// NVS namespace は "edh"（FaB 版 "lifectr" とは分離）。

#include "edh_storage_nvs.hpp"

#include <Preferences.h>
#include <esp_crc.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "app_config.hpp"

namespace counter::infra {

// ============================================================
// 内部ヘルパ
// ============================================================

uint32_t EdhStorageNvs::calcCrc32(const EdhPersistentRecord& rec) {
    const size_t crcOffset = offsetof(EdhPersistentRecord, crc32);
    return esp_crc32_le(0, reinterpret_cast<const uint8_t*>(&rec), crcOffset);
}

void EdhStorageNvs::slotKey(int slot, char* buf) {
    snprintf(buf, 4, "s%02d", slot);
}

// ============================================================
// 公開 API
// ============================================================

bool EdhStorageNvs::begin() {
    Preferences prefs;

    // NVS namespace "edh" で開く（FaB 版 "lifectr" とは分離）
    if (!prefs.begin("edh", false)) {
        Serial.println("[EdhStorageNvs] NVS の初期化に失敗しました");
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    hasValid_ = false;
    currentSlot_ = -1;
    currentSeq_ = 0;

    // 全 16 スロットをスキャンし、有効なレコードのうち sequence 最大のものを採用する
    for (int i = 0; i < kSlotCount; ++i) {
        char key[4];
        slotKey(i, key);

        EdhPersistentRecord rec{};
        size_t readLen = prefs.getBytes(key, &rec, sizeof(rec));

        if (readLen != sizeof(rec)) {
            continue;
        }

        if (rec.magic != kMagic) {
            Serial.printf("[EdhStorageNvs] スロット %d: magic 不一致 (0x%08X)\n",
                          i, rec.magic);
            continue;
        }

        if (rec.schemaVersion != kSchemaVersion) {
            Serial.printf("[EdhStorageNvs] スロット %d: schemaVersion 不一致 (%u)\n",
                          i, rec.schemaVersion);
            continue;
        }

        if (rec.payloadSize != sizeof(counter::edh::MatchState)) {
            Serial.printf("[EdhStorageNvs] スロット %d: payloadSize 不一致 (%u, 期待 %u)\n",
                          i, rec.payloadSize,
                          static_cast<unsigned>(sizeof(counter::edh::MatchState)));
            continue;
        }

        uint32_t expected = calcCrc32(rec);
        if (rec.crc32 != expected) {
            Serial.printf("[EdhStorageNvs] スロット %d: CRC 不一致 (格納 0x%08X, 計算 0x%08X)\n",
                          i, rec.crc32, expected);
            continue;
        }

        // 符号付き差分比較でラップアラウンドに対応
        if (!hasValid_ ||
            static_cast<int32_t>(rec.sequence - currentSeq_) > 0) {
            hasValid_ = true;
            currentSlot_ = i;
            currentSeq_ = rec.sequence;
            loadedState_ = rec.state;
        }
    }

    // 感度設定を読み出す
    {
        uint8_t sensVal = prefs.getUChar(
            "sens", static_cast<uint8_t>(config::kDefaultSensitivityIndex));
        sensitivityIndex_ = (sensVal < config::kSensitivityPresetCount)
            ? sensVal
            : static_cast<uint8_t>(config::kDefaultSensitivityIndex);
    }

    prefs.end();

    if (hasValid_) {
        Serial.printf("[EdhStorageNvs] スロット %d から復元 (sequence=%u)\n",
                      currentSlot_, currentSeq_);
    } else {
        Serial.println("[EdhStorageNvs] 有効なレコードなし（初回起動または全スロット不正）");
    }

    return true;
}

bool EdhStorageNvs::save(const counter::edh::MatchState& state) {
    if (!initialized_) {
        return false;
    }

    int nextSlot = (currentSlot_ + 1) % kSlotCount;
    uint32_t nextSeq = currentSeq_ + 1;

    EdhPersistentRecord rec{};
    rec.magic = kMagic;
    rec.schemaVersion = kSchemaVersion;
    rec.payloadSize = static_cast<uint16_t>(sizeof(counter::edh::MatchState));
    rec.sequence = nextSeq;
    rec.state = state;
    rec.crc32 = calcCrc32(rec);

    Preferences prefs;
    if (!prefs.begin("edh", false)) {
        Serial.println("[EdhStorageNvs] save: NVS を開けませんでした");
        prefs.end();
        return false;
    }

    char key[4];
    slotKey(nextSlot, key);

    size_t written = prefs.putBytes(key, &rec, sizeof(rec));
    prefs.end();

    if (written != sizeof(rec)) {
        Serial.printf("[EdhStorageNvs] save: スロット %d への書き込み失敗 "
                      "(written=%u, expected=%u, recSize=%u)\n",
                      nextSlot, static_cast<unsigned>(written),
                      static_cast<unsigned>(sizeof(rec)),
                      static_cast<unsigned>(sizeof(rec)));
        return false;
    }

    currentSlot_ = nextSlot;
    currentSeq_ = nextSeq;

    Serial.printf("[EdhStorageNvs] スロット %d に保存 (sequence=%u)\n",
                  currentSlot_, currentSeq_);
    return true;
}

bool EdhStorageNvs::hasValidState() const {
    return hasValid_;
}

const counter::edh::MatchState& EdhStorageNvs::loadedState() const {
    assert(hasValid_ && "loadedState() は hasValidState() が true のときのみ呼ぶこと");
    return loadedState_;
}

bool EdhStorageNvs::saveSensitivity(uint8_t index) {
    if (!initialized_) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin("edh", false)) {
        Serial.println("[EdhStorageNvs] saveSensitivity: NVS を開けませんでした");
        prefs.end();
        return false;
    }

    size_t written = prefs.putUChar("sens", index);
    prefs.end();

    if (written == 0) {
        Serial.println("[EdhStorageNvs] saveSensitivity: 書き込み失敗");
        return false;
    }

    sensitivityIndex_ = index;
    Serial.printf("[EdhStorageNvs] 感度プリセット %u を保存\n",
                  static_cast<unsigned>(index));
    return true;
}

uint8_t EdhStorageNvs::loadedSensitivity() const {
    return sensitivityIndex_;
}

}  // namespace counter::infra
