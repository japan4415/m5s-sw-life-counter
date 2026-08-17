# モバイル連携

> **注意: 本ドキュメントは MVP スコープ外の将来フェーズ設計である。** ここに記載された内容は、MVP（M5Stack 単体でのライフカウンター）の完成後に着手する拡張機能の設計であり、現時点では実装対象ではない。MVP のアーキテクチャは [アーキテクチャ](./07-architecture.md) を参照。

**関連ドキュメント**: [概要](./00-overview.md) | [永続化](./08-persistence.md) | [ロードマップ](./12-roadmap.md)

---

## システム全体像

### 3 層構成

```
試合終了後のみHTTPS同期
┌────────────────┐  BLE  ┌────────────────────┐  HTTPS  ┌───────────────┐
│  M5Stack       │◀────▶│  Android / iPhone  │◀──────▶│  任意クラウド │
│  StopWatch     │       │  アプリ            │         │  API + DB     │
│                │       │                    │         │               │
│・ライフ表示    │       │・試合設定           │         │・バックアップ  │
│・入力処理      │       │・履歴閲覧           │         │・端末間同期   │
│・イベント原本  │       │・ローカルDB         │         │・共有         │
│・単独動作      │       │・イベント複製        │         │・集計         │
└────────────────┘       └────────────────────┘         └───────────────┘
   権威データ                  ローカルレプリカ               任意
```

### 責務分担

| コンポーネント | 責務 |
|-------------|------|
| M5Stack | ライフの現在値、操作履歴、試合イベントの原本を保持 |
| スマホアプリ | 試合設定、同期、履歴表示、結果入力、統計、共有 |
| クラウド | 任意のバックアップ、複数スマホ間同期、共有 URL |
| 通信 | M5Stack-スマホ間は BLE、スマホ-クラウド間は HTTPS |

---

## 設計の中核原則

**M5Stack が試合イベントの唯一の原本（権威データ）である。**

スマホはローカルレプリカであり、クラウドは任意のバックアップである。この原則により、以下の状況でも M5Stack 単独で試合を継続でき、後から全履歴を回収できる。

- スマホが未接続
- スマホアプリが停止
- Bluetooth 切断
- スマホアプリが強制終了

---

## 前提の検証課題

M5Stack StopWatch の製品ページでは無線仕様として Wi-Fi のみ明記されている。ただし、搭載 SoC の ESP32-S3 は BLE GATT および NimBLE をサポートしている。

> **Phase 0 中間結果（2026-08-17）**: ESP32-S3 (QFN56) rev v0.2 の実機が手元で動作することを確認済み。M5Unified 0.2.20 / M5GFX 0.2.27 でのビルド・書き込み・起動に成功している。ただし **BLE の動作確認はまだ実施していない**。

**BLE advertising・接続・画面描画・タッチの同時動作は実機検証で成立性を確認する必要がある。** ここが未確認である以上、本ドキュメントに記載されたモバイル連携フェーズ全体が条件付き設計である。

---

## 動作モード

| 項目 | Tournament Mode（標準） | Casual Mode |
|------|----------------------|-------------|
| ライフ変更 | M5Stack 本体からのみ | スマホからも修正可能 |
| スマホからの Undo | 禁止 | 可能 |
| M5Stack の Wi-Fi | 無効 | 無効 |
| スマホからクラウドへのアップロード | 試合終了後のみ | 試合中も許可可能 |
| 戦略メモ・確率・推奨プレイ・カード提案の表示 | しない | 表示可能 |
| 試合中のメモ入力 | 不可 | 可能 |
| ライブ統計表示 | 不可 | 可能 |
| M5Stack のライフ表示 | 双方から確認可能 | 双方から確認可能 |

**試合開始後はモード切り替え不可。**

---

## イベントソーシング

### イベント列の例

