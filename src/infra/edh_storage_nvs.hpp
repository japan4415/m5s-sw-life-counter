#pragma once

// EDH（統率者戦）ファームウェアの NVS 永続化。
//
// 既存 storage_nvs と同じ 16 スロットローテーション + CRC32 方式で
// counter::edh::MatchState を保存・復元する。
//
// NVS namespace は "edh"（FaB 版 "lifectr" とは分離）。
// 相互に干渉しない設計。

#include <cstdint>

#include "domain/edh_match_state.hpp"

namespace counter::infra {

/// EDH 版 NVS 永続化レコード。
struct EdhPersistentRecord {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    uint32_t sequence;
    counter::edh::MatchState state;
    uint32_t crc32;
};

// NVS 単一エントリ上限（約 4000 バイト）を超えていないことを保証する
static_assert(sizeof(EdhPersistentRecord) <= 4000,
              "EdhPersistentRecord が NVS 単一エントリ上限 (4000 bytes) を超えている");

/// EDH 版 NVS 永続化クラス。
/// 既存 StorageNvs と同じインターフェースだが namespace "edh" を使用する。
class EdhStorageNvs {
public:
    /// NVS を初期化し、全 16 スロットをスキャンして最新の有効レコードを復元する。
    bool begin();

    /// 次のスロットに試合状態を書き込む。
    bool save(const counter::edh::MatchState& state);

    /// 有効な復元データがあるか。
    bool hasValidState() const;

    /// 復元された試合状態への参照。
    const counter::edh::MatchState& loadedState() const;

    /// 感度プリセットのインデックスを NVS に保存する。
    bool saveSensitivity(uint8_t index);

    /// NVS から読み出した感度プリセットのインデックスを返す。
    uint8_t loadedSensitivity() const;

private:
    // EDH 固有のマジック・スキーマバージョン
    static constexpr uint32_t kMagic         = 0x45444853;  // "EDHS" (EDH State)
    static constexpr uint16_t kSchemaVersion  = 1;
    static constexpr int      kSlotCount      = 16;

    static uint32_t calcCrc32(const EdhPersistentRecord& rec);
    static void slotKey(int slot, char* buf);

    bool initialized_ = false;
    bool hasValid_    = false;
    int  currentSlot_ = -1;
    uint32_t currentSeq_ = 0;
    counter::edh::MatchState loadedState_{};
    uint8_t sensitivityIndex_ = 1;  // デフォルト: 10 ライフ/周
};

}  // namespace counter::infra
