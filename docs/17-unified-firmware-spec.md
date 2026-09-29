# 統合ファームウェア仕様（FaB / MTG EDH / Riftbound 収録）

> **ステータス: 実装済み・実機検証待ち**
> ビルド env `m5stack-stopwatch` で 3 ゲームモードを 1 バイナリに収録した。ホストテスト 311 件全通過、アプリイメージ 566,601 バイト（アプリパーティションの 8.6%）、静的 DRAM 37,288 バイト（11.4%）を確認済み。**実機検証（ゲーム選択画面の操作、モード切替、モードを跨いだ NVS 復元）は未実施**であり、実機調整前提である。
> 本仕様書は設計判断の記録として維持する。各ゲームモードの詳細仕様は [./15-edh-firmware-spec.md](./15-edh-firmware-spec.md) / [./16-riftbound-firmware-spec.md](./16-riftbound-firmware-spec.md) を参照。

M5Stack StopWatch Dev Kit 上で動作する統合ファームウェア **for FaB / for MTG EDH / for Riftbound** のモード構成と切替機構を定義する。

**関連ドキュメント**: [./07-architecture.md](./07-architecture.md) | [./08-persistence.md](./08-persistence.md) | [./13-decisions.md](./13-decisions.md)（ADR-28） | [./15-edh-firmware-spec.md](./15-edh-firmware-spec.md) | [./16-riftbound-firmware-spec.md](./16-riftbound-firmware-spec.md)

---

## 目的とスコープ

### 目的

従来はゲーム別にファームウェアを書き分ける必要があった（for FaB / for MTG EDH / for Riftbound の 3 env）。単一バイナリに 3 ゲームモードを収録し、電源を切らずにモードを切り替えられるようにする。容量の実測調査（[ADR-28](./13-decisions.md)）により、統合アプリイメージは 566,601 バイトでアプリパーティション（6,553,600 バイト）の 8.6% に収まることを確認済みである。

### 収録モード

| モード | 対象ゲーム | 仕様書 | 対戦形式 |
|--------|-----------|--------|---------|
| for FaB | Flesh and Blood | docs/04-06（本体仕様） | 1 対 1 ライフ |
| for MTG EDH | Magic: The Gathering 統率者戦 | [./15](./15-edh-firmware-spec.md) | 4 人ライフ + 統率者ダメージ |
| for Riftbound | Riftbound（League of Legends TCG） | [./16](./16-riftbound-firmware-spec.md) | 1 対 1 得点（勝利点 8） |

### スコープ外

| 機能 | 除外理由 |
|------|---------|
| モード間でのデータ共有・統合履歴 | 各モードの対戦状態は独立して意味を持つため |
| 起動フローのスキン変更（ゲーム別の選択画面演出） | 選択画面は操作性の統一を優先し共通デザインとする |
| 特定ゲームのみのスリムバイナリ | 必要になった場合は別 env の復活で対応（パーティション表は変更不要） |

---

## 起動フローとモード決定

```
電源 ON
  ↓
ModeStore（NVS "sys"）から最終選択モードを読み出す
  ↓
そのモードに進行中の試合があるか？（各モードの NVS を走査）
  ├─ ある → そのモードを直接起動する（自動復元。確認ダイアログなし）
  └─ ない → ゲーム選択画面（SELECT GAME）を表示
                ↓ A: カーソル移動 / B: 決定
             選択されたモードを起動
                ↓
     モードの begin()（NVS 復元判定 + Setup 画面表示）
```

- カーソルの初期位置は最終選択モード（同じゲームを連続使用するケースが多いため）
- モード起動時に NVS へ選択を保存する。電源断後も選択が維持される
- 選択されていないモードのコントローラは `begin()` されないため、canvas（PSRAM 438 KB + 43 KB）は**駆動中モード分のみ確保**される。モード切替を繰り返して全モードの canvas が確保されても最大約 1.4 MB であり、空き PSRAM 約 7.5 MB に収まる

### ゲーム選択画面

```
        ┌─────────────────────┐
        │    SELECT GAME      │  y=150
        │                     │
        │   > FaB             │  y=200
        │     MTG EDH         │  y=236
        │     Riftbound       │  y=272
        │                     │
        │  A=Next  B=Select   │  y=350
        └─────────────────────┘
```

