#pragma once

// EDH（統率者戦）ファームウェアの NVS 永続化。
//
// 実装は共通テンプレート infra::NvsStateStore（nvs_state_store.hpp）へ抽出済み。
// このヘッダは既存の呼び出し側（edh_app_controller 等）を無変更のまま保つための
// 薄い別名定義で、namespace "edh" / magic 0x45444853 の設定を固定して提供する。
//
// NVS namespace は "edh"（FaB 版 "lifectr" とは分離）。
// 相互に干渉しない設計。

#include "domain/edh_match_state.hpp"
#include "nvs_state_store.hpp"

namespace counter::infra {

/// EDH 版 NVS 永続化クラス（namespace "edh"）。
class EdhStorageNvs final : public NvsStateStore<counter::edh::MatchState> {
public:
    // 旧実装と同じ namespace / magic / スキーマバージョン / ログ接頭辞で固定する。
    // detailedSaveFailureLog = true: 旧 EdhStorageNvs の詳細形式
    //「(written=%u, expected=%u, recSize=%u)」で失敗ログを出力する。
    EdhStorageNvs()
        : NvsStateStore<counter::edh::MatchState>(
              "edh",
              0x45444853,  // "EDHS" (EDH State)
              1,
              "[EdhStorageNvs]",
              true) {}
};

}  // namespace counter::infra
