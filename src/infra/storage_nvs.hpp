#pragma once

// FaB（ブレインバトルズ）ファームウェアの NVS 永続化。
//
// 実装は共通テンプレート infra::NvsStateStore（nvs_state_store.hpp）へ抽出済み。
// このヘッダは既存の呼び出し側（app_controller 等）を無変更のまま保つための
// 薄い別名定義で、namespace "lifectr" / magic 0x4C434D53 の設定を固定して
// 提供する。

#include "domain/match_state.hpp"
#include "nvs_state_store.hpp"

namespace counter::infra {

/// FaB 版 NVS 永続化クラス（namespace "lifectr"）。
class StorageNvs final : public NvsStateStore<counter::domain::MatchState> {
public:
    // 旧実装と同じ namespace / magic / スキーマバージョン / ログ接頭辞で固定する。
    // detailedSaveFailureLog = false: 旧 StorageNvs の簡易形式
    // 「(%u bytes)」で失敗ログを出力する。
    StorageNvs()
        : NvsStateStore<counter::domain::MatchState>(
              "lifectr",
              0x4C434D53,  // "LCMS" (Life Counter Match State)
              1,
              "[StorageNvs]",
              false) {}
};

}  // namespace counter::infra
