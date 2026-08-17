# m5s-sw-life-counter

M5Stack StopWatch Dev Kit (ESP32-S3) 上で動作する、Flesh and Blood (FaB) TCG 向け 1 対 1 ライフカウンター。
円形 AMOLED ディスプレイの外周をスライドしてライフを増減する操作体系を採用し、無線通信を使わない Tournament Mode を標準とする。

## ステータス

**設計フェーズ** -- 設計ドキュメントを整備中。実装コードは未着手である。

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

> **前提条件**: 本リポジトリは設計フェーズであり、実装コードは未着手である。以下は環境構築を終えた後のコマンド例である。初めての場合は [docs/02-dev-environment.md](docs/02-dev-environment.md) の手順から始めること。

1. USB-C ケーブルで Mac に接続し、ポートを確認する:

```bash
ls /dev/cu.usbmodem*
```

2. ファームウェアをビルドして書き込み、シリアルモニタを起動する:

```bash
pio run -t upload -t monitor
```

> **注意**: 書き込み完了後、デバイスが自動起動しない場合がある（`Hard resetting via RTS pin` だけでは起動しないことがある）。その場合は電源ボタンを短押ししてリセットすること。

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

**読み進め方**:

- はじめて触る人: 00 → 02 → 03 の順に読むと、概要を把握してすぐ開発を始められる
- 設計を追う人: 04 → 05 → 06 → 07 → 08 の順に読むと、要件からアーキテクチャまで一貫して理解できる
- 実装計画を知りたい人: 12 を参照

## 対象ハードウェア

- **製品名**: M5Stack StopWatch Dev Kit (ESP32-S3)
- **SKU**: C152
- **公式ドキュメント**: https://docs.m5stack.com/en/core/StopWatch

## 技術スタック

- **MVP**: PlatformIO + Arduino Framework + M5Unified + M5GFX
- **将来**: ESP-IDF + NimBLE + LittleFS への移行を検討（省電力制御・BLE 連携の強化時）

## ライセンス

MIT -- [LICENSE](LICENSE) を参照。
