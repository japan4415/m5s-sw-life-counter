#pragma once

#include <cstdint>

#include "domain/match_state.hpp"

namespace counter::infra {

/// NVS 永続化レコード。
/// docs/08-persistence.md の PersistentRecord 設計に準拠する。
/// __attribute__((packed)) は使わず自然アライメントのまま。
struct PersistentRecord {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    uint32_t sequence;
    counter::domain::MatchState state;
    uint32_t crc32;
};

// コンパイル時にサイズが固定であることを保証する
static_assert(sizeof(PersistentRecord) > 0,
              "PersistentRecord のサイズが 0 になっている");

// NVS 単一エントリ上限（約 4000 バイト）を超えていないことを保証する
static_assert(sizeof(PersistentRecord) <= 4000,
              "PersistentRecord が NVS 単一エントリ上限 (4000 bytes) を超えている");

/// NVS を用いた試合状態の永続化。
///
/// 16 スロットのローテーション書き込みにより NVS の劣化を抑制する。
/// 起動時に全スロットをスキャンし、CRC・magic 等の検証に合格した
/// 最大 sequence のレコードを採用して復元する。
///
/// NVS 初期化に失敗した場合でもアプリの動作を止めず、
/// save() が no-op（false を返す）として安全に動作する。
class StorageNvs {
public:
    /// NVS を初期化し、全 16 スロットをスキャンして最新の有効レコードを復元する。
    /// @return 初期化成功時 true。失敗時は false を返し、以降の呼び出しは no-op。
    bool begin();

    /// 次のスロットに試合状態を書き込む。
    /// sequence をインクリメントし、CRC を計算して格納する。
    /// @return 書き込み成功時 true。
    bool save(const counter::domain::MatchState& state);

    /// 有効な復元データがあるか。
    bool hasValidState() const;

    /// 復元された試合状態への参照。
    /// hasValidState() が true のときのみ有効。
    const counter::domain::MatchState& loadedState() const;

    /// 感度プリセットのインデックスを NVS に保存する。
    /// 試合状態とは別キー "sens" で管理し、既存スキーマを壊さない。
    /// @return 書き込み成功時 true。
    bool saveSensitivity(uint8_t index);

    /// NVS から読み出した感度プリセットのインデックスを返す。
    /// begin() で読み出し済み。値が存在しない場合はデフォルト値（1 = 10 ライフ/周）。
    uint8_t loadedSensitivity() const;

private:
    static constexpr uint32_t kMagic         = 0x4C434D53;  // "LCMS" (Life Counter Match State)
    static constexpr uint16_t kSchemaVersion  = 1;
    static constexpr int      kSlotCount      = 16;

    /// CRC-32 を計算する（レコード先頭から crc32 フィールド直前まで）。
    static uint32_t calcCrc32(const PersistentRecord& rec);

    /// スロット番号からキー文字列を生成する（"s00"〜"s15"）。
    static void slotKey(int slot, char* buf);

    bool initialized_ = false;   // begin() が成功したか
    bool hasValid_    = false;   // 有効なレコードが見つかったか
    int  currentSlot_ = -1;      // 最新レコードのスロット番号（-1 = なし）
    uint32_t currentSeq_ = 0;    // 最新レコードの sequence
    counter::domain::MatchState loadedState_{};
    uint8_t sensitivityIndex_ = 1;  // デフォルト: 10 ライフ/周（config::kDefaultSensitivityIndex）
};

}  // namespace counter::infra
