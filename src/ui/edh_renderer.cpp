// EDH（統率者戦）ファームウェアの描画層の実装。
//
// 4 扇形のレイアウト描画を行う。各扇形は矩形キャンバスで近似する。
//
// 【描画方式】
// 1. 各プレイヤーの扇形を sectorCanvas_ (234x234) に描画する
// 2. setRotation() でプレイヤーの着席方向へ回転する
// 3. pushSprite() で画面の対応象限に転送する
//
// 【部分再描画方針】
// 全画面再描画は実測 22.4 FPS で遅い。変化した扇形のみを再描画する。
// sectorCanvas_ の転送サイズは 234*234*2 = 109,512 bytes で、
// 全画面 (468*468*2 = 438,048 bytes) の約 1/4。

#include "ui/edh_renderer.hpp"
#include "ui/edh_theme.hpp"
#include "app_config.hpp"
#include "domain/edh_life_change.hpp"
#include "domain/edh_life_service.hpp"

#include <cstdio>
#include <cstring>

namespace counter::ui {

namespace {

// プレイヤーラベル
constexpr const char* kPlayerLabel[edh::kPlayerCount] = {
    "P1", "P2", "P3", "P4"
};

// 各扇形の画面上の左上座標（pushSprite の転送先）
// X 字分割を矩形近似で実装するため、画面を 2x2 の象限に分割する。
// 各プレイヤーの扇形（三角形）は 1 つの象限にマッピングする。
// 三角形の頂点（画面中心）が象限の角に来るよう配置する。
//
//   P1(上)→左上象限(0,0):     頂点は象限の右下角（=画面中心）
//   P2(右)→右上象限(234,0):   頂点は象限の左下角（=画面中心）
//   P3(下)→右下象限(234,234): 頂点は象限の左上角（=画面中心）
//   P4(左)→左下象限(0,234):   頂点は象限の右上角（=画面中心）
//
// 各象限の矩形は 234x234 で、扇形の三角形はその対角線で半分を占める。
// 残り半分には隣接プレイヤーの領域が侵入するが、分割線で視覚的に隠す。
//
// 実機調整前提: 回転 + pushSprite の結果が期待通りかは実機確認が必要。
constexpr int32_t kSectorX[edh::kPlayerCount] = {0, 234, 234, 0};
constexpr int32_t kSectorY[edh::kPlayerCount] = {0, 0, 234, 234};

// P3(下) は pushSprite 先を (0, 234) にしないと位置がずれる。
// ただし rot=0 なので描画は「そのまま」下半分の下象限に対応する。
// 実際のマッピング:
//   P1(rot2): キャンバス内で 180° 回転して描画 → (0,0) に転送
//   P2(rot3): キャンバス内で 270° 回転して描画 → (234,0) に転送
//   P3(rot0): キャンバス内で回転なしで描画 → (0,234) に転送
//   P4(rot1): キャンバス内で 90° 回転して描画 → (0,234) に転送
//
// 修正: 各プレイヤーの転送先は回転後のピクセル配置に依存する。
// M5Canvas::setRotation() はピクセルの論理→物理マッピングを変更するが、
// pushSprite は常に物理バッファの左上から転送する。
// したがって、回転によらず転送先座標は「扇形が画面上で占める矩形の左上」。

}  // namespace

// ============================================================
// begin — Canvas の確保
// ============================================================

void EdhRenderer::begin() {
    // 全画面 Canvas を PSRAM 上に確保する
    canvas_.setPsram(true);
    canvas_.setColorDepth(16);
    void* mainBuf = canvas_.createSprite(
        config::kDisplayWidth, config::kDisplayHeight);

    if (mainBuf == nullptr) {
        Serial.println(
            "[EdhRenderer] ERROR: PSRAM canvas allocation failed.");
        canvasReady_ = false;
    } else {
        canvasReady_ = true;
    }

    // 扇形キャンバスは SRAM 優先で確保する（部分再描画用）
    sectorCanvas_.setPsram(false);
    sectorCanvas_.setColorDepth(16);
    void* sectorBuf = sectorCanvas_.createSprite(
        edh_theme::kSectorCanvasW, edh_theme::kSectorCanvasH);

    if (sectorBuf == nullptr) {
        // SRAM 不足時は PSRAM にフォールバック
        sectorCanvas_.setPsram(true);
        sectorBuf = sectorCanvas_.createSprite(
            edh_theme::kSectorCanvasW, edh_theme::kSectorCanvasH);
        if (sectorBuf == nullptr) {
            Serial.println(
                "[EdhRenderer] ERROR: sector canvas allocation failed.");
        }
    }
}

// ============================================================
// drawAll — 全画面描画
// ============================================================

void EdhRenderer::drawAll(const edh::MatchState& state,
                          const edh::app::EdhScreenState& screenState) {
    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(edh_theme::kBgColor);
    lastHoldPercent_ = 0;

    // 4 プレイヤーの扇形を描画する
    for (uint8_t i = 0; i < edh::kPlayerCount; ++i) {
        const uint8_t rot = edh_theme::kPlayerRotation[i];

        // ビュー状態に応じて描画内容を切り替える
        if (screenState.playerView(i) == edh::app::PlayerView::CmdDamageView) {
            renderCmdDamageView(state, screenState, i, 0, rot);
        } else {
            renderLifeView(state, i, 0, rot);
        }

        pushSectorToScreen(target, i);
    }

    // 分割線を描画して扇形の境界を明示する
    drawDividers(target);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawPlayerSector — 1 プレイヤーの扇形のみ再描画
// ============================================================

void EdhRenderer::drawPlayerSector(const edh::MatchState& state,
                                    const edh::app::EdhScreenState& screenState,
                                    uint8_t playerIndex,
                                    int32_t previewDelta,
                                    bool isCommanderDmg) {
    const uint8_t rot = edh_theme::kPlayerRotation[playerIndex];

    if (isCommanderDmg ||
        screenState.playerView(playerIndex) == edh::app::PlayerView::CmdDamageView) {
        renderCmdDamageView(state, screenState, playerIndex, previewDelta, rot);
    } else {
        renderLifeView(state, playerIndex, previewDelta, rot);
    }

    // 部分転送: 全画面キャンバスを使わず直接ディスプレイに転送する
    pushSectorToScreen(&M5.Display, playerIndex);
}

// ============================================================
// renderLifeView — ライフビューの描画
// ============================================================

void EdhRenderer::renderLifeView(const edh::MatchState& state,
                                  uint8_t playerIndex,
                                  int32_t previewDelta,
                                  uint8_t rotation) {
    sectorCanvas_.setRotation(rotation);
    sectorCanvas_.fillScreen(edh_theme::kBgColor);

    const auto& player = state.players[playerIndex];
    const bool defeated = edh::isDefeated(state, playerIndex);

    // 背景色: 敗北時はダークグレー
    if (defeated) {
        sectorCanvas_.fillScreen(edh_theme::kDefeatedBgColor);
    }

    // キャンバスの論理サイズ（回転後）
    const int32_t cw = sectorCanvas_.width();
    const int32_t ch = sectorCanvas_.height();
    const int32_t cx = cw / 2;

    // プレイヤーラベル（左上に小さく）
    sectorCanvas_.setTextDatum(top_left);
    sectorCanvas_.setTextSize(edh_theme::kPlayerLabelSize);
    sectorCanvas_.setTextColor(edh_theme::kPlayerColor[playerIndex],
                                defeated ? edh_theme::kDefeatedBgColor
                                         : edh_theme::kBgColor);
    sectorCanvas_.drawString(kPlayerLabel[playerIndex], 4, 4);

    // ライフ数字
    char lifeBuf[16];
    const uint32_t life = player.life;
    snprintf(lifeBuf, sizeof(lifeBuf), "%u",
             static_cast<unsigned>(life));

    const int32_t numChars = static_cast<int32_t>(strlen(lifeBuf));
    const float fontSize = (numChars >= 4)
        ? edh_theme::kLifeFontSizeSmall
        : edh_theme::kLifeFontSize;

    sectorCanvas_.setTextDatum(middle_center);

    if (previewDelta == 0) {
        // 通常表示
        const uint16_t textColor = defeated
            ? edh_theme::kDefeatedTextColor
            : edh_theme::kLifeColor;
        const uint16_t bgColor = defeated
            ? edh_theme::kDefeatedBgColor
            : edh_theme::kBgColor;

        sectorCanvas_.setTextSize(fontSize);
        sectorCanvas_.setTextColor(textColor, bgColor);
        sectorCanvas_.drawString(lifeBuf, cx, ch / 2);

        // 敗北表示
        if (defeated) {
            sectorCanvas_.setTextSize(1.0f);
            sectorCanvas_.setTextColor(edh_theme::kDefeatedTextColor,
                                        edh_theme::kDefeatedBgColor);
            sectorCanvas_.drawString("DEFEATED", cx, ch - 20);
        }
    } else {
        // プレビュー表示
        const uint16_t bgColor = defeated
            ? edh_theme::kDefeatedBgColor
            : edh_theme::kBgColor;

        sectorCanvas_.setTextSize(fontSize);
        sectorCanvas_.setTextColor(edh_theme::kPreviewLifeColor, bgColor);
        sectorCanvas_.drawString(lifeBuf, cx, ch / 2 - 15);

        // 差分表示
        char deltaBuf[16];
        if (previewDelta > 0) {
            snprintf(deltaBuf, sizeof(deltaBuf), "+%d",
                     static_cast<int>(previewDelta));
        } else {
            snprintf(deltaBuf, sizeof(deltaBuf), "%d",
                     static_cast<int>(previewDelta));
        }

        const uint16_t deltaColor = (previewDelta > 0)
            ? edh_theme::kDeltaIncreaseColor
            : edh_theme::kDeltaDecreaseColor;

        sectorCanvas_.setTextSize(edh_theme::kDeltaFontSize);
        sectorCanvas_.setTextColor(deltaColor, bgColor);
        sectorCanvas_.drawString(deltaBuf, cx, ch / 2 + 25);
    }
}

// ============================================================
// renderCmdDamageView — 統率者ダメージビューの描画
// ============================================================

void EdhRenderer::renderCmdDamageView(const edh::MatchState& state,
                                       const edh::app::EdhScreenState& screenState,
                                       uint8_t playerIndex,
                                       int32_t previewDelta,
                                       uint8_t rotation) {
    sectorCanvas_.setRotation(rotation);
    sectorCanvas_.fillScreen(edh_theme::kBgColor);

    const auto& player = state.players[playerIndex];
    const bool defeated = edh::isDefeated(state, playerIndex);

    if (defeated) {
        sectorCanvas_.fillScreen(edh_theme::kDefeatedBgColor);
    }

    const int32_t cw = sectorCanvas_.width();
    const int32_t ch = sectorCanvas_.height();
    const int32_t cx = cw / 2;

    const uint16_t bgColor = defeated
        ? edh_theme::kDefeatedBgColor
        : edh_theme::kBgColor;

    // プレイヤーラベル
    sectorCanvas_.setTextDatum(top_left);
    sectorCanvas_.setTextSize(edh_theme::kPlayerLabelSize);
    sectorCanvas_.setTextColor(edh_theme::kPlayerColor[playerIndex], bgColor);
    sectorCanvas_.drawString(kPlayerLabel[playerIndex], 4, 4);

    // "CMD DMG" タイトル
    sectorCanvas_.setTextDatum(top_center);
    sectorCanvas_.setTextSize(1.0f);
    sectorCanvas_.setTextColor(edh_theme::kHintTextColor, bgColor);
    sectorCanvas_.drawString("CMD DMG", cx, 4);

    // 3 対戦相手のダメージ一覧を表示する
    const uint8_t selectedSource = screenState.selectedSource();
    int32_t itemY = 40;  // 一覧の開始 y（実機調整前提）
    constexpr int32_t itemSpacing = 45;  // 項目間隔（実機調整前提）

    uint8_t opponents[3];
    uint8_t opCount = 0;
    for (uint8_t i = 0; i < edh::kPlayerCount; ++i) {
        if (i != playerIndex) {
            opponents[opCount++] = i;
        }
    }

    for (uint8_t oi = 0; oi < opCount; ++oi) {
        const uint8_t srcIdx = opponents[oi];
        const uint8_t dmg = player.commanderDamageFrom[srcIdx];
        const bool isSelected = (srcIdx == selectedSource);

        // 被弾元のラベルとカラー
        sectorCanvas_.setTextDatum(middle_left);
        sectorCanvas_.setTextSize(edh_theme::kCmdDmgLabelFontSize);

        if (isSelected) {
            // 選択中: 白ハイライト + ">" 記号
            sectorCanvas_.setTextColor(
                edh_theme::kCmdDmgSelectedColor, bgColor);
            char label[16];
            snprintf(label, sizeof(label), "> %s", kPlayerLabel[srcIdx]);
            sectorCanvas_.drawString(label, 8, itemY);
        } else {
            sectorCanvas_.setTextColor(
                edh_theme::kPlayerColor[srcIdx], bgColor);
            sectorCanvas_.drawString(kPlayerLabel[srcIdx], 16, itemY);
        }

        // ダメージ数値
        char dmgBuf[16];
        // プレビュー中の場合、選択中の被弾元のダメージにプレビューを反映する
        int32_t displayDmg = static_cast<int32_t>(dmg);
        if (isSelected && previewDelta != 0) {
            displayDmg += previewDelta;
            if (displayDmg < 0) displayDmg = 0;
            if (displayDmg > 99) displayDmg = 99;
        }
        snprintf(dmgBuf, sizeof(dmgBuf), "%d",
                 static_cast<int>(displayDmg));

        sectorCanvas_.setTextDatum(middle_right);
        sectorCanvas_.setTextSize(edh_theme::kCmdDmgFontSize);

        // 21 以上は警告色
        uint16_t dmgColor;
        if (displayDmg >= 21) {
            dmgColor = edh_theme::kCmdDmgWarningColor;
        } else if (isSelected) {
            dmgColor = edh_theme::kCmdDmgSelectedColor;
        } else {
            dmgColor = edh_theme::kCmdDmgNormalColor;
        }
        sectorCanvas_.setTextColor(dmgColor, bgColor);
        sectorCanvas_.drawString(dmgBuf, cw - 8, itemY);

        itemY += itemSpacing;
    }

    // 被弾元未選択時のヒント
    if (selectedSource == edh::kSourceNone) {
        sectorCanvas_.setTextDatum(bottom_center);
        sectorCanvas_.setTextSize(edh_theme::kCmdDmgHintFontSize);
        sectorCanvas_.setTextColor(edh_theme::kHintTextColor, bgColor);
        sectorCanvas_.drawString("Tap opponent", cx, ch - 8);
    }

    // プレビューの差分表示（選択中の被弾元がある場合）
    if (selectedSource != edh::kSourceNone && previewDelta != 0) {
        char deltaBuf[16];
        if (previewDelta > 0) {
            snprintf(deltaBuf, sizeof(deltaBuf), "+%d",
                     static_cast<int>(previewDelta));
        } else {
            snprintf(deltaBuf, sizeof(deltaBuf), "%d",
                     static_cast<int>(previewDelta));
        }

        const uint16_t deltaColor = (previewDelta > 0)
            ? edh_theme::kDeltaDecreaseColor   // 統率者ダメージ増 = ライフ減
            : edh_theme::kDeltaIncreaseColor;   // 統率者ダメージ減 = ライフ増

        sectorCanvas_.setTextDatum(bottom_center);
        sectorCanvas_.setTextSize(edh_theme::kDeltaFontSize);
        sectorCanvas_.setTextColor(deltaColor, bgColor);
        sectorCanvas_.drawString(deltaBuf, cx, ch - 8);
    }
}

// ============================================================
// pushSectorToScreen — 扇形キャンバスの転送
// ============================================================

void EdhRenderer::pushSectorToScreen(LovyanGFX* target, uint8_t playerIndex) {
    // setRotation() で描画座標系を回転させた結果のピクセルバッファを、
    // 画面の対応象限に転送する。転送先座標は kSectorX/kSectorY で定義済み。
    // 実機調整前提: 回転 + 転送先の組み合わせが期待通りかは実機確認が必要。
    sectorCanvas_.pushSprite(target,
                              kSectorX[playerIndex],
                              kSectorY[playerIndex]);
}

// ============================================================
// drawDividers — 対角分割線
// ============================================================

void EdhRenderer::drawDividers(LovyanGFX* target) {
    // X 字の対角線を描画する。(0,0)-(468,468) と (468,0)-(0,468)。
    // drawLine は 1px 線なので、太さ kDividerWidth に応じて複数本描く。
    const int32_t w = config::kDisplayWidth;
    const int32_t h = config::kDisplayHeight;

    for (int32_t d = 0; d < edh_theme::kDividerWidth; ++d) {
        // 右下がりの対角線 (\)
        target->drawLine(d, 0, w - 1 + d, h - 1, edh_theme::kDividerColor);
        target->drawLine(0, d, w - 1, h - 1 + d, edh_theme::kDividerColor);

        // 右上がりの対角線 (/)
        target->drawLine(w - 1 - d, 0, 0 - d, h - 1, edh_theme::kDividerColor);
        target->drawLine(w - 1, d, 0, h - 1 + d, edh_theme::kDividerColor);
    }
}

// ============================================================
// drawLockState — タッチロック表示
// ============================================================

void EdhRenderer::drawLockState(const edh::MatchState& state) {
    // FaB 版と同じ方式: 全画面 pushSprite (44.6 ms) を避け、
    // キャンバスとディスプレイの両方に直接描画する。
    // fillRect / drawArc / drawString は対象ピクセルのみを書き換えるため、
    // ロック領域の小さな描画で済む。
    if (state.touchLocked) {
        if (canvasReady_) {
            drawLockIcon(&canvas_);
        }
        drawLockIcon(&M5.Display);
    } else {
        if (canvasReady_) {
            clearLockRegion(&canvas_);
        }
        clearLockRegion(&M5.Display);
    }
}

// ============================================================
// drawLockIcon / clearLockRegion
// ============================================================

void EdhRenderer::drawLockIcon(LovyanGFX* target) {
    const int32_t cx = config::kDisplayWidth / 2;
    const int32_t cy = config::kDisplayHeight / 2;

    // 背景矩形
    target->fillRect(edh_theme::kLockRegionX, edh_theme::kLockRegionY,
                     edh_theme::kLockRegionW, edh_theme::kLockRegionH,
                     edh_theme::kBgColor);

    // 鍵本体（矩形）
    const int32_t bodyW = 16;
    const int32_t bodyH = 12;
    target->fillRect(cx - bodyW / 2, cy - 2,
                     bodyW, bodyH, edh_theme::kLockIconColor);

    // 鍵の弧（半円）
    target->drawArc(cx, cy - 2, 8, 5, 180, 360,
                    edh_theme::kLockIconColor);

    // "LOCK" テキスト
    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kLockTextSize);
    target->setTextColor(edh_theme::kLockIconColor, edh_theme::kBgColor);
    target->drawString("LOCK", cx, cy + 15);
}

void EdhRenderer::clearLockRegion(LovyanGFX* target) {
    // ロック領域を背景色で塗りつぶす。
    // 分割線は drawDividers で再描画される前提。
    target->fillRect(edh_theme::kLockRegionX, edh_theme::kLockRegionY,
                     edh_theme::kLockRegionW, edh_theme::kLockRegionH,
                     edh_theme::kBgColor);
}

// ============================================================
// drawSetup — セットアップ画面
// ============================================================

void EdhRenderer::drawSetup(const edh::app::EdhScreenState& sc) {
    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(edh_theme::kBgColor);
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // 初期ライフの表示
    char lifeBuf[16];
    snprintf(lifeBuf, sizeof(lifeBuf), "%u",
             static_cast<unsigned>(sc.setupLife()));

    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kSetupLifeFontSize);
    target->setTextColor(edh_theme::kLifeColor, edh_theme::kBgColor);
    target->drawString(lifeBuf, cx, edh_theme::kSetupLifeY);