| 操作 | 動作 |
|------|------|
| KEYA（左・黄）短押し | カーソルを次のモードへ循環（最短パルスで通知） |
| KEYB（右・青）短押し | カーソル位置のモードを起動（確定パルス） |
| タッチ | 受け付けない（誤操作防止。ボタンのみで操作する方針（ADR-19）を継承） |

描画は M5.Display への直接全画面描画とし、カーソル移動・選択時のみ再描画する（選択画面は静的であり、ライフ操作中のような部分再描画の予算制約がない）。

---

## メニューからのモード切替（Switch Game）

各ゲームモードのゲームメニューに **Switch Game** を追加する（`MenuItem::SwitchGame`、`kMenuItemCount` は 6 → 7）。

### 操作フロー（Rematch と同じ 2 段階確認）

| 操作 | 動作 |
|------|------|
| メニューで Switch Game を選択（B 短押し） | 確認待ち表示（`> Switch Game? <` オレンジ + `Hold B to confirm`） |
| B 長押し（1 秒） | `ScreenAction::SwitchGame` を返し、`AppLauncher` がゲーム選択画面へ復帰 |
| A 短押し / A+B 長押し | 確認待ちを解除（Rematch と同一挙動） |

- 確定時に `Active` へ遷移しない点が Rematch と異なる（切替先は AppLauncher である）
- 切替時、駆動中モードの状態は既に NVS へ保存済み（確定・Undo のたびに save される）であり、追加の保存処理は不要
- 切替直前に振動が残っていた場合は `M5.Power.setVibration(0)` で停止してから選択画面へ移る

### 7 項目メニューのレイアウト

- 項目 y 範囲: 157 + 6×24 = **301**（末尾）。確認メッセージ（y=330）との隙間 15px
- 最長項目名 `> Switch Game? <` は約 192px。y=301（中心距離 67px）での円内利用可能幅は約 303px であり収まる
- メニューの最終項目への到達に必要な A 短押しが最大 5 回から 6 回に増加

### Riftbound モードの SetLife スキップとの共存

Riftbound モードでは得点制のため SetLife を選択対象から外している（[./16](./16-riftbound-firmware-spec.md)）。`RiftboundScreenState::onNext()` はカーソルが SetLife に乗った場合にさらに 1 つ進める。SwitchGame はこの循環の末尾に位置し、About → SwitchGame → Resume（SetLife をスキップ）の順に循環する。

---

## アーキテクチャ

### レイヤ構成

```
main.cpp（統合エントリポイント）
  └─ AppLauncher（モード決定・選択画面・ディスパッチ）
       ├─ IAppController（モード切替の境界。本設計で唯一のポリモーフィズム）
       │    ├─ AppController（for FaB）
       │    ├─ EdhAppController（for MTG EDH）
       │    └─ RiftboundAppController（for Riftbound）
       └─ ModeStore（NVS "sys" / 選択モードの永続化）
```

- `IAppController` は `begin() / update(nowMs) / consumeSwitchRequested()` の 3 メソッドを持つ。既存の合成 + 委譲方針（MenuNav 等）は「同一バイナリに複数実装が共存しない」前提によるものであり、共存が前提となるこの境界のみ仮想関数を使用する（[ADR-28](./13-decisions.md)）
- ゲーム切替要求はコントローラが内部フラグを立て、AppLauncher が消費型で取得する（コールバック・例外を使わない単方向の通知）
- 3 コントローラは `main.cpp` で静確保する（動的メモリ確保を行わない方針を継承）

### 共通資産とモード固有資産

| 区分 | 資産 |
|------|------|
| 共通 | `counter_core`（ジェスチャー / ボタン / MenuNav / RingBuffer / PlayerId / 振動定数 / 感度）、`render_framework.hpp`、`theme_common.hpp`、`NvsStateStore` テンプレート、M5Unified / M5GFX |
| モード固有 | 各モードの domain（life_service / edh_life_service / riftbound_score_service）、ScreenState、Renderer、Theme、StorageNvs エイリアス |

---

## 永続化

