# m5s-sw-life-counter

M5Stack StopWatch Dev Kit (ESP32-S3) 上で動作する、Flesh and Blood (FaB) TCG 向け 1 対 1 ライフカウンター。
円形 AMOLED ディスプレイの外周をスライドしてライフを増減する操作体系を採用し、無線通信を使わない Tournament Mode を標準とする。

同一ハードウェアでゲーム別の複数ファームウェアバリアントを提供する。現行リリース済みの実装は **for FaB** バリアントである。MTG 統率者戦（EDH）向けの **for MTG EDH** バリアントは[実装完了](docs/15-edh-firmware-spec.md)（実機検証待ち・未リリース）。

## ステータス

**ファームウェア v1.0.0 リリース済み** -- [Web Flasher](https://m5s-sw-life-counter.discord.jp/install) からブラウザ経由でインストールできる。

## 主要な特徴

- **外周スライド操作**: 円形画面の縁をなぞってライフを増減。時計回りで増加、反時計回りで減少
- **1 台で 2 プレイヤー**: 上半分が対戦相手、下半分が自分。180 度回転表示で双方から読める
- **FaB ルール準拠**: ライフは 0 未満にならず、開始値を超えた増加は許容。任意の初期ライフ値に対応
- **1 スライド = 1 履歴の Undo**: スライド全体を 1 件として記録し、ボタン 1 押しで正確に取り消せる
- **再起動後の自動復元**: 確定した操作は NVS に永続化され、電源断から復帰しても直前の状態を復元する
- **振動フィードバック（無音）**: 音声を一切使わず、ライフ変動ごとの短い振動でクリック感を提供する
- **Tournament Mode**: Wi-Fi / Bluetooth を初期化せず、大会規定に配慮した標準動作モード
- **将来のスマホ連携**: BLE 経由で Android / iPhone アプリと接続し、試合記録・統計を管理する拡張を計画

## クイックスタート

> **注意**: データ通信対応の USB-C ケーブルが必須である。充電専用ケーブルではデバイスを認識しない。

> **前提条件**: 以下は開発者向けの手順である。環境構築がまだの場合は [docs/02-dev-environment.md](docs/02-dev-environment.md) の手順から始めること。エンドユーザーは [Web Flasher](https://m5s-sw-life-counter.discord.jp/install) からインストールできる。

1. USB-C ケーブルで Mac に接続し、ポートを確認する:

```bash
ls /dev/cu.usbmodem*
```

2. ファームウェアをビルドして書き込み、シリアルモニタを起動する:

```bash
# for FaB（既定）
pio run -t upload -t monitor

# for MTG EDH（実機検証待ち・未リリース）
pio run -e m5stack-stopwatch-edh -t upload -t monitor
```

> **注意**: 書き込み完了後、デバイスが自動起動しない場合がある（`Hard resetting via RTS pin` だけでは起動しないことがある）。その場合は電源ボタンを短押ししてリセットすること。電源を切るときは電源ボタンを**素早く 2 回押し**する（長押しはダウンロードモードに入るため使わない）。

3. ポートが自動検出されない場合は明示的に指定する:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem<実際のポート番号>
```

詳細なセットアップ手順は [docs/02-dev-environment.md](docs/02-dev-environment.md) を参照。

## ドキュメント

| ファイル | 内容 |
|---|---|
| [docs/00-overview.md](docs/00-overview.md) | プロジェクト概要とスコープ |
| [docs/01-hardware.md](docs/01-hardware.md) | ハードウェア仕様 |
| [docs/02-dev-environment.md](docs/02-dev-environment.md) | 開発環境セットアップ（macOS + USB） |
| [docs/03-dev-workflow.md](docs/03-dev-workflow.md) | 開発ワークフローとトラブルシューティング |
| [docs/04-requirements.md](docs/04-requirements.md) | 要件定義と受入基準 |
| [docs/05-ui-ux.md](docs/05-ui-ux.md) | UI/UX 設計（外周スライド操作） |
| [docs/06-domain-model.md](docs/06-domain-model.md) | ドメインモデル |
| [docs/07-architecture.md](docs/07-architecture.md) | ソフトウェアアーキテクチャ |
| [docs/08-persistence.md](docs/08-persistence.md) | 永続化設計 |
| [docs/09-power-and-tournament.md](docs/09-power-and-tournament.md) | 電源管理と Tournament Mode |
| [docs/10-mobile-integration.md](docs/10-mobile-integration.md) | モバイル連携（将来フェーズ） |
| [docs/11-testing.md](docs/11-testing.md) | テスト設計 |
| [docs/12-roadmap.md](docs/12-roadmap.md) | 実装ロードマップ |
| [docs/13-decisions.md](docs/13-decisions.md) | 技術選定記録と未解決事項 |
| [docs/15-edh-firmware-spec.md](docs/15-edh-firmware-spec.md) | MTG EDH（統率者戦）ファームウェア仕様（実装完了・実機検証待ち） |

**読み進め方**:

- はじめて触る人: 00 → 02 → 03 の順に読むと、概要を把握してすぐ開発を始められる
- 設計を追う人: 04 → 05 → 06 → 07 → 08 の順に読むと、要件からアーキテクチャまで一貫して理解できる
- 実装計画を知りたい人: 12 を参照

## Web ページ

`web/` ディレクトリに Vite ベースの MPA（マルチページアプリケーション）を構成している。Cloudflare Workers + Assets で配信。

| パス | 内容 |
|------|------|
| `/` | 紹介ページ（プロダクト概要・各ページへの導線） |
| `/install` | ファームウェア書き込みウィザード（Web Serial + esptool-js） |
| `/guide` | 使い方ガイド（セットアップ・操作方法・トラブルシューティング） |
| `/features` | 機能一覧（全機能の詳細解説） |

### Cloudflare Workers Builds の設定

デプロイに必要な設定はすべてリポジトリルートの `wrangler.toml` に集約されている。Cloudflare ダッシュボードの Workers Builds では、プレビュー（PR ブランチ）ビルドと production（main）ビルドでルートディレクトリ設定が異なる場合がある。

| 設定項目 | プレビュー（PR ブランチ） | production（main） |
|---|---|---|
| ルートディレクトリ | `/`（既定値） | `/web` に変更されている場合がある |
| ビルドコマンド | 未設定（既定値） | `yarn install && yarn build` に設定されている場合がある |
| デプロイコマンド | `npx wrangler versions upload` | `npx wrangler deploy` |

`wrangler.toml` の `[build]` セクションにより、デプロイ前にビルドが自動実行される。`[build] command` は CWD がリポジトリルートでも `web/` でも動くように記述されているため、ダッシュボード側のルートディレクトリ設定に依存しない。

> **Yarn バージョンの固定**: `web/yarn.lock` は Yarn 1（classic）形式だが、Cloudflare Workers Builds のビルド環境には Yarn 4 系がインストールされている。Yarn 4 は旧形式の lockfile を自動移行しようとするが、immutable モードでは lockfile の変更が禁止されているためビルドが失敗する（`YN0028`）。これを回避するため、`wrangler.toml` のビルドコマンドでは `npx yarn@1.22.22` により Yarn 1 系をバージョン固定で呼び出している。`web/package.json` の `packageManager` フィールドでも同バージョンを指定しており、corepack 有効環境でのローカル開発でも Yarn 1 が使われる。

### 手動デプロイ

ローカルから手動でデプロイする場合、リポジトリルートで以下を実行する:

```bash
wrangler deploy
```

`wrangler.toml` の `[build]` セクションにより、ビルドも自動で実行される。

## 対象ハードウェア

- **製品名**: M5Stack StopWatch Dev Kit (ESP32-S3)
- **SKU**: C152
- **公式ドキュメント**: https://docs.m5stack.com/en/core/StopWatch

## 技術スタック

- **MVP**: PlatformIO + Arduino Framework + M5Unified + M5GFX
- **将来**: ESP-IDF + NimBLE + LittleFS への移行を検討（省電力制御・BLE 連携の強化時）

## ライセンス

MIT -- [LICENSE](LICENSE) を参照。