```
seq=1  MatchStarted  40 / 40
seq=2  LifeChanged   Top 40 → 37
seq=3  LifeChanged   Bottom 40 → 39
seq=4  LifeChanged   Top 37 → 32
seq=5  UndoApplied   Top 32 → 37, target=4
seq=6  LifeChanged   Top 37 → 35
seq=7  MatchFinished 35 / 39
```

### 型定義

```cpp
enum class MatchEventType : uint8_t {
    MatchStarted, LifeChanged, UndoApplied,
    LifeCorrected, MatchFinished, MatchCancelled, MetadataUpdated
};

enum class EventSource : uint8_t {
    EdgeGesture, PhysicalButton, MobileApp, SystemRecovery
};

struct MatchEvent {
    uint8_t schemaVersion;
    Uuid128 matchId;
    uint64_t sequence;
    Uuid128 eventId;
    MatchEventType type;
    EventSource source;
    uint64_t monotonicMs;
    int64_t rtcUnixMs;
    PlayerId player;
    int32_t requestedDelta;
    int32_t appliedDelta;
    uint32_t beforeLife;
    // ... 後のフィールド
};
```

### エンコード

BLE 上では JSON ではなく CBOR でエンコードする。CBOR は JSON より小さいため、BLE の MTU 制約に適している。概念的な JSON 表現を以下に示す。

```json
{
    "v": 1,
    "mid": "53ecb42d-...",
    "seq": 17,
    "eid": "d23316ab-...",
    "type": "life_changed",
    "player": "bottom",
    "before": 40,
    "after": 36,
    "delta": -4,
    "requested_delta": -4,
    "source": "edge_gesture",
    "mono_ms": 93214
}
```

### Undo の扱い

Undo はイベントを削除せず、`UndoApplied` イベントを**追記する**。これにより監査履歴を残す。

```
seq=17  LifeChanged  40 → 36
seq=18  UndoApplied  36 → 40, target_seq=17
```

### 外周スライドとイベントの対応

**外周スライド（Edge Gesture）1 回を 1 イベントとして保存する。** 1 ポイントずつ 4 イベントにする方式は採用しない。

---

## BLE 通信設計

### 役割

- M5Stack = GATT Peripheral / Server
- Android / iPhone = GATT Central / Client
- 最大 2 台に制限（接続 1: Recorder、接続 2: Observer）

### GATT サービス構成

```
Device Service: Device Info, Capabilities, Battery, Pairing State

Match Service:
  Match Snapshot     Read / Notify  現在の試合状態
  Event Stream       Notify         新規イベント
  Command            Write with response  コマンド送信
  Command Result     Indicate       確実な結果通知
  Bulk Transfer      Notify         過去イベントの分割転送
```

### コマンド一覧

`HELLO`, `PAIR_REQUEST`, `PAIR_CONFIRM`, `CLAIM_RECORDER`, `RELEASE_RECORDER`, `GET_SNAPSHOT`, `GET_EVENTS`, `ACK_EVENTS`, `START_MATCH`, `UPDATE_METADATA`, `FINISH_MATCH`, `SET_RESULT`, `SET_TIME`, `PING`

Casual Mode のみ追加: `ADJUST_LIFE`, `UNDO`

### 冪等性

すべてのコマンドに `command_id` を付与し、冪等性を確保する。

```json
{
    "command_id": "f9106ec1-...",
    "type": "finish_match",
    "match_id": "53ecb42d-...",
    "expected_revision": 31
}
```

### 分割転送

```cpp
struct ChunkHeader {
    uint32_t transferId;
    uint16_t chunkIndex;
    uint16_t chunkCount;
    uint16_t payloadLength;
};
```

通常イベントは 160 bytes 以内を目標とする。

### Notification と Indication の使い分け

| 用途 | 方式 | 特性 |
|------|------|------|
| Event Stream | Notification | 高速だが ACK なし |
| Command Result | Indication | GATT レベルで確認応答あり |

Notification の欠落は sequence による再同期で回復する。

---

## ペアリング

### 7 手順のフロー

