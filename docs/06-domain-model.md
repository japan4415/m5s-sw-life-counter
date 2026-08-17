# 06. ドメインモデル

FaB ライフカウンターのドメイン層設計を定義する。ドメイン層はハードウェアに一切依存しない純粋な C++ で構成し、ホスト PC 上で単体テストできる設計とする。

## 関連ドキュメント

- [要件定義](./04-requirements.md)
- [UI/UX 設計](./05-ui-ux.md)
- [永続化設計](./08-persistence.md)
- [テスト戦略](./11-testing.md)
- [開発ワークフロー](./03-dev-workflow.md)

---

## モデルの中心的な考え方

ドメイン層（`src/domain/`）は M5Unified、M5GFX、NVS などのハードウェア依存を一切持たない。標準 C++ ヘッダのみを使用し、ホスト PC（macOS / Linux）上の Native テスト環境でそのままコンパイル・テスト実行できる。

これにより以下を実現する:

- 実機を接続せずにライフ変更ロジック・Undo・履歴管理の単体テストを回せる
- CI 上での自動テストが可能になる
- ドメインロジックの変更と UI/ハードウェア層の変更を独立して進められる

この方針は [開発ワークフロー](./03-dev-workflow.md) の「実機不要な作業」に対応する。

---

## 型定義

### PlayerId

```cpp
enum class PlayerId : uint8_t { Top, Bottom };
```

| 値 | 意味 |
|---|---|
| `Top` | 画面上側のプレイヤー（対戦相手側） |
| `Bottom` | 画面下側のプレイヤー（自分側） |

上下の物理的な配置を表す識別子であり、ゲーム上の先攻・後攻とは独立している。

### PlayerState

```cpp
struct PlayerState {
    uint32_t startingLife;
    uint32_t life;
};
```

| フィールド | 型 | 意味 |
|---|---|---|
| `startingLife` | `uint32_t` | ゲーム開始時に設定したライフ値。Rematch 時の初期値復元に使用する |
| `life` | `uint32_t` | 現在のライフ値 |

### LifeChange

```cpp
struct LifeChange {
    uint32_t sequence;
    PlayerId player;
    int32_t requestedDelta;
    int32_t appliedDelta;
    uint32_t before;
    uint32_t after;
    uint32_t uptimeMs;
};
```

| フィールド | 型 | 意味 |
|---|---|---|
| `sequence` | `uint32_t` | 単調増加する通し番号。履歴の順序を一意に定める |
| `player` | `PlayerId` | ライフ変更の対象プレイヤー |
| `requestedDelta` | `int32_t` | ユーザーが要求した差分（外周スライドで指定した値） |
| `appliedDelta` | `int32_t` | 実際に適用された差分（下限クランプ後の値） |
| `before` | `uint32_t` | 変更前のライフ値 |
| `after` | `uint32_t` | 変更後のライフ値 |
| `uptimeMs` | `uint32_t` | 変更時点のシステム稼働時間（ミリ秒） |

### MatchState

```cpp
struct MatchState {
    uint16_t schemaVersion;
    PlayerState players[2];
    bool active;
    bool touchLocked;
    uint32_t nextSequence;
    RingBuffer<LifeChange, 64> history;
};
```

| フィールド | 型 | 意味 |
|---|---|---|
| `schemaVersion` | `uint16_t` | 永続化スキーマのバージョン番号 |
| `players[2]` | `PlayerState[2]` | `players[0]` = Top、`players[1]` = Bottom |
| `active` | `bool` | ゲームが進行中であるかどうか |
| `touchLocked` | `bool` | タッチロック状態（物理ボタンは有効のまま） |
| `nextSequence` | `uint32_t` | 次の `LifeChange` に付与する sequence 番号 |
| `history` | `RingBuffer<LifeChange, 64>` | 直近 64 件のライフ変更履歴をリングバッファで保持する |

---

## ライフ変更のルール

この節はドメインモデルの最重要部分である。

### applyLifeChange の実装

