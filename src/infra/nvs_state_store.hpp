#pragma once

// ============================================================
// NvsStateStore -- NVS 永続化の共通クラステンプレート
// ============================================================
//
// 旧 storage_nvs（FaB 版）と edh_storage_nvs（EDH 版）は、
// namespace 文字列・magic 値・State 型・ログ接頭辞以外が同一実装であったため、
// 本テンプレートへ統合した。
//
// バリアント固有の差分はすべてコンストラクタ引数として注入する:
//   - nvsNamespace           : NVS namespace 名（FaB "lifectr" / EDH "edh"）
//   - magic                  : レコード先頭のマジック値
//   - schemaVersion          : スキーマバージョン
//   - logTag                 : シリアルログの接頭辞（旧クラス名ベース）
//   - detailedSaveFailureLog : save() 書き込み失敗時のログ形式
//     （true = 旧 EdhStorageNvs の詳細形式、false = 旧 StorageNvs の簡易形式。
//      旧実装のログ文字列を 1 文字も変更しないための区分）
//
// レコードのバイトレイアウト（フィールド構成・並び・自然アライメント）、
// CRC 対象範囲、スロットキー名、"sens" キーの扱いは旧実装と完全に同一であり、
// 保存済みデータとの互換性を保つ。payload サイズは sizeof(StateT) から
// 導かれるため、履歴容量の違い（FaB 64 件 / EDH 16 件）は自動的に吸収される。
//
// Preferences.h（Arduino コア）に依存するため HW 非依存縛りの
// counter_core ライブラリには置かず、src/infra/ 直下に置く。

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cassert>

#include <Arduino.h>
#include <Preferences.h>
#include <esp_crc.h>

#include "app_config.hpp"

namespace counter::infra {

/// StateT を NVS に永続化するストア（ヘッダオンリー）。
///
/// 16 スロットのローテーション書き込みにより NVS の劣化を抑制する。
/// 起動時に全スロットをスキャンし、CRC・magic 等の検証に合格した
/// 最大 sequence のレコードを採用して復元する。
///
/// NVS 初期化に失敗した場合でもアプリの動作を止めず、
/// save() が no-op（false を返す）として安全に動作する。
template <typename StateT>
class NvsStateStore {
public:
    /// バリアント固有の設定を受けてストアを構築する。
    /// 各引数の意味はファイル冒頭のコメントを参照。
    NvsStateStore(const char* nvsNamespace,
                  uint32_t magic,
                  uint16_t schemaVersion,
                  const char* logTag,
                  bool detailedSaveFailureLog)
        : nvsNamespace_(nvsNamespace),
          magic_(magic),
          schemaVersion_(schemaVersion),
          logTag_(logTag),
          detailedSaveFailureLog_(detailedSaveFailureLog) {}

