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

    // 2. 外周リング（両方とも通常色）
    drawRings(target, theme::kRingNormalColor, theme::kRingNormalColor);

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

    // 5. 全画面転送（起動時・リマッチ時のみ呼ぶ想定。44.6 ms かかる）
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

}  // namespace counter::ui
