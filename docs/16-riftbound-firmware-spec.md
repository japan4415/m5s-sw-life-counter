# Riftbound（1v1 得点カウンター）ファームウェア仕様

> **ステータス: 実装済み・実機検証待ち**
> ビルド env `m5stack-stopwatch-riftbound` で実装済み。ホストテスト 37 件全通過（うち Riftbound 新規 37 件: ドメイン 18 / 画面状態 19）。3 ファームウェア env のビルド成功を確認済み。**実機検証は未実施**であり、描画の視認性やテーマ色の見え方は実機調整前提である。
> 本仕様書は設計判断の記録として維持する。

M5Stack StopWatch Dev Kit 上で動作する **Riftbound**（League of Legends TCG, Riot Games / UVS Games）Duel（1 対 1）向け得点カウンター **for Riftbound** の仕様を定義する。

**関連ドキュメント**: [./01-hardware.md](./01-hardware.md) | [./04-requirements.md](./04-requirements.md) | [./05-ui-ux.md](./05-ui-ux.md) | [./06-domain-model.md](./06-domain-model.md) | [./08-persistence.md](./08-persistence.md) | [./15-edh-firmware-spec.md](./15-edh-firmware-spec.md)

---

## 目的とスコープ

### 目的

Riftbound は**ライフ制ではなく得点制**のトレーディングカードゲームである。プレイヤーはバトルフィールドをユニットで征服・保持することで得点を獲得し、所定の勝利点に到達すると勝利する。本ファームウェアは各プレイヤーの**得点**を試合中に追跡する。現行の **for FaB** / **for MTG EDH** ファームウェアとは**別ファームウェア**として提供し、同一の M5Stack StopWatch Dev Kit ハードウェアに書き分ける。

カウントする対象は**得点のみ**である。ルール裁定はプレイヤーが行う。本機はあくまで記録補助であり、勝利点到達表示は目安にすぎない。

### Riftbound のルール要件（出典: 公式 How to Play / Core Rules）

| 項目 | 内容 |
|------|------|
| 得点の獲得方法 | バトルフィールドを征服で **+1 点**、ターン開始時に保持していれば **+1 点**（1 バトルフィールドにつき 1 ターン 1 点まで） |
| 勝利点 | **Duel（1v1）= 8 点**。3〜4 人 FFA も 8 点、2v2 は 11 点 |
| 勝利点の制約 | 8 点目（最終点）は、バトルフィールド保持中、または全バトルフィールド制圧のターンにしか獲得できない |

### スコープ

本ファームウェアの v1 では **Duel（1v1）のみ**を対象とする。上下 2 分割レイアウト（for FaB 版と同一）を流用する。

### スコープ外

以下の機能は本仕様の対象外とする。いずれも将来検討とする。

| 機能 | 除外理由 |
|------|---------|
| 3 人 / 4 人戦（FFA）・2v2 チーム戦 | 画面レイアウトが 1v1 専用の上下 2 分割設計のため。EDH 版の 4 分割レイアウト流用は将来検討 |
| 勝利点の変更（2v2 = 11 点など） | v1 では Duel の 8 点固定。`MatchState::victoryScore` をフィールドとして保持しており、将来の拡張余地は確保済み |
| バトルフィールドの個別管理 | どのバトルフィールドを制圧しているかの記録は本機の範囲外 |
| ユニットのダメージ・Might 管理 | ユニット単位の戦闘記録は本機の範囲外 |
| ターン・時間管理 | ライフカウンターの本質的機能ではない |

---

## ゲームルール要件

### 初期得点

Riftbound の得点は**常に 0 から開始**する。for FaB 版のような開始ライフ設定（Set Life）は存在せず、メニューからも削除した。Setup 画面は試合開始の確認のみに使う。

### 勝利点

勝利点は **8 固定**（Duel）。`MatchState::victoryScore` に設定される。

### 勝利点到達表示の位置づけ

勝利点（8 点目）には獲得条件の制約があるため、8 点到達は**必ずしも勝利を意味しない**。本機は到達時に警告表示（ゴールド色 + 枠線 + "!" マーク + 長めの振動）を行うが、これは「到達したことの通知」であり、勝利判定はプレイヤーが行う。for MTG EDH 版の敗北表示と同じ「目安」の思想である。