1. M5Stack で A+B を長押し
2. 「Pair new phone」を選択
3. M5Stack に QR コードと 6 桁コードを表示
4. スマホアプリで QR コードを読み取る
5. BLE 接続
6. M5Stack 上で接続先名を確認
7. A ボタンで承認

### QR コード内容

```
fabcounter://pair
?device=<random-device-id>
&token=<2分間だけ有効な一時トークン>
&version=1
```

- **長期的な鍵を含めない。**
- トークンは 2 分間のみ有効。

### M5Stack 本体での承認を必須とする理由

ペアリングは物理的にデバイスを操作しているユーザーのみが承認できるようにする。これにより、BLE の通信範囲内にいる第三者が無断で接続することを防ぐ。

---

## 試合のライフサイクル

### 試合設定（スマホアプリで入力）

| 項目 | 必須/任意 |
|------|----------|
| フォーマット | 任意 |
| 自分のヒーロー | 任意 |
| 相手ヒーロー | 任意 |
| 自分の開始ライフ | 必須 |
| 相手の開始ライフ | 必須 |
| 先攻・後攻 | 任意 |
| 大会・店舗名 | 任意 |
| ラウンド番号 | 任意 |
| 対戦相手名またはニックネーム | 任意 |
| 使用デッキ | 任意 |
| Tournament / Casual Mode | 必須 |

### M5Stack 上の確認画面

```
CLASSIC CONSTRUCTED

Kayo         Nuu
40           40

HOLD TO START
```

`HOLD TO START` は長押し確認である。**スマホだけで勝手に開始状態へ移行しない。** 試合の開始は必ず M5Stack 本体での物理的な承認を要する。

### 試合中の処理フロー

ジェスチャー確定 → イベント生成 → Flash へ追記 → RAM 状態へ適用 → 画面更新 → BLE 通知

スマホ未接続でもステップ 1〜5 は通常通り実行される。BLE 通知のみスキップされる。

### 試合終了後

M5Stack に保存される内容:
- 終了時刻
- 最終ライフ
- イベント列
- 終了操作フラグ
- 結果未確定フラグ

スマホアプリでの補完:
- 結果（Win / Loss / Draw / No Result）
- 終了理由（Life 0 / Concession / Time / Judge decision / Other）
- 試合後メモ

---

## 同期ステートマシン

### 状態遷移

```
DISCONNECTED
→ DISCOVERING_SERVICES
→ AUTHENTICATING
→ NEGOTIATING_PROTOCOL
→ FETCHING_SNAPSHOT
→ FETCHING_MISSING_EVENTS
→ LIVE
→ DISCONNECTED
```

### 不一致時の処理

状態不一致時は **M5Stack を正**として不足イベントを再取得する。それでも一致しない場合は `SYNC_CONFLICT` 状態とし、スマホ側の値を無条件で M5Stack へ書き戻さない。

---

## 複数スマホ接続

### WriterLease

```cpp
struct WriterLease {
    InstallationId owner;
    uint64_t issuedAt;
    uint64_t expiresAt;
    uint64_t generation;
};
```

### 制約

- 書き込み可能なスマホは 1 台のみ。
- Writer の取得には M5Stack 本体での承認が必要。
- **ローカルの外周スライド操作は常にスマホ操作より優先される。**

---

## スマホアプリ構成

### 技術スタック

Kotlin Multiplatform（KMP）で共有するもの:
- イベントモデル
- CBOR エンコーダ/デコーダ
- 状態復元 Reducer
- 同期ステートマシン
- バリデーション
- ローカル DB スキーマ
- クラウド API クライアント
- 試合集計

ネイティブで実装するもの:
- Bluetooth
- バックグラウンドライフサイクル
- UI

### モジュール構成

