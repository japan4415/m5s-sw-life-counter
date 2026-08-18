// 描画層の実装。
//
// PSRAM 上の全画面 Canvas をバッファとして使い、
// 変更のあった部分矩形のみをディスプレイに転送する。
//
// なぜ全画面転送を避けるのか:
//   外周スライド中、ライフ表示は約 43 ms ごとに更新される
//   （感度 10 度/ライフ、スイープ角速度 233 度/秒の実測値に基づく）。
//   全画面 (468x468) 転送は 44.6 ms かかり、この予算を超過する。
//   数字領域 (180x120) の転送は約 4.5 ms で、十分な余裕がある。
//   (Phase 0 Step 8 実測、ADR-15 決定)

#include "ui/renderer.hpp"
#include "ui/theme.hpp"
#include "app_config.hpp"

namespace counter::ui {

// ============================================================
// begin — Canvas の確保
// ============================================================

void Renderer::begin() {
    // --- 全画面 Canvas を PSRAM 上に確保する ---
    // 468x468 x 2 bytes (RGB565) = 438,048 bytes。
    // Phase 0 Step 6 で 438,068 bytes (ヘッダ含む) の確保に成功し、
    // 空き 7,509,919 bytes であることを確認済み。
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    void* mainBuf = canvas_.createSprite(
        config::kDisplayWidth, config::kDisplayHeight);

    if (mainBuf == nullptr) {
        // PSRAM 確保失敗。フォールバックとして直接描画に切り替える。
        // 全画面バッファなしでも drawLife() の部分転送は lifeCanvas_ 経由で可能。
        Serial.println(
            "[Renderer] ERROR: PSRAM canvas allocation failed. "
            "Falling back to direct draw.");
        Serial.printf(
            "[Renderer]   Requested: %d x %d x 2 = %d bytes\n",
            config::kDisplayWidth, config::kDisplayHeight,
            config::kDisplayWidth * config::kDisplayHeight * 2);
        Serial.printf(
            "[Renderer]   Free PSRAM: %u bytes\n",
            static_cast<unsigned>(ESP.getFreePsram()));
        canvasReady_ = false;
    } else {
        canvasReady_ = true;
        Serial.printf(
            "[Renderer] Main canvas: %d x %d allocated in PSRAM. "
            "Free PSRAM: %u bytes\n",
            config::kDisplayWidth, config::kDisplayHeight,
            static_cast<unsigned>(ESP.getFreePsram()));
    }

    // --- 数字領域用テンプキャンバス ---
    // 180x120 x 2 = 43,200 bytes。
    // SRAM のほうが PSRAM より転送が速いため、まず SRAM を試す。
    // M5Canvas のコンストラクタは親指定時にデフォルトで PSRAM を使うため、
    // 明示的に SRAM を指定する必要がある。
    lifeCanvas_.setPsram(false);
    lifeCanvas_.setColorDepth(16);
    void* lifeBuf = lifeCanvas_.createSprite(
        theme::kLifeRegionW, theme::kLifeRegionH);

    if (lifeBuf == nullptr) {
        // SRAM 不足時は PSRAM で再試行する
        Serial.println(
            "[Renderer] WARN: Life canvas SRAM alloc failed, "
            "retrying in PSRAM...");
        lifeCanvas_.setPsram(true);
        lifeBuf = lifeCanvas_.createSprite(
            theme::kLifeRegionW, theme::kLifeRegionH);
        if (lifeBuf == nullptr) {
            Serial.println(
                "[Renderer] ERROR: Life canvas allocation failed "
                "(SRAM and PSRAM both).");
        }
    }
}

// ============================================================
// drawAll — 全画面描画
// ============================================================

void Renderer::drawAll(const domain::MatchState& state) {
    // Canvas が確保できていれば Canvas へ描画し最後に一括転送。
    // 確保に失敗している場合は Display へ直接描画する（フォールバック）。
    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    // 1. 背景を黒で塗りつぶす
    target->fillScreen(theme::kBgColor);

    // 2. 外周リング
    //    ロック中は暗転して操作無効を視覚的に伝える (docs/05-ui-ux.md 表示ルール)
    uint16_t ringColor = state.touchLocked
        ? theme::kRingLockedColor
        : theme::kRingNormalColor;
    drawRings(target, ringColor, ringColor);

    // drawHoldProgress(0) がリング復元に使うため、現在の色を追跡する
    ringTopColor_ = ringColor;
    ringBottomColor_ = ringColor;

    // 全画面転送で進捗弧が消えるため、次回 drawHoldProgress() は
    // トラックから描き直す必要がある
    lastHoldPercent_ = 0;

    // 3. 中央分割帯
    auto divY = static_cast<int32_t>(config::kCenterY)
              - theme::kDividerHeight / 2;
    target->fillRect(0, divY, config::kDisplayWidth,
                     theme::kDividerHeight, theme::kDividerColor);

    // 4. 両プレイヤーのライフ数字を lifeCanvas_ 経由で target に描画する
    uint32_t topLife =
        state.players[domain::toIndex(PlayerId::Top)].life;
    uint32_t bottomLife =
        state.players[domain::toIndex(PlayerId::Bottom)].life;

    // 下側プレイヤー（通常向き）
    renderLifeRegion(bottomLife, 0, false);
    lifeCanvas_.pushSprite(target, theme::kLifeBottomX, theme::kLifeBottomY);

    // 上側プレイヤー（180 度回転）
    renderLifeRegion(topLife, 0, true);
    lifeCanvas_.pushSprite(target, theme::kLifeTopX, theme::kLifeTopY);

    // 5. ロック中は鍵アイコンを表示する
    //    分割帯の上に描画する。drawAll() は全画面転送で完結するため、
    //    ここでは target への描画のみで良い。
    if (state.touchLocked) {
        drawLockIcon(target);
    }

    // 6. 全画面転送（起動時・リマッチ時のみ呼ぶ想定。44.6 ms かかる）
    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawLife — 数字領域の部分更新
// ============================================================

void Renderer::drawLife(const domain::MatchState& state,
                        PlayerId player, int32_t previewDelta) {
    bool isTop = (player == PlayerId::Top);
    uint32_t life = state.players[domain::toIndex(player)].life;
    int32_t rx = isTop ? theme::kLifeTopX : theme::kLifeBottomX;
    int32_t ry = isTop ? theme::kLifeTopY : theme::kLifeBottomY;

    // lifeCanvas_ にライフ情報を描画する
    renderLifeRegion(life, previewDelta, isTop);

    // 全画面 Canvas に反映して一貫性を保つ。
    // drawRingHighlight() がリング更新時に Canvas を参照するため、
    // 数字領域も最新状態にしておく必要がある。
    if (canvasReady_) {
        lifeCanvas_.pushSprite(&canvas_, rx, ry);
    }

    // Display に部分転送する（高速パス: 180x120 ≈ 4.5 ms）。
    // 全画面転送 (44.6 ms) ではなく数字領域のみを転送するため、
    // 43 ms の描画予算に対して十分な余裕がある。
    lifeCanvas_.pushSprite(&M5.Display, rx, ry);
}

// ============================================================
// drawRingHighlight — リングの強調/復帰
// ============================================================

void Renderer::drawRingHighlight(PlayerId player, bool on) {
    uint16_t topColor;
    uint16_t bottomColor;

    if (on) {
        // 操作中: 対象プレイヤー側を強調し、非対象側を暗くする
        if (player == PlayerId::Top) {
            topColor    = theme::kRingHighlightColor;
            bottomColor = theme::kRingDimColor;
        } else {
            topColor    = theme::kRingDimColor;
            bottomColor = theme::kRingHighlightColor;
        }
    } else {
        // 操作終了: 両方とも通常色に戻す
        topColor    = theme::kRingNormalColor;
        bottomColor = theme::kRingNormalColor;
    }

    // drawHoldProgress(0) がリング復元に使うため、現在の色を追跡する
    ringTopColor_ = topColor;
    ringBottomColor_ = bottomColor;

    // Canvas と Display の両方にリングを描画する。
    //
    // fillArc は描画対象の表面に直接ピクセルを書くため、
    // 全画面 pushSprite (44.6 ms) を回避しつつリング部分のみを更新できる。
    //
    // 数字領域はリング内周 (r=165) の内側に全角が収まるよう設計してあるため
    // (theme.hpp の数字領域コメント参照)、リングの fillArc が数字を
    // 上書きすることはない。
    if (canvasReady_) {
        drawRings(&canvas_, topColor, bottomColor);
    }
    drawRings(&M5.Display, topColor, bottomColor);
}

// ============================================================
// drawLockState — タッチロック表示の更新
// ============================================================

void Renderer::drawLockState(const domain::MatchState& state) {
    // 部分再描画のみで全画面転送 (44.6 ms) を回避する。
    // fillArc / fillRect は対象ピクセルのみを書き換え、
    // SPI バス上でも対象領域だけを転送する。
    //
    // なぜ全画面転送を避けるのか:
    //   全画面 (468x468) 転送は 44.6 ms かかる (Phase 0 Step 8 実測)。
    //   ロック切り替えは即座の視覚応答が期待される操作であり、
    //   全画面転送の遅延はユーザー体験を損なう。

    if (state.touchLocked) {
        // --- ロック表示を有効にする ---

        // drawHoldProgress(0) がリング復元に使うため追跡する
        ringTopColor_ = theme::kRingLockedColor;
        ringBottomColor_ = theme::kRingLockedColor;

        // リングを暗転する（「タッチ領域を薄暗く表示」の実装）。
        // docs/05-ui-ux.md: 「ロック中は中央に鍵アイコン、タッチ領域を薄暗く表示」
        // fillArc は対象ピクセルのみを書き換えるため、
        // drawRingHighlight() と同じ部分更新パターンで全画面転送を回避できる。
        if (canvasReady_) {
            drawRings(&canvas_, theme::kRingLockedColor,
                      theme::kRingLockedColor);
        }
        drawRings(&M5.Display, theme::kRingLockedColor,
                  theme::kRingLockedColor);

        // 鍵アイコンを描画する
        if (canvasReady_) {
            drawLockIcon(&canvas_);
        }
        drawLockIcon(&M5.Display);
    } else {
        // --- ロック表示を解除する ---

        // drawHoldProgress(0) がリング復元に使うため追跡する
        ringTopColor_ = theme::kRingNormalColor;
        ringBottomColor_ = theme::kRingNormalColor;

        // リングを通常色に戻す
        if (canvasReady_) {
            drawRings(&canvas_, theme::kRingNormalColor,
                      theme::kRingNormalColor);
        }
        drawRings(&M5.Display, theme::kRingNormalColor,
                  theme::kRingNormalColor);

        // ロック領域をクリアし分割帯を復元する
        if (canvasReady_) {
            clearLockRegion(&canvas_);
        }
        clearLockRegion(&M5.Display);
    }
}

// ============================================================
// Private: drawLockIcon — 鍵アイコンの描画
// ============================================================

void Renderer::drawLockIcon(LovyanGFX* target) {
    auto cx = static_cast<int32_t>(config::kCenterX);   // 234
    auto cy = static_cast<int32_t>(config::kCenterY);   // 234

    // ロック領域を背景色で塗りつぶす（分割帯を消去する）。
    // ロック領域 (48x28) は上下ライフ領域の隙間に収まっており、
    // 数字領域のピクセルには触れない。
    target->fillRect(theme::kLockRegionX, theme::kLockRegionY,
                     theme::kLockRegionW, theme::kLockRegionH,
                     theme::kBgColor);

    // 南京錠の鍵アイコンを図形で描画する。
    // フォント絵文字は M5GFX では確実に表示できない場合があるため、
    // fillArc + fillRoundRect + fillCircle で形状を構成する。
    //
    // 配置（絶対座標）:
    //   シャックル中心: (234, 228)  — 半円弧の中心
    //   本体:           (224, 228)  — 角丸矩形の左上角
    //   鍵穴:           (234, 233)  — 本体中央
    //   "LOCK" テキスト: (234, 244)  — 本体下方
    //
    // 垂直方向の内訳:
    //   シャックル上端:  y=222 (228-6)
    //   本体下端:        y=240 (228+12)
    //   テキスト下端:    y=248 (244+4)
    //   → 全体 y[222, 248] = 26px。隙間 28px (y[220, 248]) に収まる。

    // シャックル（上部 U 字型弧）
    // fillArc 180°→360° で上半円を描き、金属シャックルを表現する
    int32_t shackleCY = cy - 6;  // 228
    target->fillArc(cx, shackleCY, 6, 3, 180.0f, 360.0f,
                    theme::kLockIconColor);

    // 本体（角丸矩形）
    int32_t bodyW = 16;
    int32_t bodyH = 12;
    int32_t bodyX = cx - bodyW / 2;  // 226
    int32_t bodyY = shackleCY;       // 228
    target->fillRoundRect(bodyX, bodyY, bodyW, bodyH, 2,
                          theme::kLockIconColor);

    // 鍵穴（形状で「鍵」であることを補強する視覚的手がかり）
    // 丸穴 + 下向きスロットで鍵穴の形を表現する
    int32_t keyholeY = bodyY + 4;  // 232
    target->fillCircle(cx, keyholeY, 2, theme::kBgColor);
    target->fillRect(cx - 1, keyholeY, 2, 4, theme::kBgColor);

    // "LOCK" テキストラベル
    // 鍵アイコンの形状に加えてテキスト情報を併用する。
    // 色だけに頼らず形状とテキストの両方でロック状態を伝える
    // (docs/05-ui-ux.md の色覚差対応方針)。
    target->setTextDatum(middle_center);
    target->setTextSize(theme::kLockTextSize);
    target->setTextColor(theme::kLockIconColor, theme::kBgColor);
    target->drawString("LOCK", cx, cy + 10);  // y=244
}

// ============================================================
// Private: clearLockRegion — ロック領域のクリアと分割帯復元
// ============================================================

void Renderer::clearLockRegion(LovyanGFX* target) {
    // ロック領域を背景色で塗りつぶす
    target->fillRect(theme::kLockRegionX, theme::kLockRegionY,
                     theme::kLockRegionW, theme::kLockRegionH,
                     theme::kBgColor);

    // ロック領域内の分割帯を再描画する。
    // 分割帯はロック領域の全幅 (x[210, 258]) のみ復元すればよい。
    // ロック領域外の分割帯は drawLockIcon() で変更されておらず、
    // 元のまま残っているため再描画は不要。
    auto divY = static_cast<int32_t>(config::kCenterY)
              - theme::kDividerHeight / 2;
    target->fillRect(theme::kLockRegionX, divY,
                     theme::kLockRegionW, theme::kDividerHeight,
                     theme::kDividerColor);
}

// ============================================================
// Private: drawRings — リング弧の描画
// ============================================================

void Renderer::drawRings(LovyanGFX* target,
                         uint16_t topColor, uint16_t bottomColor) {
    auto cx = static_cast<int32_t>(config::kCenterX);
    auto cy = static_cast<int32_t>(config::kCenterY);

    // M5GFX (LovyanGFX) の fillArc 角度規約:
    //   0° = 右 (3 時方向)、角度は画面上で時計回りに増加する。
    //   したがって 90° = 下、180° = 左、270° = 上。
    //
    // 上半円: 180° (左) → 270° (上) → 360°/0° (右)
    // 下半円: 0° (右) → 90° (下) → 180° (左)
    target->fillArc(cx, cy, theme::kRingInnerR, theme::kRingOuterR,
                    180.0f, 360.0f, topColor);
    target->fillArc(cx, cy, theme::kRingInnerR, theme::kRingOuterR,
                    0.0f, 180.0f, bottomColor);
}

// ============================================================
// Private: renderLifeRegion — lifeCanvas_ へのライフ描画
// ============================================================

void Renderer::renderLifeRegion(uint32_t life, int32_t previewDelta,
                                bool isTop) {
    // 上側プレイヤーは 180 度回転で描画する。
    // M5Canvas::setRotation(2) は描画座標系を 180 度回転させるが、
    // pushSprite は常に生のピクセルバッファを転送する。
    // そのため回転状態で描かれたピクセルがそのまま
    // 正しい向きで画面に表示される。
    // Phase 0 Step 7 で回転描画の正常動作を実機確認済み。
    lifeCanvas_.setRotation(isTop ? 2 : 0);
    lifeCanvas_.fillScreen(theme::kBgColor);

    int32_t cx = theme::kLifeRegionW / 2;  // 水平中央 = 90

    lifeCanvas_.setTextDatum(middle_center);

    if (previewDelta == 0) {
        // --- 通常表示: ライフ値のみ ---
        bool isZero = (life == 0);
        uint16_t textColor =
            isZero ? theme::kLifeZeroColor : theme::kLifeColor;

        lifeCanvas_.setTextColor(textColor, theme::kBgColor);
        lifeCanvas_.setTextSize(theme::kLifeFontSize);

        char buf[16];
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(life));

        // ライフ 0 のときは "!" を上に表示するため、数字を少し下げる
        int32_t numY = isZero
            ? (theme::kLifeRegionH / 2 + 10)
            : (theme::kLifeRegionH / 2);
        lifeCanvas_.drawString(buf, cx, numY);

        if (isZero) {
            // 枠線 + "!" マークで警告する
            // 色だけに頼らない視覚的手がかり (docs/05-ui-ux.md)
            drawLifeZeroBorder(&lifeCanvas_,
                               theme::kLifeRegionW, theme::kLifeRegionH);

            lifeCanvas_.setTextSize(theme::kWarningFontSize);
            lifeCanvas_.setTextColor(
                theme::kLifeZeroBorderColor, theme::kBgColor);
            lifeCanvas_.setTextDatum(top_center);
            lifeCanvas_.drawString(
                "!", cx, theme::kLifeZeroBorderThickness + 2);
        }
    } else {
        // --- プレビュー表示: 確定後の値 + 差分 ---
        // docs/05-ui-ux.md の「プレビューと確定」に従い、
        // 確定後のライフ値と差分の両方を表示する。

        // 確定後の値を計算する。下限 0 でクランプ（ドメイン層と同じルール）。
        // int64_t でオーバーフローを回避する。
        int64_t raw = static_cast<int64_t>(life) + previewDelta;
        uint32_t previewLife = (raw < 0) ? 0u : static_cast<uint32_t>(raw);

        bool isZero = (previewLife == 0);

        // 確定後のライフ値（領域上部）
        uint16_t lifeColor =
            isZero ? theme::kLifeZeroColor : theme::kPreviewLifeColor;
        lifeCanvas_.setTextColor(lifeColor, theme::kBgColor);
        lifeCanvas_.setTextSize(theme::kLifeFontSize);

        char lifeBuf[16];
        snprintf(lifeBuf, sizeof(lifeBuf), "%u",
                 static_cast<unsigned>(previewLife));
        lifeCanvas_.drawString(lifeBuf, cx, theme::kLifeRegionH / 3);

        // 差分（領域下部）
        // 色と +/- 符号の両方で方向を示す（色覚差対応:
        // オレンジ/シアンの輝度差 + テキスト符号の二重伝達）
        uint16_t deltaColor = (previewDelta < 0)
            ? theme::kDeltaDecreaseColor
            : theme::kDeltaIncreaseColor;
        lifeCanvas_.setTextColor(deltaColor, theme::kBgColor);
        lifeCanvas_.setTextSize(theme::kDeltaFontSize);

        char deltaBuf[16];
        snprintf(deltaBuf, sizeof(deltaBuf), "%+d",
                 static_cast<int>(previewDelta));
        lifeCanvas_.drawString(deltaBuf, cx, theme::kLifeRegionH * 2 / 3);

        if (isZero) {
            // プレビュー中のゼロ警告は枠線のみ表示する。
            // "!" マークはプレビュー数字・差分テキストと重なるため省略し、
            // 枠線だけで視覚的に警告する。
            drawLifeZeroBorder(&lifeCanvas_,
                               theme::kLifeRegionW, theme::kLifeRegionH);
        }
    }
}

// ============================================================
// Private: drawLifeZeroBorder — ライフ 0 警告枠線
// ============================================================

void Renderer::drawLifeZeroBorder(LovyanGFX* target,
                                  int32_t w, int32_t h) {
    // 枠線で領域を囲む。色だけでなく枠線の太さ変化で
    // ライフ 0 を視覚的に伝える (docs/05-ui-ux.md の色覚対応方針)。
    for (int32_t i = 0; i < theme::kLifeZeroBorderThickness; ++i) {
        target->drawRect(i, i, w - 2 * i, h - 2 * i,
                         theme::kLifeZeroBorderColor);
    }
}

// ============================================================
// drawSetup — セットアップ画面（全画面転送）
// ============================================================

void Renderer::drawSetup(const app::ScreenState& sc) {
    // 毎ループ呼ばれない前提。ScreenState::consumeDirty() が true のときだけ呼ぶ。
    // 全画面転送 (44.6 ms) を行うため、高頻度呼び出しは避けること。

    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(theme::kBgColor);

    // セットアップ画面はリングを描画しないため、弧領域は背景色になる。
    // drawHoldProgress(0) が正しく復元できるよう追跡する。
    ringTopColor_ = theme::kBgColor;
    ringBottomColor_ = theme::kBgColor;

    // 全画面転送で進捗弧が消えるため、次回 drawHoldProgress() は
    // トラックから描き直す必要がある
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // --- 下側プレイヤーの開始ライフ（通常向き）---
    renderSetupLifeRegion(sc.setupLife(PlayerId::Bottom), false);
    lifeCanvas_.pushSprite(target, theme::kLifeTopX, theme::kSetupBottomY);

    // --- 上側プレイヤーの開始ライフ（180 度回転）---
    // 上側プレイヤーは対面の相手が見る向きなので 180 度回転して描く。
    // renderSetupLifeRegion 内で lifeCanvas_.setRotation(2) を設定する。
    renderSetupLifeRegion(sc.setupLife(PlayerId::Top), true);
    lifeCanvas_.pushSprite(target, theme::kLifeTopX, theme::kSetupTopY);

    // --- 操作説明（画面中央の隙間 y[200, 268] に配置）---
    // ボタンは下側プレイヤーが操作する前提なので、通常向きで描く。
    target->setTextDatum(middle_center);

    target->setTextSize(theme::kSetupHintFontSize);
    target->setTextColor(theme::kHintTextColor, theme::kBgColor);
    target->drawString("Ring: +/- Life", cx, theme::kSetupHintY1);
    target->drawString("A: 20/40 toggle", cx, theme::kSetupHintY2);

    // 「Hold B to START」は開始への唯一の導線なので、強調表示する。
    // 初見のユーザーがここで詰まないよう、色とフォントサイズの両方で目立たせる。
    target->setTextSize(theme::kSetupStartFontSize);
    target->setTextColor(theme::kSetupStartColor, theme::kBgColor);
    target->drawString("Hold B to START", cx, theme::kSetupHintY3);

    // 全画面転送
    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawMenu — ゲームメニュー画面（全画面転送）
// ============================================================

void Renderer::drawMenu(const app::ScreenState& sc,
                        uint8_t batteryPercent, bool charging) {

    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(theme::kBgColor);

    // メニュー画面はリングを描画しないため、弧領域は背景色になる。
    // drawHoldProgress(0) が正しく復元できるよう追跡する。
    ringTopColor_ = theme::kBgColor;
    ringBottomColor_ = theme::kBgColor;

    // 全画面転送で進捗弧が消えるため、次回 drawHoldProgress() は
    // トラックから描き直す必要がある
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    bool confirming = sc.awaitingConfirm();

    // --- メニュー項目の描画 ---
    // 項目名は英語。docs/05-ui-ux.md のメニュー定義に準拠する。
    // MenuItem 列挙と同じ順序で並べる:
    //   Resume(0), History(1), SetLife(2), Rematch(3),
    //   SwapSides(4), About(5)
    static constexpr const char* kItemNames[] = {
        "Resume", "History", "Set Life", "Rematch",
        "Swap Sides", "About"
    };
    static_assert(
        sizeof(kItemNames) / sizeof(kItemNames[0]) == app::kMenuItemCount,
        "kItemNames size must match kMenuItemCount");

    target->setTextDatum(middle_center);

    for (uint8_t i = 0; i < app::kMenuItemCount; ++i) {
        int32_t itemY = theme::kMenuFirstItemY
                      + static_cast<int32_t>(i) * theme::kMenuItemSpacing;
        bool isSelected = (i == sc.menuIndex());
        auto item = static_cast<app::MenuItem>(i);
        bool isConfirmTarget = confirming && (item == sc.confirmTarget());

        char buf[32];

        if (isConfirmTarget) {
            // 確認待ちの対象項目。破壊的操作（Rematch）が
            // 誤操作で実行されないよう、オレンジ色と「?」で警告する。
            // 色だけでなく記号（>, ?, <）でも強調する（色覚差対応）。
            target->setTextSize(theme::kMenuItemFontSize);
            target->setTextColor(theme::kMenuConfirmColor, theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s? <", kItemNames[i]);
            target->drawString(buf, cx, itemY);

        } else if (isSelected) {
            // 選択中の項目。色と記号（>）の両方で示す（色覚差対応）。
            target->setTextSize(theme::kMenuItemFontSize);
            target->setTextColor(theme::kMenuSelectedColor, theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s", kItemNames[i]);
            target->drawString(buf, cx, itemY);

        } else {
            // 非選択項目
            target->setTextSize(theme::kMenuItemFontSize);
            target->setTextColor(theme::kMenuNormalColor, theme::kBgColor);
            target->drawString(kItemNames[i], cx, itemY);
        }
    }

    // --- 確認待ちメッセージ ---
    // 長押し進捗は drawHoldProgress() が円弧のみで表現する（数値テキストは廃止済み）。
    // ここでは操作指示テキスト "Hold B to confirm" のみ表示する。
    // この文言は「何をすべきか」を伝える役割であり、
    // drawHoldProgress() の進捗円弧（「今どこまで進んだか」）とは責務が異なる。
    if (confirming) {
        target->setTextSize(theme::kMenuConfirmFontSize);
        target->setTextColor(theme::kMenuConfirmColor, theme::kBgColor);
        target->setTextDatum(middle_center);
        target->drawString("Hold B to confirm", cx, theme::kMenuConfirmMsgY);
    }

    // --- 操作説明 ---
    target->setTextDatum(middle_center);
    target->setTextSize(theme::kMenuHintFontSize);
    target->setTextColor(theme::kHintTextColor, theme::kBgColor);
    target->drawString("A=Next  B=Select  A+B(hold)=Close", cx, theme::kMenuHintY);

    // --- バッテリー残量表示（電池アイコン ＋ パーセント数値）---
    // M5GFX の標準フォントは絵文字（🔋 等）に非対応のため、
    // 電池アイコンを図形で描画する。鍵アイコン (drawLockIcon()) と同じ方針。
    //
    // レイアウト: [!] [電池アイコン] [間隔] [87%]
    //   "!" は 20% 以下の警告時のみ表示する。
    //   アイコンと数値を合わせて中央揃えにする。
    {
        bool warning =
            (batteryPercent <= theme::kBatteryWarningThreshold);
        uint16_t color = warning
            ? theme::kBatteryWarningColor
            : theme::kBatteryNormalColor;
        int32_t borderThick = warning
            ? theme::kBatteryBorderWarning
            : theme::kBatteryBorderNormal;

        // パーセント文字列の準備
        char batBuf[8];
        snprintf(batBuf, sizeof(batBuf), "%u%%",
                 static_cast<unsigned>(batteryPercent));

        // テキスト幅を取得する（中央揃え計算に使用）
        target->setTextSize(theme::kBatteryPercentFontSize);
        int32_t textW = target->textWidth(batBuf);

        // 警告 "!" マークの幅（20% 以下のみ）
        int32_t warnW = 0;
        constexpr int32_t kWarnGap = 3;  // "!" とアイコンの間隔
        if (warning) {
            target->setTextSize(theme::kBatteryWarnMarkFontSize);
            warnW = target->textWidth("!") + kWarnGap;
        }

        // アイコン＋テキスト全体の幅を算出し中央揃えする
        int32_t iconTotalW = theme::kBatteryIconBodyW
                           + theme::kBatteryIconTermW;
        int32_t totalW = warnW + iconTotalW
                       + theme::kBatteryIconGap + textW;
        int32_t startX = cx - totalW / 2;

        // --- "!" 警告マーク（20% 以下のみ）---
        // 色だけでなく記号でも警告を伝える（色覚差対応）。
        if (warning) {
            target->setTextSize(theme::kBatteryWarnMarkFontSize);
            target->setTextColor(theme::kBatteryWarningColor,
                                 theme::kBgColor);
            target->setTextDatum(middle_left);
            target->drawString("!", startX, theme::kBatteryY);
        }

        // --- 電池アイコンの描画 ---
        int32_t iconX = startX + warnW;
        int32_t iconY = theme::kBatteryY
                      - theme::kBatteryIconBodyH / 2;

        // 本体の枠線。
        // 警告時は枠を太くすることで、残量バーの短さに加えて
        // 形の変化でも低残量を伝える（docs/05-ui-ux.md 色覚差対応）。
        for (int32_t i = 0; i < borderThick; ++i) {
            target->drawRect(
                iconX + i, iconY + i,
                theme::kBatteryIconBodyW - 2 * i,
                theme::kBatteryIconBodyH - 2 * i,
                color);
        }

        // 端子（本体右端の小さな突起。電池の向きを示す）
        int32_t termX = iconX + theme::kBatteryIconBodyW;
        int32_t termY = theme::kBatteryY
                      - theme::kBatteryIconTermH / 2;
        target->fillRect(termX, termY,
                         theme::kBatteryIconTermW,
                         theme::kBatteryIconTermH,
                         color);

        // 残量バー（本体内部を batteryPercent に比例して左から塗りつぶす）。
        // これが最も直感的にバッテリー残量を伝える要素。
        int32_t fillMaxW = theme::kBatteryIconBodyW
                         - 2 * theme::kBatteryIconPad;
        int32_t fillH = theme::kBatteryIconBodyH
                      - 2 * theme::kBatteryIconPad;
        int32_t fillW = fillMaxW * batteryPercent / 100;
        int32_t fillX = iconX + theme::kBatteryIconPad;
        int32_t fillY = iconY + theme::kBatteryIconPad;

        if (fillW > 0) {
            target->fillRect(fillX, fillY, fillW, fillH, color);
        }

        // --- 充電中インジケータ（稲妻マーク）---
        // isCharging() が true のときだけ描画する。
        // isCharging() は満充電時に false を返すため
        // （Phase 0 実測、docs/01 記録）、
        // false は「USB 未接続」とは断定できない。
        // したがって false のときは何も表示しない。
        if (charging) {
            // 電池本体の中央に稲妻マーク（⚡ 相当）を図形で描く。
            // 2 つの三角形で構成するジグザグ形状:
            //   上半分: 左上 → 中央バー右端（右下がりの三角形）
            //   下半分: 中央バー左端 → 右下（右下がりの三角形）
            // 上端が中心より左、下端が中心より右にずれることで
            // ジグザグの稲妻形状になる。
            // 白色で描画し、背景 (黒) と残量バー (グレー/オレンジ) の
            // どちらの上でも視認できるようにする。
            int32_t boltCX = iconX + theme::kBatteryIconBodyW / 2;
            int32_t boltCY = theme::kBatteryY;

            // 上半分: 左上から中央横断線まで広がる三角形
            target->fillTriangle(
                boltCX - 1, boltCY - 3,   // 上端（中心より左）
                boltCX + 2, boltCY,        // 中央右
                boltCX,     boltCY,        // 中央
                theme::kBatteryChargeBoltColor);
            // 下半分: 中央横断線から右下へ収束する三角形
            target->fillTriangle(
                boltCX,     boltCY,        // 中央
                boltCX - 2, boltCY,        // 中央左
                boltCX + 1, boltCY + 3,    // 下端（中心より右）
                theme::kBatteryChargeBoltColor);
        }

        // --- パーセント数値 ---
        int32_t textX = iconX + iconTotalW
                      + theme::kBatteryIconGap;
        target->setTextDatum(middle_left);
        target->setTextSize(theme::kBatteryPercentFontSize);
        target->setTextColor(color, theme::kBgColor);
        target->drawString(batBuf, textX, theme::kBatteryY);
    }

    // 長押しプログレスの円弧は drawHoldProgress() に一本化した。
    // drawMenu() は項目一覧・操作説明・バッテリー残量を描画する。
    // アプリ層は必要に応じて drawMenu() 後に drawHoldProgress() を呼ぶこと。

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawHistory — 履歴画面（全画面転送）
// ============================================================

void Renderer::drawHistory(const domain::MatchState& state) {
    // 毎ループ呼ばれない前提。画面遷移時に 1 回だけ呼ぶ。

    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(theme::kBgColor);

    // 履歴画面はリングを描画しないため、弧領域は背景色になる
    ringTopColor_ = theme::kBgColor;
    ringBottomColor_ = theme::kBgColor;

    // 全画面転送で進捗弧が消えるため、次回 drawHoldProgress() は
    // トラックから描き直す必要がある
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // --- タイトル ---
    target->setTextDatum(middle_center);
    target->setTextSize(theme::kHistoryTitleFontSize);
    target->setTextColor(theme::kHistoryTitleColor, theme::kBgColor);
    target->drawString("HISTORY", cx, theme::kHistoryTitleY);

    if (state.history.empty()) {
        // 履歴が空のとき。初回ゲーム開始直後など。
        target->setTextSize(theme::kHistoryItemFontSize);
        target->setTextColor(theme::kHistoryEmptyColor, theme::kBgColor);
        target->drawString("No changes yet", cx,
                           static_cast<int32_t>(config::kCenterY));
    } else {
        // 最新から kHistoryMaxVisible 件を表示する。
        // history[0] が最新、history[size()-1] が最古。
        size_t count = state.history.size();
        size_t visible = (count < theme::kHistoryMaxVisible)
            ? count : theme::kHistoryMaxVisible;

        target->setTextDatum(middle_center);
        target->setTextSize(theme::kHistoryItemFontSize);

        for (size_t i = 0; i < visible; ++i) {
            const auto& entry = state.history[i];
            int32_t y = theme::kHistoryFirstItemY
                      + static_cast<int32_t>(i) * theme::kHistorySpacing;

            // プレイヤー識別にテキストラベル "TOP"/"BTM" を使い、
            // 色だけに頼らない（色覚差対応）。
            // 色は補助的な視覚的手がかりとして併用する。
            const char* tag =
                (entry.player == PlayerId::Top) ? "TOP" : "BTM";
            uint16_t color =
                (entry.player == PlayerId::Top)
                    ? theme::kHistoryTopColor
                    : theme::kHistoryBottomColor;

            char line[48];
            snprintf(line, sizeof(line), "%s %u>%u (%+d)",
                     tag,
                     static_cast<unsigned>(entry.before),
                     static_cast<unsigned>(entry.after),
                     static_cast<int>(entry.appliedDelta));

            target->setTextColor(color, theme::kBgColor);
            target->drawString(line, cx, y);
        }

        // 表示しきれない履歴がある場合はその旨を示す
        if (visible < count) {
            int32_t moreY = theme::kHistoryFirstItemY
                          + static_cast<int32_t>(visible)
                            * theme::kHistorySpacing;
            target->setTextSize(theme::kHistoryFooterFontSize);
            target->setTextColor(theme::kHistoryEmptyColor, theme::kBgColor);

            char moreBuf[24];
            snprintf(moreBuf, sizeof(moreBuf), "(%u more)",
                     static_cast<unsigned>(count - visible));
            target->drawString(moreBuf, cx, moreY);
        }
    }

    // --- 操作説明 ---
    target->setTextDatum(middle_center);
    target->setTextSize(theme::kHistoryFooterFontSize);
    target->setTextColor(theme::kHintTextColor, theme::kBgColor);
    target->drawString("B: Back", cx, theme::kHistoryFooterY);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawAbout — About 画面（全画面転送）
// ============================================================

void Renderer::drawAbout() {
    // 毎ループ呼ばれない前提。画面遷移時に 1 回だけ呼ぶ。

    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(theme::kBgColor);

    // About 画面はリングを描画しないため、弧領域は背景色になる
    ringTopColor_ = theme::kBgColor;
    ringBottomColor_ = theme::kBgColor;

    // 全画面転送で進捗弧が消えるため、次回 drawHoldProgress() は
    // トラックから描き直す必要がある
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    target->setTextDatum(middle_center);

    // --- タイトル ---
    target->setTextSize(theme::kAboutTitleFontSize);
    target->setTextColor(theme::kAboutTitleColor, theme::kBgColor);
    target->drawString("Life Counter", cx, theme::kAboutTitleY);

    // --- バージョン ---
    // theme::kFirmwareVersion を表示する。リリース時に theme.hpp で更新する。
    target->setTextSize(theme::kAboutVersionFontSize);
    target->setTextColor(theme::kAboutVersionColor, theme::kBgColor);
    char verBuf[24];
    snprintf(verBuf, sizeof(verBuf), "v%s", theme::kFirmwareVersion);
    target->drawString(verBuf, cx, theme::kAboutVersionY);

    // --- 操作説明 ---
    target->setTextSize(theme::kAboutFooterFontSize);
    target->setTextColor(theme::kHintTextColor, theme::kBgColor);
    target->drawString("B: Back", cx, theme::kAboutFooterY);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// Private: renderSetupLifeRegion — セットアップ画面のライフ描画
// ============================================================

void Renderer::renderSetupLifeRegion(uint32_t life, bool isTop) {
    // lifeCanvas_ にライフ数字とプリセット一致表示を描画する。
    // 上側プレイヤーは 180 度回転で描画する
    // （drawAll の renderLifeRegion と同じ回転方式）。
    lifeCanvas_.setRotation(isTop ? 2 : 0);
    lifeCanvas_.fillScreen(theme::kBgColor);

    int32_t cx = theme::kLifeRegionW / 2;  // 90

    // --- ライフ数字 ---
    lifeCanvas_.setTextDatum(middle_center);
    lifeCanvas_.setTextSize(theme::kSetupLifeFontSize);
    lifeCanvas_.setTextColor(theme::kLifeColor, theme::kBgColor);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(life));
    lifeCanvas_.drawString(buf, cx, theme::kSetupLifeNumCY);

    // --- プリセット一致表示 ---
    // 20 と 40 を並べ、現在値と一致するものをシアンで強調する。
    // 色だけでなく角括弧 [] で一致を示す（色覚差対応）。
    // 不一致のプリセットはスペースで囲み、幅を揃える。
    lifeCanvas_.setTextSize(theme::kSetupPresetFontSize);

    bool is20 = (life == 20);
    bool is40 = (life == 40);

    // 中央から左に 20、右に 40 を配置する
    char label20[8], label40[8];
    snprintf(label20, sizeof(label20), "%s", is20 ? "[20]" : " 20 ");
    snprintf(label40, sizeof(label40), "%s", is40 ? "[40]" : " 40 ");

    lifeCanvas_.setTextColor(
        is20 ? theme::kSetupPresetActiveColor
             : theme::kSetupPresetInactiveColor,
        theme::kBgColor);
    lifeCanvas_.setTextDatum(middle_right);
    lifeCanvas_.drawString(label20, cx - 4, theme::kSetupPresetCY);

    lifeCanvas_.setTextColor(
        is40 ? theme::kSetupPresetActiveColor
             : theme::kSetupPresetInactiveColor,
        theme::kBgColor);
    lifeCanvas_.setTextDatum(middle_left);
    lifeCanvas_.drawString(label40, cx + 4, theme::kSetupPresetCY);
}

// ============================================================
// drawHoldProgress — 長押し進捗の部分再描画
// ============================================================

void Renderer::drawHoldProgress(uint8_t percent) {
    // 長押し進捗を差分描画で表示する。
    // どの画面の上にも重ねて描画でき、percent=0 で元のリング表示を復元する。
    //
    // なぜ差分だけ描くのか:
    //   以前は毎回トラック弧を全周 (360度) 塗り直してから進捗弧を重ねていた。
    //   画面へ直接描画しておりダブルバッファではないため、既に進捗色 (シアン)
    //   で塗られていた部分が毎回いったんトラック色 (グレー) に戻ってから
    //   進捗色で塗り直され、フラッシュ（ちらつき）として見えていた。
    //   差分のみ描画することで、既に塗った部分に触れずちらつきを解消する。
    //
    // 部分再描画の仕組み:
    //   fillArc は描画対象の表面に直接ピクセルを書き、SPI バスで
    //   対象領域のみを転送する。全画面転送 (44.6 ms) を回避でき、
    //   差分弧の転送量は全周の一部のみで 43 ms の描画予算に十分収まる。
    //
    // 注意: drawHoldProgress() 呼び出し後に drawMenu() 等の全画面メソッドを
    // 呼ぶと進捗表示は上書きされる。全画面メソッドは画面全体を再描画するため、
    // 進捗弧も含めて fillScreen で塗りつぶされる。全画面メソッドは
    // lastHoldPercent_ を 0 にリセットするので、次回呼び出し時にトラックから
    // 描き直す。アプリ層が表示の一貫性を制御する前提。

    auto cx = static_cast<int32_t>(config::kCenterX);
    auto cy = static_cast<int32_t>(config::kCenterY);

    if (percent == 0) {
        // --- 進捗表示を消去し、元の表示を復元する ---
        // ringTopColor_ / ringBottomColor_ で追跡しているリング色
        // （非 Active 画面では kBgColor）を使って弧領域を塗り直す。
        // 上半円と下半円を別色で描くことで、リングハイライト中の
        // 非対称な色分けも正しく復元できる。
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                            180.0f, 360.0f, ringTopColor_);
            canvas_.fillArc(cx, cy,
                            theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                            0.0f, 180.0f, ringBottomColor_);
        }
        M5.Display.fillArc(cx, cy,
                           theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                           180.0f, 360.0f, ringTopColor_);
        M5.Display.fillArc(cx, cy,
                           theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                           0.0f, 180.0f, ringBottomColor_);
        lastHoldPercent_ = 0;
        return;
    }

    uint8_t pct = (percent > 100) ? 100 : percent;

    // 同じ値のときは何も描かない。アプリ層でも 5% 刻みの抑制をしているが、
    // 描画層でも二重に守ることで無駄な SPI 転送を確実に防ぐ。
    if (pct == lastHoldPercent_) {
        return;
    }

    // --- 弧の差分描画 ---
    // M5GFX fillArc 角度規約: 0°=右(3時), 時計回りに増加。270°=上端(12時)。
    // 進捗は上端 (270°) から時計回りに伸びる。

    if (lastHoldPercent_ == 0) {
        // 進捗の開始: トラック弧を全周 1 回だけ描き、0→pct の進捗弧を塗る。
        // 以降の呼び出しでは差分だけ塗るため、トラックの全周描画はここだけ。
        // 視認性: 0x4208 (RGB 66,66,66) は AMOLED の黒背景と区別でき、
        // 進捗弧 (シアン 0x07FF) との輝度差も十分 (色覚差に依存しない)。
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                            0.0f, 360.0f, theme::kHoldArcTrackColor);
        }
        M5.Display.fillArc(cx, cy,
                           theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                           0.0f, 360.0f, theme::kHoldArcTrackColor);

        // 0→pct の進捗弧を描画する
        float endAngle = 270.0f + 360.0f * pct / 100;
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                            270.0f, endAngle, theme::kHoldArcColor);
        }
        M5.Display.fillArc(cx, cy,
                           theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                           270.0f, endAngle, theme::kHoldArcColor);

    } else if (pct > lastHoldPercent_) {
        // 増加: 前回の角度から今回の角度までの差分だけを進捗色で塗る。
        // 既に塗った部分には触れないため、ちらつきが発生しない。
        // これがちらつき解消の核心: トラックの全周塗り直しを行わず、
        // 進捗が伸びた分だけを上塗りする。
        float fromAngle = 270.0f + 360.0f * lastHoldPercent_ / 100;
        float toAngle   = 270.0f + 360.0f * pct / 100;
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                            fromAngle, toAngle, theme::kHoldArcColor);
        }
        M5.Display.fillArc(cx, cy,
                           theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                           fromAngle, toAngle, theme::kHoldArcColor);

    } else {
        // 減少 (pct < lastHoldPercent_): 通常は起きないが保険として対応。
        // 減った範囲だけをトラック色で塗り戻す。
        float fromAngle = 270.0f + 360.0f * pct / 100;
        float toAngle   = 270.0f + 360.0f * lastHoldPercent_ / 100;
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                            fromAngle, toAngle, theme::kHoldArcTrackColor);
        }
        M5.Display.fillArc(cx, cy,
                           theme::kHoldArcInnerR, theme::kHoldArcOuterR,
                           fromAngle, toAngle, theme::kHoldArcTrackColor);
    }

    lastHoldPercent_ = pct;
}

}  // namespace counter::ui