### クランプ

得点は **0 でクランプ**する（for FaB 版のライフ下限と同一）。**勝利点は上限ではない** -- 本機は記録補助であり、Undo や入力修正の余地を残すため、8 点を超える値も入力できる（for FaB 版の「開始ライフは上限ではない」と同じ思想）。表示側で到達を警告する。

---

## 画面レイアウト

for FaB 版と同一の上下 2 分割である。

```
        ┌─────────────────────┐
        │      対戦相手        │  ← 180 度回転表示
        │         0           │
        │                     │
        ├─────────────────────┤  ← 中央分割帯
        │                     │
        │         0           │
        │        自分          │  ← 通常向き表示
        └─────────────────────┘
```

### 回転描画

| プレイヤー | 位置 | 回転角 |
|-----------|------|--------|
| 対戦相手 | 上 | 180° |
| 自分 | 下 | 0°（無回転） |

### 半径方向の区分

現行 for FaB ファームウェアと同一の区分を踏襲する。

| 領域 | 半径範囲 (px) | 用途 |
|------|-------------|------|
| 外周操作リング | 165 以上（上限なし） | 得点の増減スライド |
| 内側領域 | 165 未満 | 操作なし（Riftbound ではビュー切替もない） |
| キャンセル領域 | 145 未満（設計値） | スライド中のキャンセル判定 |

---

## 操作体系

### 操作一覧

| 操作 | 動作 |
|------|------|
| 外周スライド（上半分、時計回り / 反時計回り） | 対戦相手の得点 +1 / -1 |
| 外周スライド（下半分、時計回り / 反時計回り） | 自分の得点 +1 / -1 |
| 感度・一周あたり変動量 5 / 10 / 20 | for FaB 版と同一実装を共用（既定 10） |
| スライド中の中央引き込み | キャンセル |
| KEYA 短押し | Undo（全履歴共通を 1 手戻す） |
| KEYB 短押し | タッチロック切替 |
| KEYA + KEYB 長押し（1 秒） | ゲームメニュー |
| Setup 画面で KEYB 長押し（1 秒） | 試合開始 |

### for FaB 版との操作差分

- **Setup 画面ではタッチを受け付けない**。for FaB 版は Setup で外周スライドにより開始ライフを調整するが、Riftbound の得点は常に 0 から始まるため調整する値が存在しない。
- 内側タップによるビュー切替は存在しない（EDH 版の統率者ダメージビューに相当する機能がない）。

---

## 画面状態機械

for FaB 版の画面状態を踏襲する。

### 画面状態

```
SETUP ── B(hold) 1秒 ──→ ACTIVE
                            ↕ A+B(hold) 1秒
                           MENU
           ┌────────┬──────┼──────────┬───────┐
         Resume  History  Sensitivity  Rematch  About
           ↓       ↓       ↓            ↓        ↓
         ACTIVE  HISTORY  SENSITIVITY  (確認待ち) ABOUT
                   ↓         ↓          B(hold)    ↓
              B短押し→ MENU  B短押し→ MENU   確定→ACTIVE  B短押し→ MENU
```

画面状態は for FaB 版と同一の **SETUP / ACTIVE / MENU / HISTORY / ABOUT / SENSITIVITY** の 6 つである。

### ゲームメニュー

for FaB 版の 6 項目から **Set Life を除く 5 項目**とする。

| メニュー | 動作 | 備考 |
|---------|------|------|
| Resume | ゲームに戻る | |
| History | 得点変更履歴を表示 | 各エントリは対象・変化前後の値・変化量を 1 行で表示（例: `TOP 0>3 (+3)`） |
| Sensitivity | 一周あたりの得点変動量を変更 | 5 / 10 / 20。for FaB 版と同一実装を共用 |
| Rematch | 得点 0 で試合をやり直す | B(hold) 長押し確認を要求 |
| About | ファームウェアバージョン表示 | |

**Set Life を削除した理由**: 得点は常に 0 から始まるため開始値を設定する項目が存在しない。共通の `MenuNav`（FaB / EDH との enum 互換で 6 項目固定）を流用するため、項目の削除ではなく**カーソル循環で SetLife をスキップする**方式で実装した（`RiftboundScreenState::onNext()`）。表示側も SetLife を除く 5 項目を描画する。