```
mobile/
├── shared/
│   ├── domain/Match.kt, MatchEvent.kt, MatchReducer.kt, Rules.kt
│   ├── protocol/CborCodec.kt, Packet.kt, ProtocolVersion.kt
│   ├── sync/SyncStateMachine.kt, EventReconciler.kt, UploadQueue.kt
│   ├── database/MatchRepository.kt, migrations/
│   └── api/CloudClient.kt
├── androidApp/ble/, companion/, service/, ui/
└── iosApp/Bluetooth/, Lifecycle/, Views/
```

### アプリ画面一覧

- **Home**: 接続状態
- **Match Setup**: 試合設定
- **Live Record**: イベント受信状況
- **History**: 試合一覧
- **Match Detail**: ライフ推移グラフ、イベントタイムライン、試合後メモ、JSON/CSV 出力

### SQLite スキーマ

```sql
CREATE TABLE matches (
    match_id TEXT PRIMARY KEY,
    device_id TEXT NOT NULL,
    status TEXT NOT NULL,
    mode TEXT NOT NULL,
    format TEXT,
    started_at INTEGER,
    ended_at INTEGER,
    result TEXT,
    result_reason TEXT,
    player_hero TEXT,
    opponent_hero TEXT,
    player_starting_life INTEGER NOT NULL,
    opponent_starting_life INTEGER NOT NULL,
    event_name TEXT,
    round_name TEXT,
    opponent_alias TEXT,
    notes TEXT,
    revision INTEGER NOT NULL,
    sync_state TEXT NOT NULL
);

CREATE TABLE match_events (
    match_id TEXT NOT NULL,
    sequence INTEGER NOT NULL,
    event_id TEXT NOT NULL,
    event_type TEXT NOT NULL,
    event_cbor BLOB NOT NULL,
    event_hash BLOB,
    PRIMARY KEY (match_id, sequence)
);
```

---

## プラットフォーム固有のバックグラウンド制約

### iOS

- `Info.plist` に `NSBluetoothAlwaysUsageDescription` を記載
- `UIBackgroundModes: bluetooth-central` を設定
- `CBCentralManagerOptionRestoreIdentifierKey` で State Preservation and Restoration を有効化
- **ユーザーが強制終了した場合、Bluetooth イベントでアプリは再起動されない。** M5Stack 側でのイベント完全保持と再同期が設計の前提である。

### Android 12 以降

- 権限: `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`
- 位置情報不要の場合は `neverForLocation` を宣言
- 初期ペアリングは `CompanionDeviceManager` を使用
- 長時間接続は `CompanionDeviceService` または Foreground Service で維持

---

## クラウド設計（任意）

クラウドは必須ではない。ユーザーが任意で有効化する。

### API エンドポイント

```
POST   /v1/devices/register
PUT    /v1/matches/{match_id}
POST   /v1/matches/{match_id}/events:batch
POST   /v1/matches/{match_id}:finalize
GET    /v1/matches
GET    /v1/matches/{match_id}
POST   /v1/matches/{match_id}:share
DELETE /v1/matches/{match_id}
```

### 一意制約

- `UNIQUE(device_id, match_id, sequence)`
- `UNIQUE(event_id)`
- HTTP リクエストには `Idempotency-Key` を付与

### Tournament Mode での同期制約

Tournament Mode では以下のタイミングでのみクラウド同期を行う。

- 試合終了後
- ユーザーが同期ボタンを押したとき
- 試合画面を閉じた後

**M5Stack 自身はクラウドに接続しない。** クラウドとの通信はすべてスマホアプリ経由で行う。

---

## 改ざん検知（MVP 後の拡張）

### ハッシュチェーン

```
hash_0 = SHA-256(match_id || device_id)
hash_n = SHA-256(hash_(n-1) || canonical_cbor(event_n))
```

最終ハッシュを試合終了時に保存する。

### 署名による強化

M5Stack 固有鍵でハッシュチェーンに署名することで、改ざん検知をさらに強化できる。

---

## 障害時の挙動

