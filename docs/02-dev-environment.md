# 開発環境セットアップ

M5Stack StopWatch Dev Kit (ESP32-S3) を macOS 上で開発するための環境構築手順を記述する。本ドキュメントの目標は「箱から出した StopWatch を USB で Mac に繋いで、最初の 1 行が画面に出るまで」を迷わず到達させることである。

**関連ドキュメント**: [ハードウェア仕様](./01-hardware.md) | [開発ワークフロー](./03-dev-workflow.md)

---

## 前提条件

| 項目 | 内容 |
|------|------|
| ホスト OS | macOS（Darwin 24.x / Apple Silicon 想定） |
| 対象デバイス | M5Stack StopWatch Dev Kit (ESP32-S3) SKU C152 |
| SoC | ESP32-S3R8（Xtensa LX7 dual-core, 240MHz） |
| Flash / PSRAM | 16MB / 8MB（Octal） |
| ディスプレイ | 1.75" 円形 AMOLED 468x468 px（実測値。公式仕様では 466x466） |
| USB 通信方式 | Native USB-CDC（USB-Serial 変換 IC 非搭載） |

---

## 開発方式の選択

初日のセットアップでは PlatformIO を選べばよい。以下の比較表は参考情報である。

本プロジェクトでは以下 4 つの開発方式を検討した。

| 方式 | バージョン固定 | CI 連携 | ホストテスト | ESP-IDF 移行 | 備考 |
|------|:---:|:---:|:---:|:---:|------|
| **PlatformIO + Arduino** | `platformio.ini` で完結 | 容易 | `pio test -e native` | 可能 | **本プロジェクトの第一候補** |
| Arduino IDE (arduino-cli) | Board Manager 依存 | やや困難 | 非標準 | 可能 | GUI 操作中心、手軽だがバージョン管理が弱い |
| ESP-IDF | `sdkconfig` + cmake | 容易 | 可能 | 不要（そのもの） | モバイル連携フェーズ以降の移行先候補 |
| UIFlow2 | Web IDE 依存 | 不可 | 不可 | 不可 | 動作確認・プロトタイピング用途 |

### 選定理由

**PlatformIO + Arduino Framework + M5Unified / M5GFX** を第一候補とする。理由は以下のとおりである。

1. **バージョン固定が `platformio.ini` で完結する** -- ライブラリ・プラットフォームのバージョンを 1 ファイルでピン留めでき、チームメンバー間の環境差異を排除できる
2. **CI・ホスト側ユニットテストとの相性が良い** -- `pio test -e native` によりドメインロジックを実機なしで検証できる
3. **後から ESP-IDF へ移行可能** -- モバイル連携フェーズで BLE や省電力制御が必要になった場合、ESP-IDF ベースへの段階的移行が可能である（公式に ESP-IDF ベースの StopWatch デモが存在する: [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)）

UIFlow2 は実機の初期動作確認やプロトタイピング用途として位置づける。

---

## ツールチェーンのインストール

### Homebrew（未導入の場合）

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### PlatformIO Core（CLI）

Homebrew でインストールする場合:

```bash
brew install platformio
```

uv でインストールする場合（**`--with pip` が必須**）:

```bash
uv tool install platformio --with pip
```

> **注意**: `uv tool install platformio` だけでは失敗する。PlatformIO が内部で `python -m pip` を呼んで esptool 等の依存を解決するため、pip の無い uv ツール環境では `MissingPackageManifestError` で落ちる。`--with pip` を必ず付けること。

インストール確認（確認済みバージョン: PlatformIO Core **6.1.19**）:

```bash
pio --version
```

### esptool（フラッシュ操作用）

```bash
brew install esptool
```

### arduino-cli（Arduino IDE 方式を併用する場合のみ）

```bash
brew install arduino-cli
```

### VS Code + PlatformIO IDE 拡張

