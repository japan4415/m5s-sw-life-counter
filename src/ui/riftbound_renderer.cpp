// 描画層の実装（Riftbound 版）。
//
// PSRAM 上の全画面 Canvas をバッファとして使い、
// 変更のあった部分矩形のみをディスプレイに転送する。
//
// 構造は FaB 版 renderer.cpp と同一である。意味の差分は次の 3 点:
//   1. ライフ 0 警告（オレンジ） -> 勝利点到達警告（ゴールド）
//   2. セットアップ画面にプリセット表示が無い（得点は常に 0 から開始）
//   3. メニューは SetLife を除く 5 項目を表示する
//
// なぜ全画面転送を避けるのか:
//   外周スライド中、得点表示は約 43 ms ごとに更新される
//   （感度 10 度/得点、スイープ角速度 233 度/秒の実測値に基づく）。
//   全画面 (468x468) 転送は 44.6 ms かかり、この予算を超過する。
//   数字領域 (180x120) の転送は約 4.5 ms で、十分な余裕がある。
//   (Phase 0 Step 8 実測、ADR-15 決定)

#include "ui/riftbound_renderer.hpp"
#include "ui/render_framework.hpp"
#include "ui/riftbound_theme.hpp"
#include "app_config.hpp"

#include <cstring>  // strlen

