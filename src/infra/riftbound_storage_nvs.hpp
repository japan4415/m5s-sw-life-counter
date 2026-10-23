#pragma once

// Riftbound（1v1 得点カウンター）ファームウェアの NVS 永続化。
//
// 実装は共通テンプレート infra::NvsStateStore（nvs_state_store.hpp）。
// このヘッダは storage_nvs.hpp（FaB 版）/ edh_storage_nvs.hpp（EDH 版）と
// 同じ薄い別名定義で、namespace "rift" / magic 0x52425353 の設定を固定して提供する。
//
// NVS namespace は "rift"（FaB 版 "lifectr" / EDH 版 "edh" とは分離）。
// 相互に干渉しない設計。

#include "domain/riftbound_match_state.hpp"
#include "nvs_state_store.hpp"

namespace counter::infra {

/// Riftbound 版 NVS 永続化クラス（namespace "rift"）。
class RiftboundStorageNvs final
    : public NvsStateStore<counter::riftbound::MatchState> {
public:
    RiftboundStorageNvs()
        : NvsStateStore<counter::riftbound::MatchState>(
              "rift",
              0x52425353,  // "RBSS" (RiftBound Score State)
              1,
              "[RiftboundStorageNvs]",
              false) {}
};

}  // namespace counter::infra