    // 操作説明
    target->setTextSize(edh_theme::kSetupHintFontSize);
    target->setTextColor(edh_theme::kHintTextColor, edh_theme::kBgColor);
    target->drawString("Ring: +/- Life", cx, edh_theme::kSetupHintY1);
    target->drawString("A: Life presets", cx, edh_theme::kSetupHintY2);

    // "Hold B to START"（強調）
    target->setTextSize(edh_theme::kSetupStartFontSize);
    target->setTextColor(edh_theme::kSetupStartColor, edh_theme::kBgColor);
    target->drawString("Hold B to START", cx, edh_theme::kSetupHintY3);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawMenu — メニュー画面
// ============================================================

void EdhRenderer::drawMenu(const edh::app::EdhScreenState& sc,
                            uint8_t batteryPercent, bool charging) {
    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(edh_theme::kBgColor);
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // バッテリーアイコン
    drawBatteryIcon(target, batteryPercent, charging);

    // メニュー項目名（EDH 版は FaB 版と同じ 6 項目構成）
    static constexpr const char* kItemNames[] = {
        "Resume", "History", "Set Life", "Sensitivity", "Rematch", "About"
    };

    target->setTextDatum(middle_center);

    const bool confirming = sc.awaitingConfirm();

    for (uint8_t i = 0; i < edh::app::kMenuItemCount; ++i) {
        int32_t itemY = edh_theme::kMenuFirstItemY
                      + static_cast<int32_t>(i) * edh_theme::kMenuItemSpacing;
        bool isSelected = (i == sc.menuIndex());
        auto item = static_cast<edh::app::MenuItem>(i);
        bool isConfirmTarget = confirming && (item == sc.confirmTarget());

        char buf[32];

        if (isConfirmTarget) {
            // 確認待ちの対象項目: オレンジ色 + "?" で警告する
            target->setTextSize(edh_theme::kMenuItemFontSize);
            target->setTextColor(edh_theme::kMenuConfirmColor, edh_theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s? <", kItemNames[i]);
            target->drawString(buf, cx, itemY);
        } else if (isSelected) {
            // 選択中: シアン + ">" マーカー
            target->setTextSize(edh_theme::kMenuItemFontSize);
            target->setTextColor(edh_theme::kMenuSelectedColor, edh_theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s", kItemNames[i]);
            target->drawString(buf, cx, itemY);
        } else {
            // 非選択: グレー
            target->setTextSize(edh_theme::kMenuItemFontSize);
            target->setTextColor(edh_theme::kMenuNormalColor, edh_theme::kBgColor);
            target->drawString(kItemNames[i], cx, itemY);
        }
    }

    // 確認メッセージ
    if (confirming) {
        target->setTextSize(edh_theme::kMenuConfirmFontSize);
        target->setTextColor(edh_theme::kMenuConfirmColor, edh_theme::kBgColor);
        target->setTextDatum(middle_center);
        target->drawString("Hold B to confirm", cx, edh_theme::kMenuConfirmMsgY);
    }

    // 操作ヒント
    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kMenuHintFontSize);
    target->setTextColor(edh_theme::kHintTextColor, edh_theme::kBgColor);
    target->drawString("A=Next  B=Select  A+B(hold)=Close", cx,
                       edh_theme::kMenuHintY);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawBatteryIcon — バッテリーアイコン（メニュー画面）
// ============================================================

void EdhRenderer::drawBatteryIcon(LovyanGFX* target,
                                   uint8_t percent, bool charging) {
    // FaB 版 renderer.cpp の drawMenu 内バッテリー描画と同じロジック。
    // 詳細は省略し、必要最小限の表示を行う。
    const auto cx = static_cast<int32_t>(config::kCenterX);
    const bool warning = (percent <= edh_theme::kBatteryWarningThreshold);
    const uint16_t color = warning
        ? edh_theme::kBatteryWarningColor
        : edh_theme::kBatteryNormalColor;

    // パーセント表示
    char batBuf[8];
    snprintf(batBuf, sizeof(batBuf), "%u%%", static_cast<unsigned>(percent));

    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kBatteryPercentFontSize);
    target->setTextColor(color, edh_theme::kBgColor);
    target->drawString(batBuf, cx, edh_theme::kBatteryY);
}

// ============================================================
// drawHistory — 履歴画面
// ============================================================

void EdhRenderer::drawHistory(const edh::MatchState& state) {
    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(edh_theme::kBgColor);
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // タイトル
    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kHistoryTitleFontSize);
    target->setTextColor(edh_theme::kHistoryTitleColor, edh_theme::kBgColor);
    target->drawString("HISTORY", cx, edh_theme::kHistoryTitleY);

    // 履歴項目
    const size_t count = state.history.size();
    if (count == 0) {
        target->setTextSize(edh_theme::kHistoryItemFontSize);
        target->setTextColor(edh_theme::kHistoryEmptyColor, edh_theme::kBgColor);
        target->drawString("No history", cx, edh_theme::kHistoryFirstItemY);
    } else {
        const size_t visible = (count < edh_theme::kHistoryMaxVisible)
            ? count : edh_theme::kHistoryMaxVisible;

        for (size_t i = 0; i < visible; ++i) {
            const auto& entry = state.history[i];
            int32_t itemY = edh_theme::kHistoryFirstItemY
                          + static_cast<int32_t>(i) * edh_theme::kHistorySpacing;

            // 統率者ダメージ操作の場合: "P3 <- P2  +5" 形式
            // 通常ライフ操作の場合: "P1  -3" 形式
            char histBuf[32];
            if (entry.sourceIndex != edh::kSourceNone) {
                // 統率者ダメージ操作
                snprintf(histBuf, sizeof(histBuf), "P%d <- P%d  %s%d",
                         entry.playerIndex + 1,
                         entry.sourceIndex + 1,
                         (entry.delta > 0) ? "+" : "",
                         static_cast<int>(entry.delta));
            } else {
                // 通常ライフ操作
                snprintf(histBuf, sizeof(histBuf), "P%d  %s%d",
                         entry.playerIndex + 1,
                         (entry.delta > 0) ? "+" : "",
                         static_cast<int>(entry.delta));
            }

            // プレイヤーのテーマカラーで表示
            target->setTextSize(edh_theme::kHistoryItemFontSize);
            target->setTextColor(
                edh_theme::kPlayerColor[entry.playerIndex],
                edh_theme::kBgColor);
            target->drawString(histBuf, cx, itemY);
        }
    }

    // フッター
    target->setTextSize(edh_theme::kHistoryFooterFontSize);
    target->setTextColor(edh_theme::kHintTextColor, edh_theme::kBgColor);
    target->drawString("B: Back", cx, edh_theme::kHistoryFooterY);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawAbout — About 画面
// ============================================================

void EdhRenderer::drawAbout() {
    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(edh_theme::kBgColor);
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    target->setTextDatum(middle_center);

    target->setTextSize(edh_theme::kAboutTitleFontSize);
    target->setTextColor(edh_theme::kAboutTitleColor, edh_theme::kBgColor);
    target->drawString("EDH Counter", cx, edh_theme::kAboutTitleY);

    char verBuf[16];
    snprintf(verBuf, sizeof(verBuf), "v%s", edh_theme::kFirmwareVersion);

    target->setTextSize(edh_theme::kAboutVersionFontSize);
    target->setTextColor(edh_theme::kAboutVersionColor, edh_theme::kBgColor);
    target->drawString(verBuf, cx, edh_theme::kAboutVersionY);

    target->setTextSize(edh_theme::kAboutFooterFontSize);
    target->setTextColor(edh_theme::kHintTextColor, edh_theme::kBgColor);
    target->drawString("B: Back", cx, edh_theme::kAboutFooterY);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawSensitivity — 感度設定画面
// ============================================================

void EdhRenderer::drawSensitivity(const edh::app::EdhScreenState& sc) {
    LovyanGFX* target = canvasReady_
        ? static_cast<LovyanGFX*>(&canvas_)
        : static_cast<LovyanGFX*>(&M5.Display);

    target->fillScreen(edh_theme::kBgColor);
    lastHoldPercent_ = 0;

    auto cx = static_cast<int32_t>(config::kCenterX);

    // タイトル
    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kSensitivityTitleFontSize);
    target->setTextColor(edh_theme::kSensitivityTitleColor, edh_theme::kBgColor);
    target->drawString("SENSITIVITY", cx, edh_theme::kSensitivityTitleY);

    // 現在の感度値（一周あたりのライフ数）
    const uint8_t sensIdx = sc.sensitivityIndex();
    const uint8_t sensVal = config::kSensitivityPresets[sensIdx];

    char valBuf[8];
    snprintf(valBuf, sizeof(valBuf), "%u", static_cast<unsigned>(sensVal));

    target->setTextSize(edh_theme::kSensitivityValueFontSize);
    target->setTextColor(edh_theme::kSensitivityValueColor, edh_theme::kBgColor);
    target->drawString(valBuf, cx, edh_theme::kSensitivityValueY);

    // ラベル
    target->setTextSize(edh_theme::kSensitivityLabelFontSize);
    target->setTextColor(edh_theme::kSensitivityLabelColor, edh_theme::kBgColor);
    target->drawString("life / rotation", cx, edh_theme::kSensitivityLabelY);

    // プリセット一覧
    target->setTextSize(edh_theme::kSensitivityPresetFontSize);
    char presetBuf[32];
    snprintf(presetBuf, sizeof(presetBuf), "%u  %u  %u",
             static_cast<unsigned>(config::kSensitivityPresets[0]),
             static_cast<unsigned>(config::kSensitivityPresets[1]),
             static_cast<unsigned>(config::kSensitivityPresets[2]));
    target->setTextColor(edh_theme::kSensitivityLabelColor, edh_theme::kBgColor);
    target->drawString(presetBuf, cx, edh_theme::kSensitivityPresetY);

    // ヒント
    target->setTextSize(edh_theme::kSensitivityHintFontSize);
    target->setTextColor(edh_theme::kHintTextColor, edh_theme::kBgColor);
    target->drawString("A: Change  B: OK", cx, edh_theme::kSensitivityHintY);

    if (canvasReady_) {
        canvas_.pushSprite(0, 0);
    }
}

// ============================================================
// drawHoldProgress — 長押し進捗弧
// ============================================================

void EdhRenderer::drawHoldProgress(uint8_t percent) {
    // FaB 版と同じロジック: 外周リング内に円弧を描画する。
    const int32_t cx = config::kDisplayWidth / 2;
    const int32_t cy = config::kDisplayHeight / 2;

    if (percent == 0 && lastHoldPercent_ != 0) {
        // 進捗消去: トラック弧を背景色で塗りつぶして消す
        M5.Display.fillArc(cx, cy,
                           edh_theme::kHoldArcOuterR,
                           edh_theme::kHoldArcInnerR,
                           0, 360,
                           edh_theme::kBgColor);
        lastHoldPercent_ = 0;
        return;
    }

    if (percent == lastHoldPercent_) {
        return;  // 変化なし
    }

    // トラック（背景弧）を描画
    if (lastHoldPercent_ == 0) {
        M5.Display.fillArc(cx, cy,
                           edh_theme::kHoldArcOuterR,
                           edh_theme::kHoldArcInnerR,
                           0, 360,
                           edh_theme::kHoldArcTrackColor);
    }

    // 進捗弧を描画
    float endAngle = 360.0f * static_cast<float>(percent) / 100.0f;
    if (endAngle > 0.1f) {
        M5.Display.fillArc(cx, cy,
                           edh_theme::kHoldArcOuterR,
                           edh_theme::kHoldArcInnerR,
                           270,  // 上端から時計回り
                           270 + static_cast<int32_t>(endAngle),
                           edh_theme::kHoldArcColor);
    }

    lastHoldPercent_ = percent;
}

}  // namespace counter::ui