namespace counter::ui {

// ============================================================
// begin — Canvas の確保
// ============================================================

void RiftboundRenderer::begin() {
    // --- 全画面 Canvas を PSRAM 上に確保する ---
    // 468x468 x 2 bytes (RGB565) = 438,048 bytes。
    // Phase 0 Step 6 で確保可能なことを確認済み（FaB 版と同一条件）。
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    void* mainBuf = canvas_.createSprite(
        config::kDisplayWidth, config::kDisplayHeight);

    if (mainBuf == nullptr) {
        // PSRAM 確保失敗。フォールバックとして直接描画に切り替える。
        // 全画面バッファなしでも drawScore() の部分転送は scoreCanvas_ 経由で可能。
        Serial.println(
            "[RiftboundRenderer] ERROR: PSRAM canvas allocation failed. "
            "Falling back to direct draw.");
        Serial.printf(
            "[RiftboundRenderer]   Requested: %d x %d x 2 = %d bytes\n",
            config::kDisplayWidth, config::kDisplayHeight,
            config::kDisplayWidth * config::kDisplayHeight * 2);
        Serial.printf(
            "[RiftboundRenderer]   Free PSRAM: %u bytes\n",
            static_cast<unsigned>(ESP.getFreePsram()));
        canvasReady_ = false;
    } else {
        canvasReady_ = true;
        Serial.printf(
            "[RiftboundRenderer] Main canvas: %d x %d allocated in PSRAM. "
            "Free PSRAM: %u bytes\n",
            config::kDisplayWidth, config::kDisplayHeight,
            static_cast<unsigned>(ESP.getFreePsram()));
    }

    // --- 数字領域用テンプキャンバス ---
    // 180x120 x 2 = 43,200 bytes。
    // SRAM のほうが PSRAM より転送が速いため、まず SRAM を試す。
    scoreCanvas_.setPsram(false);
    scoreCanvas_.setColorDepth(16);
    void* scoreBuf = scoreCanvas_.createSprite(
        rift_theme::kScoreRegionW, rift_theme::kScoreRegionH);

    if (scoreBuf == nullptr) {
        // SRAM 不足時は PSRAM で再試行する
        Serial.println(
            "[RiftboundRenderer] WARN: Score canvas SRAM alloc failed, "
            "retrying in PSRAM...");
        scoreCanvas_.setPsram(true);
        scoreBuf = scoreCanvas_.createSprite(
            rift_theme::kScoreRegionW, rift_theme::kScoreRegionH);
        if (scoreBuf == nullptr) {
            Serial.println(
                "[RiftboundRenderer] ERROR: Score canvas allocation failed "
                "(SRAM and PSRAM both).");
        }
    }
}

// ============================================================
// drawAll — 全画面描画
// ============================================================

void RiftboundRenderer::drawAll(const riftbound::MatchState& state) {
    // Canvas が確保できていれば Canvas へ描画し最後に一括転送。
    // 確保に失敗している場合は Display へ直接描画する（フォールバック）。
    LovyanGFX* target = selectDrawTarget(canvasReady_, canvas_);

    // 1. 背景を黒で塗りつぶす
    beginFullScreenDraw(target, rift_theme::kBgColor, lastHoldPercent_);

    // 2. 外周リング
    //    ロック中は暗転して操作無効を視覚的に伝える (docs/05-ui-ux.md 表示ルール)
    uint16_t ringColor = state.touchLocked
        ? rift_theme::kRingLockedColor
        : rift_theme::kRingNormalColor;
    drawRings(target, ringColor, ringColor);

    // drawHoldProgress(0) がリング復元に使うため、現在の色を追跡する
    ringTopColor_ = ringColor;
    ringBottomColor_ = ringColor;

    // 3. 中央分割帯
    auto divY = static_cast<int32_t>(config::kCenterY)
              - rift_theme::kDividerHeight / 2;
    target->fillRect(0, divY, config::kDisplayWidth,
                     rift_theme::kDividerHeight, rift_theme::kDividerColor);

    // 4. 両プレイヤーの得点数字を scoreCanvas_ 経由で target に描画する
    const uint32_t topScore =
        state.players[riftbound::toIndex(riftbound::PlayerId::Top)].score;
    const uint32_t bottomScore =
        state.players[riftbound::toIndex(riftbound::PlayerId::Bottom)].score;
    // 下側プレイヤー（通常向き）
    renderScoreRegion(bottomScore, 0, state.victoryScore, false);
    scoreCanvas_.pushSprite(target, rift_theme::kScoreBottomX, rift_theme::kScoreBottomY);

    // 上側プレイヤー（180 度回転）
    renderScoreRegion(topScore, 0, state.victoryScore, true);
    scoreCanvas_.pushSprite(target, rift_theme::kScoreTopX, rift_theme::kScoreTopY);

    // 5. ロック中は鍵アイコンを表示する
    if (state.touchLocked) {
        drawLockIcon(target);
    }

    // 6. 全画面転送（起動時・リマッチ時のみ呼ぶ想定。44.6 ms かかる）
    endFullScreenDraw(canvasReady_, canvas_);
}

// ============================================================
// drawScore — 数字領域の部分更新
// ============================================================

void RiftboundRenderer::drawScore(const riftbound::MatchState& state,
                                  riftbound::PlayerId player,
                                  int32_t previewDelta) {
    bool isTop = (player == riftbound::PlayerId::Top);
    const uint32_t score = state.players[riftbound::toIndex(player)].score;
    int32_t rx = isTop ? rift_theme::kScoreTopX : rift_theme::kScoreBottomX;
    int32_t ry = isTop ? rift_theme::kScoreTopY : rift_theme::kScoreBottomY;

    // scoreCanvas_ に得点情報を描画する
    renderScoreRegion(score, previewDelta, state.victoryScore, isTop);

    // 全画面 Canvas に反映して一貫性を保つ。
    // drawRingHighlight() がリング更新時に Canvas を参照するため、
    // 数字領域も最新状態にしておく必要がある。
    if (canvasReady_) {
        scoreCanvas_.pushSprite(&canvas_, rx, ry);
    }

    // Display に部分転送する（高速パス: 180x120 ≈ 4.5 ms）。
    scoreCanvas_.pushSprite(&M5.Display, rx, ry);
}

// ============================================================
// drawRingHighlight — リングの強調/復帰
// ============================================================

void RiftboundRenderer::drawRingHighlight(riftbound::PlayerId player, bool on) {
    uint16_t topColor;
    uint16_t bottomColor;

    if (on) {
        // 操作中: 対象プレイヤー側を強調し、非対象側を暗くする
        if (player == riftbound::PlayerId::Top) {
            topColor    = rift_theme::kRingHighlightColor;
            bottomColor = rift_theme::kRingDimColor;
        } else {
            topColor    = rift_theme::kRingDimColor;
            bottomColor = rift_theme::kRingHighlightColor;
        }
    } else {
        // 操作終了: 両方とも通常色に戻す
        topColor    = rift_theme::kRingNormalColor;
        bottomColor = rift_theme::kRingNormalColor;
    }

    // drawHoldProgress(0) がリング復元に使うため、現在の色を追跡する
    ringTopColor_ = topColor;
    ringBottomColor_ = bottomColor;

    // Canvas と Display の両方にリングを描画する。
    // 得点領域はリング内周 (r=165) の内側に全角が収まるよう設計してあるため
    // (riftbound_theme.hpp の得点領域コメント参照)、リングの fillArc が
    // 数字を上書きすることはない。
    if (canvasReady_) {
        drawRings(&canvas_, topColor, bottomColor);
    }
    drawRings(&M5.Display, topColor, bottomColor);
}

// ============================================================
// drawLockState — タッチロック表示の更新
// ============================================================

void RiftboundRenderer::drawLockState(const riftbound::MatchState& state) {
    // 部分再描画のみで全画面転送 (44.6 ms) を回避する。
    if (state.touchLocked) {
        // --- ロック表示を有効にする ---

        // drawHoldProgress(0) がリング復元に使うため追跡する
        ringTopColor_ = rift_theme::kRingLockedColor;
        ringBottomColor_ = rift_theme::kRingLockedColor;

        // リングを暗転する
        if (canvasReady_) {
            drawRings(&canvas_, rift_theme::kRingLockedColor,
                      rift_theme::kRingLockedColor);
        }
        drawRings(&M5.Display, rift_theme::kRingLockedColor,
                  rift_theme::kRingLockedColor);

        // 鍵アイコンを描画する
        if (canvasReady_) {
            drawLockIcon(&canvas_);
        }
        drawLockIcon(&M5.Display);
    } else {
        // --- ロック表示を解除する ---

        ringTopColor_ = rift_theme::kRingNormalColor;
        ringBottomColor_ = rift_theme::kRingNormalColor;

        // リングを通常色に戻す
        if (canvasReady_) {
            drawRings(&canvas_, rift_theme::kRingNormalColor,
                      rift_theme::kRingNormalColor);
        }
        drawRings(&M5.Display, rift_theme::kRingNormalColor,
                  rift_theme::kRingNormalColor);

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

void RiftboundRenderer::drawLockIcon(LovyanGFX* target) {
    auto cx = static_cast<int32_t>(config::kCenterX);   // 234
    auto cy = static_cast<int32_t>(config::kCenterY);   // 234

    // ロック領域を背景色で塗りつぶす（分割帯を消去する）。
    target->fillRect(rift_theme::kLockRegionX, rift_theme::kLockRegionY,
                     rift_theme::kLockRegionW, rift_theme::kLockRegionH,
                     rift_theme::kBgColor);

    // 南京錠の鍵アイコンを図形で描画する（FaB 版と同一の形状）。
    // 配置（絶対座標）:
    //   シャックル中心: (234, 228)  — 半円弧の中心
    //   本体:           (226, 228)  — 角丸矩形の左上角
    //   鍵穴:           (234, 233)  — 本体中央
    //   "LOCK" テキスト: (234, 244)  — 本体下方

    // シャックル（上部 U 字型弧）
    int32_t shackleCY = cy - 6;  // 228
    target->fillArc(cx, shackleCY, 6, 3, 180.0f, 360.0f,
                    rift_theme::kLockIconColor);

    // 本体（角丸矩形）
    int32_t bodyW = 16;
    int32_t bodyH = 12;
    int32_t bodyX = cx - bodyW / 2;  // 226
    int32_t bodyY = shackleCY;       // 228
    target->fillRoundRect(bodyX, bodyY, bodyW, bodyH, 2,
                          rift_theme::kLockIconColor);

    // 鍵穴（丸穴 + 下向きスロット）
    int32_t keyholeY = bodyY + 4;  // 232
    target->fillCircle(cx, keyholeY, 2, rift_theme::kBgColor);
    target->fillRect(cx - 1, keyholeY, 2, 4, rift_theme::kBgColor);

    // "LOCK" テキストラベル
    // 色だけに頼らず形状とテキストの両方でロック状態を伝える。
    target->setTextDatum(middle_center);
    target->setTextSize(rift_theme::kLockTextSize);
    target->setTextColor(rift_theme::kLockIconColor, rift_theme::kBgColor);
    target->drawString("LOCK", cx, cy + 10);  // y=244
}

// ============================================================
// Private: clearLockRegion — ロック領域のクリアと分割帯復元
// ============================================================

void RiftboundRenderer::clearLockRegion(LovyanGFX* target) {
    // ロック領域を背景色で塗りつぶす
    target->fillRect(rift_theme::kLockRegionX, rift_theme::kLockRegionY,
                     rift_theme::kLockRegionW, rift_theme::kLockRegionH,
                     rift_theme::kBgColor);

    // ロック領域内の分割帯を再描画する。
    auto divY = static_cast<int32_t>(config::kCenterY)
              - rift_theme::kDividerHeight / 2;
    target->fillRect(rift_theme::kLockRegionX, divY,
                     rift_theme::kLockRegionW, rift_theme::kDividerHeight,
                     rift_theme::kDividerColor);
}

// ============================================================
// Private: drawRings — リング弧の描画
// ============================================================

void RiftboundRenderer::drawRings(LovyanGFX* target,
                                  uint16_t topColor, uint16_t bottomColor) {
    auto cx = static_cast<int32_t>(config::kCenterX);
    auto cy = static_cast<int32_t>(config::kCenterY);

    // M5GFX (LovyanGFX) の fillArc 角度規約:
    //   0° = 右 (3 時方向)、角度は画面上で時計回りに増加する。
    // 上半円: 180° (左) → 270° (上) → 360°/0° (右)
    // 下半円: 0° (右) → 90° (下) → 180° (左)
    target->fillArc(cx, cy, rift_theme::kRingInnerR, rift_theme::kRingOuterR,
                    180.0f, 360.0f, topColor);
    target->fillArc(cx, cy, rift_theme::kRingInnerR, rift_theme::kRingOuterR,
                    0.0f, 180.0f, bottomColor);
}

// ============================================================
// Private: renderScoreRegion — scoreCanvas_ への得点描画
// ============================================================

void RiftboundRenderer::renderScoreRegion(uint32_t score,
                                          int32_t previewDelta,
                                          uint8_t victoryScore, bool isTop) {
    // 上側プレイヤーは 180 度回転で描画する。
    // M5Canvas::setRotation(2) は描画座標系を 180 度回転させるが、
    // pushSprite は常に生のピクセルバッファを転送する。
    // Phase 0 Step 7 で回転描画の正常動作を実機確認済み（FaB 版と同一方式）。
    scoreCanvas_.setRotation(isTop ? 2 : 0);
    scoreCanvas_.fillScreen(rift_theme::kBgColor);

    int32_t cx = rift_theme::kScoreRegionW / 2;  // 水平中央 = 90

    scoreCanvas_.setTextDatum(middle_center);

    if (previewDelta == 0) {
        // --- 通常表示: 得点値のみ ---
        const bool targetReached = (score >= victoryScore);
        uint16_t textColor =
            targetReached ? rift_theme::kScoreTargetColor
                          : rift_theme::kLifeColor;

        scoreCanvas_.setTextColor(textColor, rift_theme::kBgColor);

        char buf[16];
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(score));
        scoreCanvas_.setTextSize(
            rift_theme::scoreFontSizeForWidth(static_cast<int>(strlen(buf))));

        // 勝利点到達時は "!" を上に表示するため、数字を少し下げる
        int32_t numY = targetReached
            ? (rift_theme::kScoreRegionH / 2 + 10)
            : (rift_theme::kScoreRegionH / 2);
        scoreCanvas_.drawString(buf, cx, numY);

        if (targetReached) {
            // 枠線 + "!" マークで警告する
            // 色だけに頼らない視覚的手がかり (docs/05-ui-ux.md)
            drawScoreTargetBorder(&scoreCanvas_,
                                  rift_theme::kScoreRegionW,
                                  rift_theme::kScoreRegionH);

            scoreCanvas_.setTextSize(rift_theme::kWarningFontSize);
            scoreCanvas_.setTextColor(
                rift_theme::kScoreTargetBorderColor, rift_theme::kBgColor);
            scoreCanvas_.setTextDatum(top_center);
            scoreCanvas_.drawString(
                "!", cx, rift_theme::kScoreTargetBorderThickness + 2);
        }
    } else {
        // --- プレビュー表示: 確定後の値 + 差分 ---
        // 確定後の値を計算する。下限 0 でクランプ（ドメイン層と同じルール）。
        int64_t raw = static_cast<int64_t>(score) + previewDelta;
        uint32_t previewScore = (raw < 0) ? 0u : static_cast<uint32_t>(raw);

        // 確定後の得点値（領域上部）
        // 到達判定は確定後の値に対して行う（現在値ではなく）。
        const bool previewReached = (previewScore >= victoryScore);
        uint16_t lifeColor =
            previewReached ? rift_theme::kScoreTargetColor
                           : rift_theme::kPreviewLifeColor;
        scoreCanvas_.setTextColor(lifeColor, rift_theme::kBgColor);

        char lifeBuf[16];
        snprintf(lifeBuf, sizeof(lifeBuf), "%u",
                 static_cast<unsigned>(previewScore));
        scoreCanvas_.setTextSize(
            rift_theme::scoreFontSizeForWidth(
                static_cast<int>(strlen(lifeBuf))));
        scoreCanvas_.drawString(lifeBuf, cx, rift_theme::kPreviewScoreCY);

        // 差分（領域下部）
        // 色と +/- 符号の両方で方向を示す（色覚差対応）。
        uint16_t deltaColor = (previewDelta < 0)
            ? rift_theme::kDeltaDecreaseColor
            : rift_theme::kDeltaIncreaseColor;
        scoreCanvas_.setTextColor(deltaColor, rift_theme::kBgColor);
        scoreCanvas_.setTextSize(rift_theme::kDeltaFontSize);

        char deltaBuf[16];
        snprintf(deltaBuf, sizeof(deltaBuf), "%+d",
                 static_cast<int>(previewDelta));
        scoreCanvas_.drawString(deltaBuf, cx, rift_theme::kPreviewDeltaCY);

        if (previewReached) {
            // プレビュー中の到達警告は枠線のみ表示する。
            // "!" マークはプレビュー数字・差分テキストと重なるため省略し、
            // 枠線だけで視覚的に警告する。
            drawScoreTargetBorder(&scoreCanvas_,
                                  rift_theme::kScoreRegionW,
                                  rift_theme::kScoreRegionH);
        }
    }
}

// ============================================================
// Private: drawScoreTargetBorder — 勝利点到達警告枠線
// ============================================================

void RiftboundRenderer::drawScoreTargetBorder(LovyanGFX* target,
                                              int32_t w, int32_t h) {
    // 枠線で領域を囲む。色だけでなく枠線の太さ変化で
    // 勝利点到達を視覚的に伝える (docs/05-ui-ux.md の色覚対応方針)。
    for (int32_t i = 0; i < rift_theme::kScoreTargetBorderThickness; ++i) {
        target->drawRect(i, i, w - 2 * i, h - 2 * i,
                         rift_theme::kScoreTargetBorderColor);
    }
}

// ============================================================
// drawSetup — セットアップ画面（全画面転送）
// ============================================================

void RiftboundRenderer::drawSetup(const riftbound::app::RiftboundScreenState& sc) {
    // 毎ループ呼ばれない前提。ScreenState::consumeDirty() が true のときだけ呼ぶ。
    (void)sc;  // 得点は常に 0 で描画するため状態は参照しない

    LovyanGFX* target = selectDrawTarget(canvasReady_, canvas_);

    beginFullScreenDraw(target, rift_theme::kBgColor, lastHoldPercent_);

    // セットアップ画面はリングを描画しないため、弧領域は背景色になる。
    ringTopColor_ = rift_theme::kBgColor;
    ringBottomColor_ = rift_theme::kBgColor;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // --- 下側プレイヤーの得点（通常向き）---
    renderSetupScoreRegion(false);
    scoreCanvas_.pushSprite(target, rift_theme::kScoreTopX, rift_theme::kSetupBottomY);

    // --- 上側プレイヤーの得点（180 度回転）---
    // 上側プレイヤーは対面の相手が見る向きなので 180 度回転して描く。
    renderSetupScoreRegion(true);
    scoreCanvas_.pushSprite(target, rift_theme::kScoreTopX, rift_theme::kSetupTopY);

    // --- 操作説明（画面中央の隙間 y[200, 268] に配置）---
    // ボタンは下側プレイヤーが操作する前提なので、通常向きで描く。
    target->setTextDatum(middle_center);

    target->setTextSize(rift_theme::kSetupHintFontSize);
    target->setTextColor(rift_theme::kHintTextColor, rift_theme::kBgColor);
    target->drawString("Ring: +/- Score", cx, rift_theme::kSetupHintY1);

    // 「Hold B to START」は開始への唯一の導線なので、強調表示する。
    target->setTextSize(rift_theme::kSetupStartFontSize);
    target->setTextColor(rift_theme::kSetupStartColor, rift_theme::kBgColor);
    target->drawString("Hold B to START", cx, rift_theme::kSetupStartY);

    // 全画面転送
    endFullScreenDraw(canvasReady_, canvas_);
}

// ============================================================
// drawMenu — ゲームメニュー画面（全画面転送）
// ============================================================

void RiftboundRenderer::drawMenu(const riftbound::app::RiftboundScreenState& sc,
                                 uint8_t batteryPercent, bool charging) {

    LovyanGFX* target = selectDrawTarget(canvasReady_, canvas_);

    beginFullScreenDraw(target, rift_theme::kBgColor, lastHoldPercent_);

    // メニュー画面はリングを描画しないため、弧領域は背景色になる。
    ringTopColor_ = rift_theme::kBgColor;
    ringBottomColor_ = rift_theme::kBgColor;

    auto cx = static_cast<int32_t>(config::kCenterX);

    bool confirming = sc.awaitingConfirm();

    // --- メニュー項目の描画 ---
    // Riftbound のメニューは SetLife を除く 5 項目。得点は常に 0 から
    // 始まるため開始値を設定する項目が存在しない。
    // 共通 MenuItem enum（FaB との互換で 6 値固定）から SetLife を除き、
    // 表示順を圧縮して並べる。カーソル（menuIndex）は 6 値のままなので、
    // 選択判定は menuItem() の値で行う。
    struct MenuEntry {
        riftbound::app::MenuItem item;
        const char* label;
    };
    static constexpr MenuEntry kMenuEntries[] = {
        { riftbound::app::MenuItem::Resume,         "Resume" },
        { riftbound::app::MenuItem::History,        "History" },
        { riftbound::app::MenuItem::SetSensitivity, "Sensitivity" },
        { riftbound::app::MenuItem::Rematch,        "Rematch" },
        { riftbound::app::MenuItem::About,          "About" },
    };
    static constexpr size_t kMenuEntryCount =
        sizeof(kMenuEntries) / sizeof(kMenuEntries[0]);

    target->setTextDatum(middle_center);

    for (size_t row = 0; row < kMenuEntryCount; ++row) {
        const auto& entry = kMenuEntries[row];
        int32_t itemY = rift_theme::kMenuFirstItemY
                      + static_cast<int32_t>(row) * rift_theme::kMenuItemSpacing;
        bool isSelected = (sc.menuItem() == entry.item);
        bool isConfirmTarget = confirming && (entry.item == sc.confirmTarget());

        char buf[32];

        if (isConfirmTarget) {
            // 確認待ちの対象項目。破壊的操作（Rematch）が
            // 誤操作で実行されないよう、オレンジ色と「?」で警告する。
            target->setTextSize(rift_theme::kMenuItemFontSize);
            target->setTextColor(rift_theme::kMenuConfirmColor,
                                 rift_theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s? <", entry.label);
            target->drawString(buf, cx, itemY);

        } else if (isSelected) {
            // 選択中の項目。色と記号（>）の両方で示す（色覚差対応）。
            target->setTextSize(rift_theme::kMenuItemFontSize);
            target->setTextColor(rift_theme::kMenuSelectedColor,
                                 rift_theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s", entry.label);
            target->drawString(buf, cx, itemY);

        } else {
            // 非選択項目
            target->setTextSize(rift_theme::kMenuItemFontSize);
            target->setTextColor(rift_theme::kMenuNormalColor,
                                 rift_theme::kBgColor);
            target->drawString(entry.label, cx, itemY);
        }
    }

    // --- 確認待ちメッセージ ---
    if (confirming) {
        target->setTextSize(rift_theme::kMenuConfirmFontSize);
        target->setTextColor(rift_theme::kMenuConfirmColor, rift_theme::kBgColor);
        target->setTextDatum(middle_center);
        target->drawString("Hold B to confirm", cx, rift_theme::kMenuConfirmMsgY);
    }

    // --- 操作説明 ---
    target->setTextDatum(middle_center);
    target->setTextSize(rift_theme::kMenuHintFontSize);
    target->setTextColor(rift_theme::kHintTextColor, rift_theme::kBgColor);
    target->drawString("A=Next  B=Select  A+B(hold)=Close", cx, rift_theme::kMenuHintY);

    // --- バッテリー残量表示（電池アイコン ＋ パーセント数値）---
    // 幾何・配色は theme_common.hpp の共通定数を使い、FaB 版と同一の描画を行う。
    {
        bool warning =
            (batteryPercent <= rift_theme::kBatteryWarningThreshold);
        uint16_t color = warning
            ? rift_theme::kBatteryWarningColor
            : rift_theme::kBatteryNormalColor;
        int32_t borderThick = warning
            ? rift_theme::kBatteryBorderWarning
            : rift_theme::kBatteryBorderNormal;

        // パーセント文字列の準備
        char batBuf[8];
        snprintf(batBuf, sizeof(batBuf), "%u%%",
                 static_cast<unsigned>(batteryPercent));

        // テキスト幅を取得する（中央揃え計算に使用）
        target->setTextSize(rift_theme::kBatteryPercentFontSize);
        int32_t textW = target->textWidth(batBuf);

        // 警告 "!" マークの幅（20% 以下のみ）
        int32_t warnW = 0;
        constexpr int32_t kWarnGap = 3;  // "!" とアイコンの間隔
        if (warning) {
            target->setTextSize(rift_theme::kBatteryWarnMarkFontSize);
            warnW = target->textWidth("!") + kWarnGap;
        }

        // アイコン＋テキスト全体の幅を算出し中央揃えする
        int32_t iconTotalW = rift_theme::kBatteryIconBodyW
                           + rift_theme::kBatteryIconTermW;
        int32_t totalW = warnW + iconTotalW
                       + rift_theme::kBatteryIconGap + textW;
        int32_t startX = cx - totalW / 2;

        // --- "!" 警告マーク（20% 以下のみ）---
        if (warning) {
            target->setTextSize(rift_theme::kBatteryWarnMarkFontSize);
            target->setTextColor(rift_theme::kBatteryWarningColor,
                                 rift_theme::kBgColor);
            target->setTextDatum(middle_left);
            target->drawString("!", startX, rift_theme::kBatteryY);
        }

        // --- 電池アイコンの描画 ---
        int32_t iconX = startX + warnW;
        int32_t iconY = rift_theme::kBatteryY
                      - rift_theme::kBatteryIconBodyH / 2;

        // 本体の枠線。警告時は枠を太くする。
        for (int32_t i = 0; i < borderThick; ++i) {
            target->drawRect(
                iconX + i, iconY + i,
                rift_theme::kBatteryIconBodyW - 2 * i,
                rift_theme::kBatteryIconBodyH - 2 * i,
                color);
        }

        // 端子（本体右端の小さな突起。電池の向きを示す）
        int32_t termX = iconX + rift_theme::kBatteryIconBodyW;
        int32_t termY = rift_theme::kBatteryY
                      - rift_theme::kBatteryIconTermH / 2;
        target->fillRect(termX, termY,
                         rift_theme::kBatteryIconTermW,
                         rift_theme::kBatteryIconTermH,
                         color);

        // 残量バー（本体内部を batteryPercent に比例して左から塗りつぶす）
        int32_t fillMaxW = rift_theme::kBatteryIconBodyW
                         - 2 * rift_theme::kBatteryIconPad;
        int32_t fillH = rift_theme::kBatteryIconBodyH
                      - 2 * rift_theme::kBatteryIconPad;
        int32_t fillW = fillMaxW * batteryPercent / 100;
        int32_t fillX = iconX + rift_theme::kBatteryIconPad;
        int32_t fillY = iconY + rift_theme::kBatteryIconPad;

        if (fillW > 0) {
            target->fillRect(fillX, fillY, fillW, fillH, color);
        }

        // --- 充電中インジケータ（稲妻マーク）---
        // isCharging() は満充電時に false を返すため
        // false のときは何も表示しない（docs/01 実測）。
        if (charging) {
            int32_t boltCX = iconX + rift_theme::kBatteryIconBodyW / 2;
            int32_t boltCY = rift_theme::kBatteryY;

            // 上半分: 左上から中央横断線まで広がる三角形
            target->fillTriangle(
                boltCX - 1, boltCY - 3,   // 上端（中心より左）
                boltCX + 2, boltCY,        // 中央右
                boltCX,     boltCY,        // 中央
                rift_theme::kBatteryChargeBoltColor);
            // 下半分: 中央横断線から右下へ収束する三角形
            target->fillTriangle(
                boltCX,     boltCY,        // 中央
                boltCX - 2, boltCY,        // 中央左
                boltCX + 1, boltCY + 3,    // 下端（中心より右）
                rift_theme::kBatteryChargeBoltColor);
        }

        // --- パーセント数値 ---
        int32_t textX = iconX + iconTotalW
                      + rift_theme::kBatteryIconGap;
        target->setTextDatum(middle_left);
        target->setTextSize(rift_theme::kBatteryPercentFontSize);
        target->setTextColor(color, rift_theme::kBgColor);
        target->drawString(batBuf, textX, rift_theme::kBatteryY);
    }

    endFullScreenDraw(canvasReady_, canvas_);
}

// ============================================================
// drawHistory — 履歴画面（全画面転送）
// ============================================================

void RiftboundRenderer::drawHistory(const riftbound::MatchState& state) {
    // 毎ループ呼ばれない前提。画面遷移時に 1 回だけ呼ぶ。

    LovyanGFX* target = selectDrawTarget(canvasReady_, canvas_);

    beginFullScreenDraw(target, rift_theme::kBgColor, lastHoldPercent_);

    // 履歴画面はリングを描画しないため、弧領域は背景色になる
    ringTopColor_ = rift_theme::kBgColor;
    ringBottomColor_ = rift_theme::kBgColor;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // --- タイトル ---
    target->setTextDatum(middle_center);
    target->setTextSize(rift_theme::kHistoryTitleFontSize);
    target->setTextColor(rift_theme::kHistoryTitleColor, rift_theme::kBgColor);
    target->drawString("HISTORY", cx, rift_theme::kHistoryTitleY);

    if (state.history.empty()) {
        // 履歴が空のとき。初回ゲーム開始直後など。
        target->setTextSize(rift_theme::kHistoryItemFontSize);
        target->setTextColor(rift_theme::kHistoryEmptyColor, rift_theme::kBgColor);
        target->drawString("No changes yet", cx,
                           static_cast<int32_t>(config::kCenterY));
    } else {
        // 最新から kHistoryMaxVisible 件を表示する。
        // history[0] が最新、history[size()-1] が最古。
        size_t count = state.history.size();
        size_t visible = (count < rift_theme::kHistoryMaxVisible)
            ? count : rift_theme::kHistoryMaxVisible;

        target->setTextDatum(middle_center);
        target->setTextSize(rift_theme::kHistoryItemFontSize);

        for (size_t i = 0; i < visible; ++i) {
            const auto& entry = state.history[i];
            int32_t y = rift_theme::kHistoryFirstItemY
                      + static_cast<int32_t>(i) * rift_theme::kHistorySpacing;

            // プレイヤー識別にテキストラベル "TOP"/"BTM" を使い、
            // 色だけに頼らない（色覚差対応）。
            const char* tag =
                (entry.player == riftbound::PlayerId::Top) ? "TOP" : "BTM";
            uint16_t color =
                (entry.player == riftbound::PlayerId::Top)
                    ? rift_theme::kHistoryTopColor
                    : rift_theme::kHistoryBottomColor;

            char line[48];
            snprintf(line, sizeof(line), "%s %u>%u (%+d)",
                     tag,
                     static_cast<unsigned>(entry.before),
                     static_cast<unsigned>(entry.after),
                     static_cast<int>(entry.appliedDelta));

            target->setTextColor(color, rift_theme::kBgColor);
            target->drawString(line, cx, y);
        }

        // 表示しきれない履歴がある場合はその旨を示す
        if (visible < count) {
            int32_t moreY = rift_theme::kHistoryFirstItemY
                          + static_cast<int32_t>(visible)
                            * rift_theme::kHistorySpacing;
            target->setTextSize(rift_theme::kHistoryFooterFontSize);
            target->setTextColor(rift_theme::kHistoryEmptyColor,
                                 rift_theme::kBgColor);

            char moreBuf[24];
            snprintf(moreBuf, sizeof(moreBuf), "(%u more)",
                     static_cast<unsigned>(count - visible));
            target->drawString(moreBuf, cx, moreY);
        }
    }

    // --- 操作説明 ---
    target->setTextDatum(middle_center);
    target->setTextSize(rift_theme::kHistoryFooterFontSize);
    target->setTextColor(rift_theme::kHintTextColor, rift_theme::kBgColor);
    target->drawString("B: Back", cx, rift_theme::kHistoryFooterY);

    endFullScreenDraw(canvasReady_, canvas_);
}

// ============================================================
// drawAbout — About 画面（全画面転送）
// ============================================================

void RiftboundRenderer::drawAbout() {
    // 毎ループ呼ばれない前提。画面遷移時に 1 回だけ呼ぶ。

    LovyanGFX* target = selectDrawTarget(canvasReady_, canvas_);

    beginFullScreenDraw(target, rift_theme::kBgColor, lastHoldPercent_);

    // About 画面はリングを描画しないため、弧領域は背景色になる
    ringTopColor_ = rift_theme::kBgColor;
    ringBottomColor_ = rift_theme::kBgColor;

    auto cx = static_cast<int32_t>(config::kCenterX);

    target->setTextDatum(middle_center);

    // --- タイトル ---
    target->setTextSize(rift_theme::kAboutTitleFontSize);
    target->setTextColor(rift_theme::kAboutTitleColor, rift_theme::kBgColor);
    target->drawString("Score Counter", cx, rift_theme::kAboutTitleY);

    // --- バージョン ---
    // rift_theme::kFirmwareVersion を表示する。リリース時に riftbound_theme.hpp で更新する。
    target->setTextSize(rift_theme::kAboutVersionFontSize);
    target->setTextColor(rift_theme::kAboutVersionColor, rift_theme::kBgColor);
    char verBuf[24];
    snprintf(verBuf, sizeof(verBuf), "v%s", rift_theme::kFirmwareVersion);
    target->drawString(verBuf, cx, rift_theme::kAboutVersionY);

    // --- 操作説明 ---
    target->setTextSize(rift_theme::kAboutFooterFontSize);
    target->setTextColor(rift_theme::kHintTextColor, rift_theme::kBgColor);
    target->drawString("B: Back", cx, rift_theme::kAboutFooterY);

    endFullScreenDraw(canvasReady_, canvas_);
}

// ============================================================
// drawSensitivity — 感度設定画面（全画面転送）
// ============================================================

void RiftboundRenderer::drawSensitivity(
    const riftbound::app::RiftboundScreenState& sc) {
    // 毎ループ呼ばれない前提。画面遷移時やプリセット変更時に呼ぶ。

    LovyanGFX* target = selectDrawTarget(canvasReady_, canvas_);

    beginFullScreenDraw(target, rift_theme::kBgColor, lastHoldPercent_);

    // 感度設定画面はリングを描画しないため、弧領域は背景色になる
    ringTopColor_ = rift_theme::kBgColor;
    ringBottomColor_ = rift_theme::kBgColor;

    auto cx = static_cast<int32_t>(config::kCenterX);

    const uint8_t sensIndex = sc.sensitivityIndex();
    const uint8_t sensValue = config::kSensitivityPresets[sensIndex];

    target->setTextDatum(middle_center);

    // --- タイトル ---
    target->setTextSize(rift_theme::kSensitivityTitleFontSize);
    target->setTextColor(rift_theme::kSensitivityTitleColor, rift_theme::kBgColor);
    target->drawString("SENSITIVITY", cx, rift_theme::kSensitivityTitleY);

    // --- 現在値（大きな数字）---
    target->setTextSize(rift_theme::kSensitivityValueFontSize);
    target->setTextColor(rift_theme::kSensitivityValueColor, rift_theme::kBgColor);
    char valBuf[8];
    snprintf(valBuf, sizeof(valBuf), "%u", static_cast<unsigned>(sensValue));
    target->drawString(valBuf, cx, rift_theme::kSensitivityValueY);

    // --- ラベル ---
    target->setTextSize(rift_theme::kSensitivityLabelFontSize);
    target->setTextColor(rift_theme::kSensitivityLabelColor, rift_theme::kBgColor);
    target->drawString("score / rev", cx, rift_theme::kSensitivityLabelY);

    // --- プリセット一覧（角括弧で選択中を示す。色覚差対応）---
    target->setTextSize(rift_theme::kSensitivityPresetFontSize);
    for (size_t i = 0; i < config::kSensitivityPresetCount; ++i) {
        bool isCurrent = (i == sensIndex);
        char label[8];
        snprintf(label, sizeof(label), "%s%u%s",
                 isCurrent ? "[" : " ",
                 static_cast<unsigned>(config::kSensitivityPresets[i]),
                 isCurrent ? "]" : " ");

        target->setTextColor(
            isCurrent ? rift_theme::kSetupPresetActiveColor
                      : rift_theme::kSetupPresetInactiveColor,
            rift_theme::kBgColor);

        // 3 項目を中央揃えで横に配置する。
        int32_t offsetX = static_cast<int32_t>(i) * 60
                        - static_cast<int32_t>(config::kSensitivityPresetCount - 1) * 30;
        target->drawString(label, cx + offsetX, rift_theme::kSensitivityPresetY);
    }

    // --- 操作説明 ---
    target->setTextSize(rift_theme::kSensitivityHintFontSize);
    target->setTextColor(rift_theme::kHintTextColor, rift_theme::kBgColor);
    target->drawString("A: Change  B: OK", cx, rift_theme::kSensitivityHintY);

    endFullScreenDraw(canvasReady_, canvas_);
}

// ============================================================
// Private: renderSetupScoreRegion — セットアップ画面の得点描画
// ============================================================

void RiftboundRenderer::renderSetupScoreRegion(bool isTop) {
    // scoreCanvas_ に得点数字（開始時は常に 0）と "SCORE" ラベルを描画する。
    // Riftbound の得点は常に 0 から始まるため、FaB 版のような
    // 外周スライドによる開始値調整・プリセット一致表示は存在しない。
    scoreCanvas_.setRotation(isTop ? 2 : 0);
    scoreCanvas_.fillScreen(rift_theme::kBgColor);

    int32_t cx = rift_theme::kScoreRegionW / 2;  // 90

    // --- 得点数字（常に 0）---
    scoreCanvas_.setTextDatum(middle_center);
    scoreCanvas_.setTextColor(rift_theme::kLifeColor, rift_theme::kBgColor);
    scoreCanvas_.setTextSize(rift_theme::kScoreFontSize);
    scoreCanvas_.drawString("0", cx, rift_theme::kSetupScoreNumCY);

    // --- "SCORE" ラベル ---
    scoreCanvas_.setTextSize(rift_theme::kSetupScoreLabelFontSize);
    scoreCanvas_.setTextColor(rift_theme::kHintTextColor, rift_theme::kBgColor);
    scoreCanvas_.drawString("SCORE", cx, rift_theme::kSetupScoreLabelCY);
}

// ============================================================
// drawHoldProgress — 長押し進捗の部分再描画
// ============================================================

void RiftboundRenderer::drawHoldProgress(uint8_t percent) {
    // 長押し進捗を差分描画で表示する（FaB 版と同一のアルゴリズム）。
    // どの画面の上にも重ねて描画でき、percent=0 で元のリング表示を復元する。
    // 差分のみ描画することで、既に塗った部分に触れずちらつきを解消する。

    auto cx = static_cast<int32_t>(config::kCenterX);
    auto cy = static_cast<int32_t>(config::kCenterY);

    if (percent == 0) {
        // --- 進捗表示を消去し、元の表示を復元する ---
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                            180.0f, 360.0f, ringTopColor_);
            canvas_.fillArc(cx, cy,
                            rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                            0.0f, 180.0f, ringBottomColor_);
        }
        M5.Display.fillArc(cx, cy,
                           rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                           180.0f, 360.0f, ringTopColor_);
        M5.Display.fillArc(cx, cy,
                           rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                           0.0f, 180.0f, ringBottomColor_);
        lastHoldPercent_ = 0;
        return;
    }

    uint8_t pct = (percent > 100) ? 100 : percent;

    // 同じ値のときは何も描かない（描画層でも二重に守る）。
    if (pct == lastHoldPercent_) {
        return;
    }

    // --- 弧の差分描画 ---
    // M5GFX fillArc 角度規約: 0°=右(3時), 時計回りに増加。270°=上端(12時)。
    // 進捗は上端 (270°) から時計回りに伸びる。

    if (lastHoldPercent_ == 0) {
        // 進捗の開始: トラック弧を全周 1 回だけ描き、0→pct の進捗弧を塗る。
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                            0.0f, 360.0f, rift_theme::kHoldArcTrackColor);
        }
        M5.Display.fillArc(cx, cy,
                           rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                           0.0f, 360.0f, rift_theme::kHoldArcTrackColor);

        // 0→pct の進捗弧を描画する
        float endAngle = 270.0f + 360.0f * pct / 100;
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                            270.0f, endAngle, rift_theme::kHoldArcColor);
        }
        M5.Display.fillArc(cx, cy,
                           rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                           270.0f, endAngle, rift_theme::kHoldArcColor);

    } else if (pct > lastHoldPercent_) {
        // 増加: 前回の角度から今回の角度までの差分だけを進捗色で塗る。
        float fromAngle = 270.0f + 360.0f * lastHoldPercent_ / 100;
        float toAngle   = 270.0f + 360.0f * pct / 100;
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                            fromAngle, toAngle, rift_theme::kHoldArcColor);
        }
        M5.Display.fillArc(cx, cy,
                           rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                           fromAngle, toAngle, rift_theme::kHoldArcColor);

    } else {
        // 減少 (pct < lastHoldPercent_): 通常は起きないが保険として対応。
        // 減った範囲だけをトラック色で塗り戻す。
        float fromAngle = 270.0f + 360.0f * pct / 100;
        float toAngle   = 270.0f + 360.0f * lastHoldPercent_ / 100;
        if (canvasReady_) {
            canvas_.fillArc(cx, cy,
                            rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                            fromAngle, toAngle, rift_theme::kHoldArcTrackColor);
        }
        M5.Display.fillArc(cx, cy,
                           rift_theme::kHoldArcInnerR, rift_theme::kHoldArcOuterR,
                           fromAngle, toAngle, rift_theme::kHoldArcTrackColor);
    }

    lastHoldPercent_ = pct;
}

}  // namespace counter::ui