1. [VS Code](https://code.visualstudio.com/) をインストールする
2. 拡張機能マーケットプレイスから **PlatformIO IDE** (`platformio.platformio-ide`) をインストールする

> **注記**: Homebrew / PlatformIO のコマンドは fish shell でも動作する。ポート名にグロブ（ワイルドカード）を含む例は、実際のポート名に置き換えて使用すること。

---

## リポジトリのクローンとプロジェクト初期化

### Step 1: リポジトリをクローンする

```bash
git clone <リポジトリ URL>
```

```bash
cd m5s-sw-life-counter
```

### Step 2: `platformio.ini` を作成する

本ドキュメントの「[`platformio.ini` の初期案](#platformioini-の初期案)」節の内容をプロジェクトルートに `platformio.ini` として保存する。

### Step 3: `src/` ディレクトリを作成する

```bash
mkdir -p src
```

### Step 4: 動作確認用スケッチを配置する

「[動作確認用の最小スケッチ](#動作確認用の最小スケッチ)」節のコードを `src/main.cpp` として保存する。

### Step 5: ビルドを確認する

```bash
pio run
```

エラーなくビルドが完了すれば、プロジェクトの初期化は成功である。

---

## USB 接続の物理チェックリスト

M5Stack StopWatch は **Native USB-CDC**（ESP32-S3 内蔵 USB OTG）で通信する。USB-Serial 変換 IC（CH9102 / CP210x / CH340）は**搭載されていない**ため、専用ドライバのインストールは不要である。

以下のチェックリストを確認すること。

- [ ] **データ通信対応の USB-C ケーブルを使用している** -- 充電専用ケーブル（データ線なし）では PC から一切認識されない。**これが最も頻出する失敗原因である**
- [ ] **USB ハブを使わず Mac 本体の USB-C ポートに直接接続している** -- ハブ経由では認識が不安定になることがある
- [ ] ケーブルが両端ともしっかり差し込まれている

---

## ポートの特定手順

USB デバイスが macOS に正しく認識されているかを確認する手順を示す。

### Step 1: 接続前のポート一覧を確認する

```bash
ls /dev/cu.*
```

### Step 2: StopWatch を USB-C ケーブルで Mac に接続する

### Step 3: 接続後のポート一覧を確認し、差分を確認する

```bash
ls /dev/cu.*
```

### Step 4: 新たに出現したポートを特定する

Native USB-CDC の場合、ポート名は以下の形式になる。

```
/dev/cu.usbmodem14301
```

`usbmodem` に続く番号はランダムに割り当てられるため、環境ごとに異なる。

> **重要**: USB-Serial 変換 IC を使用するデバイスでは `/dev/cu.usbserial-*` や `/dev/cu.SLAB_*` のような名前になるが、StopWatch は Native USB-CDC のため `/dev/cu.usbmodem*` になる。ドライバのインストールは不要である（macOS 11 以降）。

---

## ダウンロードモードへの入り方

ファームウェアを書き込むには、デバイスをダウンロードモードに入れる必要がある。

### 通常のダウンロードモード

1. 電源ボタンを約 2 秒長押しする
2. 緑色の LED が点灯したらボタンを離す
3. デバイスが書き込み待機状態になる

### 強制ブートローダーモード（ブートループ・文鎮化時）

1. Boot ボタン（GPIO0）を押したまま保持する
2. その状態で USB-C ケーブルを抜き差しする
3. デバイスが強制的にブートローダーモードに入る

> **要実機確認**: Boot ボタン（GPIO0）の物理的な位置は公式ドキュメントに明記されていない。KEYA（GPIO2）・KEYB（GPIO1）とは別の専用ボタンである可能性がある。実機で確認すること。

> **電源ボタンの操作**（出典: [公式ドキュメント](https://docs.m5stack.com/en/core/StopWatch)）: 短押し 1 回で電源 ON/リセット、素早く 2 回押しで電源 OFF、約 2 秒長押し（緑 LED 点灯まで）でダウンロードモードに入る。

出典: https://docs.m5stack.com/en/arduino/stopwatch/program

---

## `platformio.ini` の初期案

以下は本プロジェクト向けの `platformio.ini` 初期案である。**この内容をプロジェクトルートに `platformio.ini` として保存する**。一次資料の設定をベースに、各行の役割をコメントで示す。

> **注記**: `pio project init --board esp32s3box` を実行してプロジェクトの雛形を生成してから、この内容で上書きする方法でもよい。`board = esp32s3box` は[公式ドキュメント](https://docs.m5stack.com/en/core/StopWatch)が明示的に指定しているボード定義である。

```ini
[env:m5stack-stopwatch]
; ESP32-S3 向けプラットフォーム（バージョンをピン留め）
platform = espressif32 @ 6.12.0

; ボード定義（公式ドキュメント指定。StopWatch 専用の定義は存在しない）
board = esp32s3box

; Arduino Framework を使用
framework = arduino

; 16MB Flash 向けパーティションテーブル
board_build.partitions = default_16MB.csv

; Flash サイズの明示指定
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216

; PSRAM を Octal モード（OPI）で使用するためのメモリタイプ指定
board_build.arduino.memory_type = qio_opi

; シリアルモニタのボーレート
monitor_speed = 115200

; ビルドフラグ
build_flags =
    -DESP32S3                        ; ESP32-S3 であることを明示
    -DBOARD_HAS_PSRAM                ; PSRAM 搭載ボードであることを宣言
    -DCORE_DEBUG_LEVEL=5             ; デバッグログレベル（5=VERBOSE, 開発中のみ）
    -DARDUINO_USB_CDC_ON_BOOT=1      ; Native USB-CDC を有効にする（シリアル出力の鍵）
    -DARDUINO_USB_MODE=1             ; USB モードを CDC に設定（上記と対で必要）

; ライブラリ依存（バージョンをピン留め）
lib_deps =
    m5stack/M5Unified @ ^0.2.15      ; M5Stack 統合ライブラリ（StopWatch 対応は 0.2.15 以降。確認済み: 0.2.20）
    m5stack/M5GFX @ ^0.2.21          ; グラフィックスライブラリ（StopWatch 対応は 0.2.21 以降。確認済み: 0.2.27）
    ; 公式 platformio.ini には M5PM1（電源管理 IC）と M5IOE1（IO エキスパンダ）の
    ; ライブラリも含まれるが、M5.begin() が内部で初期化するため MVP では不要

; [env:m5stack-stopwatch] は実機ビルド用、[env:native] はホスト上でのユニットテスト用である

; ホストテスト用環境（pio test -e native で実行）
[env:native]
platform = native
build_flags =
    -std=c++17
    -DNATIVE_TEST           ; ハードウェア依存コードを切り替えるフラグ
```

### Native USB-CDC に関する重要事項

`-DARDUINO_USB_CDC_ON_BOOT=1` と `-DARDUINO_USB_MODE=1` の 2 つのフラグが Native USB-CDC を有効にする鍵である。これらが欠落すると、シリアル出力が一切行われず、`pio device monitor` に何も表示されない。

### `board = esp32s3box` について

PlatformIO に StopWatch 専用のボード定義は存在しない（`pio boards | grep -i stopwatch` で 0 件を確認済み）。[公式ドキュメント](https://docs.m5stack.com/en/core/StopWatch)が明示的に `board = esp32s3box` を指定しており、これは「流用でやむを得ず」ではなく**公式指定**である。

---

## Arduino IDE / arduino-cli を使う場合

PlatformIO ではなく Arduino IDE または arduino-cli を使用する場合の設定手順を示す。

### Board Manager URL の追加

Arduino IDE の設定、または `arduino-cli config` に以下の URL を追加する。

```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

arduino-cli の場合:

```bash
arduino-cli config add board_manager.additional_urls https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

### ボードパッケージのインストール

```bash
arduino-cli core update-index
```

```bash
arduino-cli core install m5stack:esp32
```

Board Manager のバージョンは **3.3.7 以上** を使用すること。

### ボードの選択

FQBN（Fully Qualified Board Name）の候補は以下の 2 つである。使用する Board Manager パッケージによって異なる。

| Board Manager パッケージ | FQBN | 備考 |
|---|---|---|
| M5Stack Board Manager (`m5stack:esp32`) | `m5stack:esp32:M5StopWatch` | M5Stack 公式パッケージ使用時 |
| Espressif Board Manager (`esp32:esp32`) | `esp32:esp32:esp32s3box` | 一次資料の arduino-cli 例で使用 |

> **要実機確認**: どちらの FQBN が正しいかは環境に依存する。以下のコマンドで実際のボード一覧を確認し、該当するものを使用すること。いずれかを断定的に選ばないこと。

```bash
arduino-cli board listall | grep -i stopwatch
```

```bash
arduino-cli board listall | grep -i esp32s3box
```

### コンパイルと書き込み

以下は M5Stack Board Manager を使用する場合の例である。FQBN は上記で確認した値に置き換えること。

```bash
arduino-cli compile --fqbn m5stack:esp32:M5StopWatch .
```

書き込み時は、`ls /dev/cu.usbmodem*` で確認したポート名を明示的に指定する。

```bash
arduino-cli upload -p /dev/cu.usbmodem<実際のポート番号> --fqbn m5stack:esp32:M5StopWatch .
```

---

## バージョン固定方針

開発環境の再現性を確保するため、以下のバージョンをピン留めする。

| コンポーネント | 最低バージョン | 確認済みバージョン | 固定方法 |
|---|---|---|---|
| M5Unified | >= 0.2.15 | 0.2.20 | `platformio.ini` の `lib_deps` |
| M5GFX | >= 0.2.21 | 0.2.27 | `platformio.ini` の `lib_deps` |
| espressif32 プラットフォーム | 6.12.0 | 6.12.0 | `platformio.ini` の `platform` |
| PlatformIO Core | -- | 6.1.19 | `brew install platformio` または `uv tool install platformio --with pip` |
| M5Stack Board Manager（Arduino 使用時） | >= 3.3.7 | -- | Arduino IDE / arduino-cli で手動管理 |

> **ビルドサイズ参考値**（M5Unified + M5GFX の最小構成スケッチ）: RAM 6.7%（21,920 / 327,680 bytes）、Flash 7.6%（498,965 / 6,553,600 bytes）

### 運用ルール

- `platformio.ini` の `lib_deps` と `platform` にはバージョンを必ず指定する
- **動作確認が取れた組み合わせは、そのまま git にコミットする** -- 動作する `platformio.ini` が常にリポジトリに存在する状態を維持する
- ライブラリやプラットフォームのバージョンを変更する場合は、単独のコミットにして変更理由をコミットメッセージに記録する
- `pio pkg update` による一括更新は避け、1 ライブラリずつ更新・動作確認する

---

## 動作確認用の最小スケッチ

> **実機検証済み**: このスケッチは Phase 0 で実機動作を確認済みである。シリアル出力で `Display: 468 x 468`、`Free PSRAM: 7947987 bytes` が得られている。

以下のコードを `src/main.cpp` として保存する。

```cpp
// src/main.cpp — Phase 0 疎通確認用の最小スケッチ（実機検証済み）
#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // Native USB-CDC の確立を待つ（シリアルログ先頭の取りこぼし防止）
  delay(1000);

  Serial.println("StopWatch boot OK");
  Serial.printf("Display: %d x %d\n", M5.Display.width(), M5.Display.height());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString("Hello, StopWatch!", M5.Display.width() / 2, M5.Display.height() / 2);
}

void loop() {
  M5.update();
}
```

このスケッチが正常に動作すれば、下記「[セットアップ完了の判定条件](#セットアップ完了の判定条件)」の 4 項目（ビルド成功・USB ポート認識・書き込み成功・シリアルモニタ出力）をすべて検証できる。

---

## セットアップ完了の判定条件

以下の 4 つの条件をすべて満たせば、開発環境のセットアップは完了である。

### 1. ビルドが通る

```bash
pio run
```

エラーなくビルドが完了すること。

### 2. USB ポートが見える

```bash
ls /dev/cu.usbmodem*
```

StopWatch のポートが一覧に表示されること。

### 3. ファームウェアの書き込みが成功する

```bash
pio run -t upload
```

書き込みが正常に完了すること。エラーが出る場合はダウンロードモードに手動で入ってから再試行する。書き込み完了後、デバイスが自動起動しない場合は**電源ボタンを短押し**してリセットすること。

### 4. シリアルモニタにブートログが出る

```bash
pio device monitor
```

ESP32-S3 のブートログ（ライブラリバージョン、Flash サイズ等）がシリアルモニタに表示されること。

> **注記**: Native USB-CDC では、デバイス起動直後のログが取りこぼされる場合がある。`pio device monitor` を先に起動した状態でデバイスをリセットするか、コード中で起動直後に短い待ち（`delay(1000)` 等）を入れると確認しやすい。

> **注意**: `pio device monitor` は TTY を要求するため、非対話環境（CI、SSH 経由のスクリプト等）では動作しない（`termios.error` が発生する）。その場合は pyserial で直接読む方法を使う（[開発ワークフロー](./03-dev-workflow.md#pyserial-による代替シリアルモニタ)を参照）。

> **注記**: macOS Ventura / Sequoia 環境での Native USB-CDC の安定性については、アップロード後にポートが変化して再接続が不安定になるケースがコミュニティで報告されている。問題が発生した場合は [トラブルシューティング](./03-dev-workflow.md#トラブルシューティング) を参照すること。

---

## 参考リンク

- M5Stack StopWatch 製品ページ: https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3
- M5Stack StopWatch 公式ドキュメント: https://docs.m5stack.com/en/core/StopWatch
- M5Stack StopWatch プログラミングガイド: https://docs.m5stack.com/en/arduino/stopwatch/program
- M5Stack Arduino Board Manager: https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
- M5Unified GitHub: https://github.com/m5stack/M5Unified
- M5StopWatch-UserDemo (ESP-IDF): https://github.com/m5stack/M5StopWatch-UserDemo
- PlatformIO ドキュメント: https://docs.platformio.org/
- ESP32-S3 USB-CDC 概要: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-otg-console.html