| 項目 | namespace | 内容 |
|------|-----------|------|
| モード選択 | `"sys"`（新設） | キー `"mode"`（uint8_t、GameMode 列挙値）。範囲外値・欠損時は FaB にフォールバック |
| for FaB の試合状態 | `"lifectr"` | 従来どおり（変更なし） |
| for MTG EDH の試合状態 | `"edh"` | 従来どおり（変更なし） |
| for Riftbound の試合状態 | `"rift"` | 従来どおり（変更なし） |

- モード切替・電源断でいずれのモードの対戦状態も失われない
- 起動時の自動復元は「最終選択モードに進行中の試合がある場合」に限定して従来どおり動作する。進行中の試合がなければゲーム選択画面となり、別モードを選べばそのモードの復元または Setup から始まる
- 感度設定は各モードの namespace 内 `"sens"` キーのまま（モード間で独立）

---

## ビルド・リリース構成

| 項目 | 統合後 |
|------|--------|
| ビルド env | `m5stack-stopwatch` の 1 本（`build_src_filter` / `lib_ignore` は廃止。未使用コードは --gc-sections で除去） |
| アプリイメージ | 566,601 バイト（app パーティション 6,553,600 バイトの 8.6%） |
| 静的 DRAM | 37,288 バイト（327,680 バイトの 11.4%） |
| リリースアセット | `firmware.bin` + 共通 3 点（bootloader / partitions / boot_app0）+ sha256sums。`firmware-edh.bin` / `firmware-riftbound.bin` は廃止 |
| Web Flasher | 全ページが `firmware.bin` を書き込む。ゲーム別ページは案内役として維持 |
| パーティション表 | `default_16MB.csv` のまま（app0 / app1 は変更なし。app1 は不使用のまま将来の OTA 用に残置） |

---

## 未確定事項・実機調整前提

| 項目 | 現在の設計値 / 状態 | 備考 |
|------|-------------------|------|
| ゲーム選択画面の視認性・配色 | theme_common 共通色（タイトル白 / カーソル項目シアン / 非選択グレー） | AMOLED 実機で確認して調整する前提 |
| 選択画面の項目 y 配置（200 / 236 / 272） | 設計値 | 3 項目の中央揃え感は実機で確認する |
| モード切替直後の描画 | 各 begin() が全画面を描き直す前提 | 切替時の一瞬の黒画面の長さを実機で確認する |
| 進行中試合のあるモードへの自動復元 | 実装済み・ホストテスト対象外（NVS 依存） | 実機で「FaB で対戦中に電源断 → 再起動で FaB 復帰」を確認する |

---

## 受入基準

### モード決定と切替

- [ ] UF-AC-01: 初回起動時にゲーム選択画面が表示される
- [ ] UF-AC-02: ゲーム選択画面で A 短押しによりカーソルが循環する
- [ ] UF-AC-03: ゲーム選択画面で B 短押しにより選択したモードが起動する
- [ ] UF-AC-04: 選択したモードが NVS に保存され、次回起動時のカーソル初期位置になる
- [ ] UF-AC-05: 進行中の試合があるモードへは確認なしで直接復帰する
- [ ] UF-AC-06: 各モードのメニューに Switch Game が表示される
- [ ] UF-AC-07: Switch Game は長押し確認後にゲーム選択画面へ戻る
- [ ] UF-AC-08: モード切替後、切り替え前モードの対戦状態が保持されている
- [ ] UF-AC-09: モード切替後、切り替え先モードの復元または Setup が開始される

### 共通操作

- [ ] UF-AC-10: Switch Game 以外の全メニュー項目が統合前と同一の挙動である
- [ ] UF-AC-11: Undo / タッチロック / 感度設定はモードごとに独立して機能する

### 永続化

- [ ] UF-AC-12: モード選択が NVS（"sys"）に保存され、電源断後も維持される
- [ ] UF-AC-13: 3 モードの NVS namespace（lifectr / edh / rift）が相互に干渉しない

### テスト

- [x] UF-T-01: ホストテスト 311 件全通過（MenuNav / FaB / EDH / Riftbound の SwitchGame 追加分を含む）
- [x] UF-T-02: 統合ファームウェアがビルド成功する（Flash 8.6% / RAM 11.4%）