---

## ドメインモデル

`lib/riftbound_core/` に実装した。namespace は `counter::riftbound` である。

```cpp
namespace counter::riftbound {

constexpr uint8_t kPlayerCount = 2;
constexpr uint8_t kDefaultVictoryScore = 8;

struct PlayerState {
    uint32_t score;  // 0 でクランプ。勝利点は上限ではない
};

struct MatchState {
    uint16_t schemaVersion;
    PlayerState players[kPlayerCount];
    uint8_t  victoryScore;   // Duel は 8。将来の 2v2 = 11 拡張に備えたフィールド
    bool     active;
    bool     touchLocked;
    uint32_t nextSequence;
    counter::domain::RingBuffer<ScoreChange, 64> history;
};

struct ScoreChange {
    uint32_t sequence;
    PlayerId player;
    int32_t  requestedDelta;  // 要求された変化量（スライド操作量）
    int32_t  appliedDelta;    // 実際に適用された変化量（クランプ後の差分）
    uint32_t before;
    uint32_t after;
    uint32_t uptimeMs;
};

}
```

### フィールド補足

| フィールド | 説明 |
|-----------|------|
| `MatchState::victoryScore` | 勝利点。`startMatch()` で `kDefaultVictoryScore`（8）に設定し、`rematch()` でも維持する。2v2（11 点）対応の将来拡張に備えフィールド化している |
| `ScoreChange::appliedDelta` | クランプ後の実際の変化量。例: 得点 2 で -5 を操作した場合 appliedDelta = -2。履歴表示に使う |

### ドメイン操作

| 操作 | 関数 | 意味論 |
|------|------|--------|
| 得点変更 | `applyScoreChange()` | 0 でクランプし履歴に積む。requestedDelta == 0 は履歴を積まない |
| Undo | `undoLast()` | 差分の逆適用ではなく `before` 値への直接復元（for FaB 版と同一方針）。クランプが介在しても一意に正しく巻き戻る |
| 試合開始 | `startMatch()` | 得点 0/0、victoryScore 8、履歴クリア、sequence 0。for FaB 版と異なり開始値の引数を持たない |
| やり直し | `rematch()` | 得点 0/0 に戻す。victoryScore は維持 |
| 到達判定 | `hasReachedVictory()` | `score >= victoryScore`。表示・振動の目安に使う |

### 共通資産の再利用

`lib/counter_core` に含まれる以下の共通資産を再利用する。

| 共通資産 | 内容 |
|---------|------|
| `counter::PlayerId` | Top / Bottom の 2 値 enum（1v1 でそのまま使える） |
| `RingBuffer<ScoreChange, 64>` | 履歴バッファ。`counter::domain::RingBuffer` を共用 |
| ジェスチャー判定 | `GestureState` の状態遷移、角度計算、角速度フィルタ |
| タッチゾーン | 外周リング / キャンセル領域の半径判定 |
| ボタン状態機械 | 短押し / 長押し / 同時押しの判定 |
| 画面遷移コア | `MenuNav`（合成・委譲）+ `Screen` / `MenuItem` / `ScreenAction` の共用 enum |
| NVS 永続化方式 | `NvsStateStore` クラステンプレート（16 スロットローテーション + CRC32） |
| 振動フィードバック | パルス制御 |
| 感度設定 | プリセット管理と NVS 永続化 |

---

## 永続化

for FaB / EDH 版の NVS 16 スロットローテーション + CRC32 方式を踏襲する（`NvsStateStore` テンプレート）。

| 項目 | 値 |
|------|-----|
| NVS namespace | `"rift"`（for FaB 版 `"lifectr"` / for MTG EDH 版 `"edh"` とは別 namespace） |
| magic | `0x52425353`（"RBSS"） |
| schemaVersion | 1（Riftbound 固有。for FaB / EDH 版とは独立） |
| スロット数 | 16（ローテーション） |
| 検証 | CRC32 |

for FaB / EDH 版のデータと干渉しない設計とする。永続化方式の詳細は [./08-persistence.md](./08-persistence.md) を参照。