    /// NVS を初期化し、全 16 スロットをスキャンして最新の有効レコードを復元する。
    /// @return 初期化成功時 true。失敗時は false を返し、以降の呼び出しは no-op。
    bool begin() {
        Preferences prefs;

        // NVS を開く（読み書き両用）。namespace 名はバリアントごとに異なる
        if (!prefs.begin(nvsNamespace_, false)) {
            Serial.printf("%s NVS の初期化に失敗しました\r\n", logTag_);
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
            if (rec.magic != magic_) {
                Serial.printf("%s スロット %d: magic 不一致 (0x%08X)\n",
                              logTag_, i, rec.magic);
                continue;
            }

            // schemaVersion 検証
            if (rec.schemaVersion != schemaVersion_) {
                Serial.printf("%s スロット %d: schemaVersion 不一致 (%u)\n",
                              logTag_, i, rec.schemaVersion);
                continue;
            }

            // payloadSize 検証
            if (rec.payloadSize != sizeof(StateT)) {
                Serial.printf("%s スロット %d: payloadSize 不一致 (%u, 期待 %u)\n",
                              logTag_, i, rec.payloadSize,
                              static_cast<unsigned>(sizeof(StateT)));
                continue;
            }

            // CRC-32 検証
            uint32_t expected = calcCrc32(rec);
            if (rec.crc32 != expected) {
                Serial.printf("%s スロット %d: CRC 不一致 (格納 0x%08X, 計算 0x%08X)\n",
                              logTag_, i, rec.crc32, expected);
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

        // 感度設定を読み出す（試合状態とは独立したキー "sens"）。
        // 既存のレコードスキーマを壊さず、単一の uint8_t として管理する。
        // キーが存在しない場合（初回起動）はデフォルト値にフォールバックする。
        {
            uint8_t sensVal = prefs.getUChar(
                "sens", static_cast<uint8_t>(config::kDefaultSensitivityIndex));
            sensitivityIndex_ = (sensVal < config::kSensitivityPresetCount)
                ? sensVal
                : static_cast<uint8_t>(config::kDefaultSensitivityIndex);
        }

        prefs.end();

        if (hasValid_) {
            Serial.printf("%s スロット %d から復元 (sequence=%u)\n",
                          logTag_, currentSlot_, currentSeq_);
        } else {
            Serial.printf("%s 有効なレコードなし（初回起動または全スロット不正）\r\n",
                          logTag_);
        }

        return true;
    }

    /// 次のスロットに試合状態を書き込む。
    /// sequence をインクリメントし、CRC を計算して格納する。
    /// @return 書き込み成功時 true。
    bool save(const StateT& state) {
        if (!initialized_) {
            return false;
        }

        // 次のスロットを決定する（ローテーション）
        int nextSlot = (currentSlot_ + 1) % kSlotCount;
        uint32_t nextSeq = currentSeq_ + 1;

        // レコードを構築する
        PersistentRecord rec{};
        rec.magic = magic_;
        rec.schemaVersion = schemaVersion_;
        rec.payloadSize = static_cast<uint16_t>(sizeof(StateT));
        rec.sequence = nextSeq;
        rec.state = state;
        rec.crc32 = calcCrc32(rec);

        // NVS に書き込む
        Preferences prefs;
        if (!prefs.begin(nvsNamespace_, false)) {
            Serial.printf("%s save: NVS を開けませんでした\r\n", logTag_);
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
            // 失敗ログの形式は旧実装の区別をそのまま引き継ぐ
            if (detailedSaveFailureLog_) {
                Serial.printf("%s save: スロット %d への書き込み失敗 "
                              "(written=%u, expected=%u, recSize=%u)\n",
                              logTag_, nextSlot, static_cast<unsigned>(written),
                              static_cast<unsigned>(sizeof(rec)),
                              static_cast<unsigned>(sizeof(rec)));
            } else {
                Serial.printf("%s save: スロット %d への書き込み失敗 (%u bytes)\n",
                              logTag_, nextSlot, static_cast<unsigned>(written));
            }
            return false;
        }

        // 書き込み成功 — 内部状態を更新する
        currentSlot_ = nextSlot;
        currentSeq_ = nextSeq;

        Serial.printf("%s スロット %d に保存 (sequence=%u)\n",
                      logTag_, currentSlot_, currentSeq_);
        return true;
    }

    /// 有効な復元データがあるか。
    bool hasValidState() const {
        return hasValid_;
    }

    /// 復元された試合状態への参照。
    /// hasValidState() が true のときのみ有効。
    const StateT& loadedState() const {
        // hasValidState() が false のときに呼ぶのは誤用。
        // デバッグビルドでのみ検出し、リリースビルドでは assert が除去されるため
        // 動作変更やクラッシュを招かない。
        assert(hasValid_ && "loadedState() は hasValidState() が true のときのみ呼ぶこと");
        return loadedState_;
    }

    /// 感度プリセットのインデックスを NVS に保存する。
    /// 試合状態とは別キー "sens" で管理し、既存スキーマを壊さない。
    /// @return 書き込み成功時 true。
    bool saveSensitivity(uint8_t index) {
        if (!initialized_) {
            return false;
        }

        Preferences prefs;
        if (!prefs.begin(nvsNamespace_, false)) {
            Serial.printf("%s saveSensitivity: NVS を開けませんでした\r\n", logTag_);
            prefs.end();
            return false;
        }

        size_t written = prefs.putUChar("sens", index);
        prefs.end();

        if (written == 0) {
            Serial.printf("%s saveSensitivity: 書き込み失敗\r\n", logTag_);
            return false;
        }

        sensitivityIndex_ = index;
        Serial.printf("%s 感度プリセット %u を保存\n",
                      logTag_, static_cast<unsigned>(index));
        return true;
    }

    /// NVS から読み出した感度プリセットのインデックスを返す。
    /// begin() で読み出し済み。値が存在しない場合はデフォルト値（1 = 10 ライフ/周）。
    uint8_t loadedSensitivity() const {
        return sensitivityIndex_;
    }

private:
    /// NVS 永続化レコード。
    /// docs/08-persistence.md の PersistentRecord 設計に準拠する。
    /// __attribute__((packed)) は使わず自然アライメントのまま。
    struct PersistentRecord {
        uint32_t magic;
        uint16_t schemaVersion;
        uint16_t payloadSize;
        uint32_t sequence;
        StateT state;
        uint32_t crc32;
    };

    // コンパイル時にサイズが固定であることを保証する
    static_assert(sizeof(PersistentRecord) > 0,
                  "PersistentRecord のサイズが 0 になっている");

    // NVS 単一エントリ上限（約 4000 バイト）を超えていないことを保証する
    static_assert(sizeof(PersistentRecord) <= 4000,
                  "PersistentRecord が NVS 単一エントリ上限 (4000 bytes) を超えている");

    static constexpr int kSlotCount = 16;

    /// CRC-32 を計算する（レコード先頭から crc32 フィールド直前まで）。
    static uint32_t calcCrc32(const PersistentRecord& rec) {
        // crc32 フィールド直前までのバイト数を計算する。
        // PersistentRecord のレイアウト: magic, schemaVersion, payloadSize, sequence, state, crc32
        // crc32 より前の全バイトが対象。
        const size_t crcOffset = offsetof(PersistentRecord, crc32);
        return esp_crc32_le(0, reinterpret_cast<const uint8_t*>(&rec), crcOffset);
    }

    /// スロット番号からキー文字列を生成する（"s00"〜"s15"）。
    static void slotKey(int slot, char* buf) {
        // "s00"〜"s15" の 3 文字 + null 終端
        snprintf(buf, 4, "s%02d", slot);
    }

    // --- バリアント固有設定（コンストラクタで注入） ---
    const char* nvsNamespace_;         // NVS namespace 名（"lifectr" / "edh" 等）
    uint32_t magic_;                   // レコード先頭のマジック値
    uint16_t schemaVersion_;           // スキーマバージョン
    const char* logTag_;               // シリアルログの接頭辞
    bool detailedSaveFailureLog_;      // save() 失敗ログの形式（true = 詳細形式）

    // --- 実行時状態 ---
    bool initialized_ = false;   // begin() が成功したか
    bool hasValid_    = false;   // 有効なレコードが見つかったか
    int  currentSlot_ = -1;      // 最新レコードのスロット番号（-1 = なし）
    uint32_t currentSeq_ = 0;    // 最新レコードの sequence
    StateT loadedState_{};
    uint8_t sensitivityIndex_ = 1;  // デフォルト: 10 ライフ/周（config::kDefaultSensitivityIndex）
};

}  // namespace counter::infra
