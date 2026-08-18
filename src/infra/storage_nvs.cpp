#include "storage_nvs.hpp"

#include <Preferences.h>
#include <esp_crc.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace counter::infra {

// ============================================================
// 内部ヘルパ
// ============================================================

uint32_t StorageNvs::calcCrc32(const PersistentRecord& rec) {
    // crc32 フィールド直前までのバイト数を計算する。
    // PersistentRecord のレイアウト: magic, schemaVersion, payloadSize, sequence, state, crc32
    // crc32 より前の全バイトが対象。
    const size_t crcOffset = offsetof(PersistentRecord, crc32);
    return esp_crc32_le(0, reinterpret_cast<const uint8_t*>(&rec), crcOffset);
}

void StorageNvs::slotKey(int slot, char* buf) {
    // "s00"〜"s15" の 3 文字 + null 終端
    snprintf(buf, 4, "s%02d", slot);
}

// ============================================================
// 公開 API
// ============================================================

bool StorageNvs::begin() {
    Preferences prefs;

    // 名前空間 "lifectr" で NVS を開く（読み書き両用）
    if (!prefs.begin("lifectr", false)) {
        Serial.println("[StorageNvs] NVS の初期化に失敗しました");
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

        PersistentRecord rec{};
        size_t readLen = prefs.getBytes(key, &rec, sizeof(rec));

        if (readLen != sizeof(rec)) {
            // 読み出し失敗またはサイズ不一致 → スキップ
            continue;
        }

        // magic 検証
        if (rec.magic != kMagic) {
            Serial.printf("[StorageNvs] スロット %d: magic 不一致 (0x%08X)\n",
                          i, rec.magic);
            continue;
        }

        // schemaVersion 検証
        if (rec.schemaVersion != kSchemaVersion) {
            Serial.printf("[StorageNvs] スロット %d: schemaVersion 不一致 (%u)\n",
                          i, rec.schemaVersion);
            continue;
        }

        // payloadSize 検証
        if (rec.payloadSize != sizeof(counter::domain::MatchState)) {
            Serial.printf("[StorageNvs] スロット %d: payloadSize 不一致 (%u, 期待 %u)\n",
                          i, rec.payloadSize,
                          static_cast<unsigned>(sizeof(counter::domain::MatchState)));
            continue;
        }

        // CRC-32 検証
        uint32_t expected = calcCrc32(rec);
        if (rec.crc32 != expected) {
            Serial.printf("[StorageNvs] スロット %d: CRC 不一致 (格納 0x%08X, 計算 0x%08X)\n",
                          i, rec.crc32, expected);
            continue;
        }

        // 全検証合格 — sequence が最も新しいものを採用する。
        // 符号付き差分比較により、sequence が 0xFFFFFFFF を超えて
        // ラップアラウンドしても正しく新旧を判定できる。
        // 例: currentSeq_=0xFFFFFFFE, rec.sequence=0x00000001 のとき
        //     (int32_t)(0x00000001 - 0xFFFFFFFE) = 3 > 0 → rec が新しい
        if (!hasValid_ ||
            static_cast<int32_t>(rec.sequence - currentSeq_) > 0) {
            hasValid_ = true;
            currentSlot_ = i;
            currentSeq_ = rec.sequence;
            loadedState_ = rec.state;
        }
    }

    prefs.end();

    if (hasValid_) {
        Serial.printf("[StorageNvs] スロット %d から復元 (sequence=%u)\n",
                      currentSlot_, currentSeq_);
    } else {
        Serial.println("[StorageNvs] 有効なレコードなし（初回起動または全スロット不正）");
    }

    return true;
}

bool StorageNvs::save(const counter::domain::MatchState& state) {
    if (!initialized_) {
        return false;
    }

    // 次のスロットを決定する（ローテーション）
    int nextSlot = (currentSlot_ + 1) % kSlotCount;
    uint32_t nextSeq = currentSeq_ + 1;

    // PersistentRecord を構築する
    PersistentRecord rec{};
    rec.magic = kMagic;
    rec.schemaVersion = kSchemaVersion;
    rec.payloadSize = static_cast<uint16_t>(sizeof(counter::domain::MatchState));
    rec.sequence = nextSeq;
    rec.state = state;
    rec.crc32 = calcCrc32(rec);

    // NVS に書き込む
    Preferences prefs;
    if (!prefs.begin("lifectr", false)) {
        Serial.println("[StorageNvs] save: NVS を開けませんでした");
        // begin() 失敗時はハンドル未取得だが、全パスで end() を呼んで
        // 後片付けを統一する（Preferences::end() は未初期化時でも安全）。
        prefs.end();
        return false;
    }

    char key[4];
    slotKey(nextSlot, key);

    size_t written = prefs.putBytes(key, &rec, sizeof(rec));
    prefs.end();

    if (written != sizeof(rec)) {
        Serial.printf("[StorageNvs] save: スロット %d への書き込み失敗 (%u bytes)\n",
                      nextSlot, static_cast<unsigned>(written));
        return false;
    }

    // 書き込み成功 — 内部状態を更新する
    currentSlot_ = nextSlot;
    currentSeq_ = nextSeq;

    Serial.printf("[StorageNvs] スロット %d に保存 (sequence=%u)\n",
                  currentSlot_, currentSeq_);
    return true;
}

bool StorageNvs::hasValidState() const {
    return hasValid_;
}

const counter::domain::MatchState& StorageNvs::loadedState() const {
    // hasValidState() が false のときに呼ぶのは誤用。
    // デバッグビルドでのみ検出し、リリースビルドでは assert が除去されるため
    // 動作変更やクラッシュを招かない。
    assert(hasValid_ && "loadedState() は hasValidState() が true のときのみ呼ぶこと");
    return loadedState_;
}

}  // namespace counter::infra