```cpp
LifeChange applyLifeChange(
    PlayerState& player,
    PlayerId playerId,
    int32_t requestedDelta
) {
    const uint32_t before = player.life;
    int64_t candidate = static_cast<int64_t>(before) + static_cast<int64_t>(requestedDelta);
    if (candidate < 0) candidate = 0;
    if (candidate > UINT32_MAX) candidate = UINT32_MAX;
    player.life = static_cast<uint32_t>(candidate);
    return LifeChange{
        .player = playerId,
        .requestedDelta = requestedDelta,
        .appliedDelta = static_cast<int32_t>(player.life) - static_cast<int32_t>(before),
        .before = before,
        .after = player.life,
    };
}
```

### 基本ルール

1. **開始ライフは上限ではない** -- Flesh and Blood のルール上、ライフは開始値を超えて増加できる。`startingLife` は初期値および Rematch 用の参照値であり、上限として機能しない
2. **下限は 0** -- ライフは 0 未満にならない。`candidate < 0` の場合は 0 にクランプする
3. **`UINT32_MAX` 上限** -- 事実上到達しない値だが、オーバーフロー防止のために `UINT32_MAX` で上限クランプする

### requestedDelta と appliedDelta を分けて保持する理由

ユーザーが要求した差分（`requestedDelta`）と実際に適用された差分（`appliedDelta`）は異なる場合がある。特にライフが 0 付近のとき、下限クランプによって差が生じる。

**具体例**: ライフ 2 の状態で -5 の入力が行われた場合

| フィールド | 値 |
|---|---|
| `requestedDelta` | -5 |
| `appliedDelta` | -2 |
| `before` | 2 |
| `after` | 0 |

**Undo は +5 ではなく `before` = 2 へ戻す。** `appliedDelta` の逆数を加算するのではなく、`before` に記録された値へ直接復元する。これにより、クランプが発生した場合でも正確な復元が保証される。

### int64_t キャストによるオーバーフロー回避

`before`（`uint32_t`）と `requestedDelta`（`int32_t`）の加算は、直接行うと `UINT32_MAX` 近傍で算術オーバーフローが発生する可能性がある。`int64_t` にキャストしてから加算し、結果を範囲チェックすることでこれを回避している。

---

## Undo の意味論

### 履歴の単位

1 回の外周スライド全体を 1 件として履歴に登録する。外周スライド中に -1 が 4 回発生しても、4 件として記録するのではなく、最終的な差分（例: -4）を 1 件の `LifeChange` として記録する。

### Undo の動作

Undo は差分の逆適用ではなく、`before` への復元である。具体的には:

1. 履歴の最新エントリの `before` 値を取得する
2. 対象プレイヤーのライフを `before` に直接設定する
3. 履歴からそのエントリを取り除く

この方式により、クランプが発生した変更でも正確に元の状態へ戻せる。

### 履歴の容量

履歴はリングバッファ 64 件で管理する。65 件目が追加されると最も古いエントリが上書きされる。超過分は復元できない（Undo 不可）。

---

## ライフ 0 の扱い

ライフ 0 は独立した状態（ステート）にせず、`life == 0` から導出される表示状態とする。

- 数字を強調表示し、警告アイコンと強い振動で通知する
- **操作不能にしない** -- Undo、ライフ増加、メニュー操作はすべて可能である
- 自動リセットは行わない（Rematch は必ずメニューから手動で行う）
- ゲーム終了の判定はプレイヤーが行う（アプリは関知しない）

この設計は、FaB のルール上ライフ 0 でもゲームが即座に終了しない場合があること、およびライフ 0 到達時に即座にリセットされると誤操作時に復帰できなくなることを考慮している。

---

## ジェスチャーのドメイン表現

外周スライド操作のドメインモデルとして `GestureState` と `EdgeGesture` を定義する。

### GestureState

```cpp
enum class GestureState { Idle, Candidate, Active, Cancelled };
```

| 状態 | 意味 |
|---|---|
| `Idle` | タッチなし。入力待ち |
| `Candidate` | 外周リングに触れたが、まだ最低移動角（6 度）に達していない |
| `Active` | 最低移動角を超え、ライフ変更のプレビューを表示中 |
| `Cancelled` | 指が中央へ移動し（半径 145px 未満）、操作がキャンセルされた |