| 障害 | 挙動 |
|------|------|
| スマホ切断 | M5Stack 単独で記録継続 |
| スマホアプリ強制終了 | M5Stack へ影響なし、次回起動時に再同期 |
| iPhone ロック | BLE は可能な範囲で継続、切断しても後追い同期 |
| M5Stack 再起動 | イベントログを再生して試合復元 |
| 操作中に電源断 | 確定前ジェスチャーだけ破棄、確定済みイベントを復元 |
| BLE イベント重複 | `event_id` と `sequence` で無視 |
| コマンド重複 | `command_id` で以前の結果を返す |
| 2 台から同時書き込み | Writer Lease のない側を拒否 |
| 時計がずれている | `sequence` を順序の正とする |
| ログ容量不足 | 新規試合開始を停止し、既存ログを保護 |

---

## ファームウェア内部構成の発展形

モバイル連携フェーズでは、ファームウェアを以下の 5 タスク構成へ発展させる。

```
┌────────────────────────────────┐
│ UI Task（画面、外周スライド、振動）│
├────────────────────────────────┤
│ Match Engine Task（唯一の状態変更主体）│
├────────────────────────────────┤
│ Event Store Task（LittleFS、CRC、スナップショット）│
├────────────────────────────────┤
│ BLE Task（GATT、接続、送受信、分割転送）│
├────────────────────────────────┤
│ Power / RTC Task（バッテリー、時刻、スリープ）│
└────────────────────────────────┘
```

コマンドキューによる直列化:

```
Touch/BLE → DomainCommand Queue → MatchEngine → EventStore → State更新 → UI/BLE通知
```

**このフェーズでは Arduino Framework から ESP-IDF + NimBLE + LittleFS + CBOR codec への移行が推奨される。** MVP のアーキテクチャは [アーキテクチャ](./07-architecture.md) を参照。

---

## リポジトリ構成の発展形

```
fab-life-counter/
├── firmware/
│   ├── main/
│   └── components/
│       ├── match_engine/
│       ├── event_store/
│       ├── ble_protocol/
│       ├── stopwatch_ui/
│       └── pairing/
├── protocol/
│   ├── protocol.cddl
│   ├── uuid.md
│   ├── state-machine.md
│   ├── compatibility.md
│   └── golden/event-v1.cbor, snapshot-v1.cbor
├── mobile/shared/, androidApp/, iosApp/
├── backend/api/, migrations/, openapi.yaml
├── simulator/
│   ├── virtual-counter/
│   └── fault-injector/
└── docs/
```

### simulator の意義

- **`virtual-counter/`**: M5Stack 実機なしでスマホアプリの開発を進めるためのシミュレータ。BLE GATT Peripheral をソフトウェアでエミュレートし、スマホアプリの接続・同期・イベント受信のテストを可能にする。
- **`fault-injector/`**: BLE 切断、遅延、パケットロス、電源断などの障害をシミュレートし、異常系の動作検証を行うためのツール。

---

## このフェーズの受入基準と未解決事項

### 受入基準（要点）

- スマホなしで M5Stack が完全動作すること
- すべてのライフ変更が永続化されること
- 30 分切断後に全イベントを回収できること
- 同一イベントの重複登録がないこと
- Undo 前のイベントが監査履歴として保持されること
- Android と iPhone で同一のイベント再生結果になること
- Tournament Mode でスマホからのライフ変更が不可であること
- Tournament Mode で試合中のインターネット通信が行われないこと

### 未検証項目

| 項目 | 状態 |
|------|------|
| BLE advertising・接続・画面描画・タッチの同時動作 | **Phase 0 で実機検証必須** |
| iOS 強制終了後の Bluetooth イベントによるアプリ再起動 | 仕様上不可とされる。M5Stack 側の保持で対処 |
| BLE 接続時の消費電力増加量 | 未測定。バッテリー目標への影響を評価する必要がある |
| LittleFS の電源断耐性 | 書き込み途中での電源断テストが必須 |
| 2 台同時接続時の BLE スループット | 未測定 |