---

## ビルド構成

PlatformIO の別ビルド env（`m5stack-stopwatch-riftbound`）で for FaB / for MTG EDH と書き分ける。1 つのリポジトリ内で 3 ファームウェアバリアントを管理する。

| バリアント | env | ソースセット |
|-----------|-----|-------------|
| for FaB | `m5stack-stopwatch` | `main.cpp` + FaB 固有ファイル（EDH / Riftbound を除外） |
| for MTG EDH | `m5stack-stopwatch-edh` | `main_edh.cpp` + EDH 固有ファイル（FaB / Riftbound を除外） |
| for Riftbound | `m5stack-stopwatch-riftbound` | `main_riftbound.cpp` + Riftbound 固有ファイル（FaB / EDH を除外） |

`lib_ignore` により、FaB / Riftbound env は `edh_core` を、FaB / EDH env は `riftbound_core` を取り込まない。

## Web Flasher

Web Flasher は `/riftbound/install` を追加し、`firmware-riftbound.bin` をリリースアセットから取得する。リリースワークフローは `firmware-riftbound.bin` をビルド・添付する。

## 警告表示の設計

| 項目 | 設計値 | 備考 |
|------|--------|------|
| 勝利点到達表示 | ゴールド `0xF6A0`（数字・枠線）+ 枠線 3px + "!" マーク | for FaB 版のライフ 0 警告（オレンジ `0xFB40`）と色で区別する。色覚差対応のため枠線と "!" を併用 |
| 到達時の振動 | 120 ms（`kVibLifeZeroMs` を共用） | 「強い警告」の意味階層を FaB 版のライフ 0 到達と共通化 |
| プレビュー中の到達判定 | 確定後の値（`score + previewDelta`）に対して判定 | 現在値ではなく確定後の値で警告する |

---

## 未確定事項・実機調整前提

| 項目 | 現在の設計値 / 状態 | 備考 |
|------|-------------------|------|
| テーマ色（ゴールド `0xF6A0`）の AMOLED での見え方 | 未検証 | 実機でオレンジ（差分表示）と区別できるか確認して調整する前提 |
| Setup 画面の "SCORE" ラベル配置 | 数字中心 y=30 / ラベル中心 y=90（設計値） | 視認性は実機で確認する |
| 長時間の実戦運用 | 未検証 | for FaB 版と同一の描画方式（部分再描画）であり性能リスクは低いが、実機での確認が必要 |

---

## 受入基準

上記の仕様を検証可能な形に落としたチェックリストを以下に定義する。

### 得点管理

- [ ] RB-AC-01: 2 プレイヤーの得点をそれぞれ独立して変更できる
- [ ] RB-AC-02: 試合開始時の得点が 0 / 0 で開始される
- [ ] RB-AC-03: 得点が 0 未満にならない
- [ ] RB-AC-04: 外周スライドでスライド開始位置（上 / 下）に応じたプレイヤーの得点が増減する
- [ ] RB-AC-05: 1 スライド全体を 1 件として履歴に記録し、ボタン 1 押しで正確に取り消せる

### 勝利点到達

- [ ] RB-AC-06: 得点が 8 に到達すると警告表示（ゴールド + 枠線 + "!"）になる
- [ ] RB-AC-07: 勝利点到達時により長い振動で警告される
- [ ] RB-AC-08: 警告表示中も Undo・得点増減・メニュー操作がすべて可能である

### 共通操作

- [ ] RB-AC-09: タッチロックが機能する
- [ ] RB-AC-10: Rematch には B(hold) 長押し確認が必要である
- [ ] RB-AC-11: 感度設定が機能し、NVS に永続化される

### 画面表示

- [ ] RB-AC-12: 上半分が 180 度回転で描画され、双方が自分の数値を正位置で読める

### 永続化

- [ ] RB-AC-13: NVS に保存が成功し、再起動後に復元される
- [ ] RB-AC-14: for FaB / EDH 版の NVS データと干渉しない

### テスト

- [x] RB-T-01: ドメインロジックのホストテストが通過する（18 件）
- [x] RB-T-02: 画面状態機械のホストテストが通過する（19 件）
- [x] RB-T-03: 3 ファームウェア env がビルド成功する