### EdgeGesture

```cpp
struct EdgeGesture {
    GestureState state;
    PlayerId player;
    float startAngle;
    float previousAngle;
    float accumulatedAngle;
    float startRadius;
    int32_t previewDelta;
    uint32_t originalLife;
    uint32_t startedAtMs;
};
```

| フィールド | 型 | 意味 |
|---|---|---|
| `state` | `GestureState` | 現在のジェスチャー状態 |
| `player` | `PlayerId` | 操作対象のプレイヤー（タッチ開始地点で決定し、操作中は固定） |
| `startAngle` | `float` | タッチ開始時の角度（ラジアン） |
| `previousAngle` | `float` | 前フレームの角度（差分計算用） |
| `accumulatedAngle` | `float` | 累積角度変化量（ラジアン） |
| `startRadius` | `float` | タッチ開始時の中心からの距離 |
| `previewDelta` | `int32_t` | 現在のプレビュー差分値 |
| `originalLife` | `uint32_t` | スライド開始時のライフ値（Undo 用の `before` 値となる） |
| `startedAtMs` | `uint32_t` | ジェスチャー開始時刻（ミリ秒） |

### commitGesture のスニペット

```cpp
void commitGesture() {
    if (gesture.previewDelta == 0) return;
    applyLifeChange(gesture.player, gesture.previewDelta);
    history.push({
        .player = gesture.player,
        .before = gesture.originalLife,
        .after = currentLife,
        .delta = actualDelta,
    });
    storage.scheduleSave();
}
```

`previewDelta == 0` の場合（外周に触れたが十分にスライドしなかった場合）、履歴を作らずに終了する。これにより、意味のない操作が履歴を消費することを防ぐ。

---

## 上下入れ替え（Swap Sides）

メニューから「Swap Sides」を選択すると、上下プレイヤーの情報を入れ替える。入れ替え対象は以下の通り:

- **現在のライフ** (`life`): 正しく入れ替わること
- **開始ライフ** (`startingLife`): 正しく入れ替わること（Rematch 時に各プレイヤーが正しい開始ライフで再開するため）

入れ替え後は永続化を行い、表示を即座に更新する。

---

## 不変条件（invariants）

以下の不変条件は実装とテストの契約として維持する:

1. **`life >= 0`** -- ライフは 0 未満にならない（`uint32_t` 型のため型レベルでも保証される）
2. **`appliedDelta == after - before`** -- 適用差分は変更前後のライフ差に一致する
3. **履歴は最大 64 件** -- リングバッファの容量を超えた場合、最も古いエントリが上書きされる
4. **`sequence` は単調増加** -- 各 `LifeChange` の `sequence` は前のエントリより必ず大きい
5. **`after == before + appliedDelta`** -- 変更後ライフは変更前ライフに適用差分を加えた値に等しい
6. **`0 <= life <= UINT32_MAX`** -- ライフ値の有効範囲
7. **`requestedDelta != 0` ならば履歴に記録される** -- ただし `previewDelta == 0` の場合はそもそも `commitGesture` が呼ばれない
8. **Undo 後のライフは `before` に一致する** -- 差分の逆適用ではなく、記録された `before` への復元
9. **操作対象プレイヤーはスライド開始地点で固定** -- スライド中に上下境界を越えても対象は変わらない
10. **Swap Sides 後、両プレイヤーの `life` と `startingLife` が正しく入れ替わっている**

---

## 将来拡張との関係

モバイル連携フェーズでは、現在の `MatchState` のスナップショットベースの管理に加えて、追記専用の `MatchEvent` 列を持つイベントソーシング設計へ発展する。各操作（ライフ変更、Undo、試合開始・終了）がイミュータブルなイベントとして記録され、スマートフォンアプリとの BLE 同期や試合記録の完全な監査ログとして機能する。

この拡張は MVP のドメインモデルに対して後方互換であり、既存の `LifeChange` がイベントの一種として包含される形で設計される。

詳細は [モバイル連携設計](./10-mobile-integration.md) を参照。
