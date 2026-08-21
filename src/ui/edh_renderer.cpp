// EDH（統率者戦）ファームウェアの描画層の実装。
//
// 4 扇形のレイアウト描画を行う。各扇形は矩形キャンバスで近似する。
//
// 【描画方式】
// 1. 各プレイヤーの扇形を sectorCanvas_ (100x80) に描画する
// 2. setRotation() でプレイヤーの着席方向へ回転する
// 3. pushSprite() で画面の対応位置に転送する
//
// 【部分再描画方針】
// 全画面再描画は実測 22.4 FPS で遅い。変化した扇形のみを再描画する。
// sectorCanvas_ の転送サイズは 100*80*2 = 16,000 bytes で、
// 全画面 (468*468*2 = 438,048 bytes) の約 1/27。

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

// 各キャンバスの画面上の左上座標 (pushSprite の転送先)。
// 非重複方式: 小さなキャンバス (100x80) を互いに重ならない位置に配置する。
// 各プレイヤー方向の中心座標 = 画面中心 +/- kSectorPlacementR。
// 左上座標 = 中心 - サイズ/2。
// 計算は edh_theme.hpp のコメントに記載。
constexpr int32_t kScreenCX = 234;  // config::kCenterX
constexpr int32_t kScreenCY = 234;  // config::kCenterY
constexpr int32_t kHW = edh_theme::kSectorCanvasW / 2;  // 50
constexpr int32_t kHH = edh_theme::kSectorCanvasH / 2;  // 40
constexpr int32_t kR  = edh_theme::kSectorPlacementR;    // 105

constexpr int32_t kSectorX[edh::kPlayerCount] = {
    kScreenCX - kHW,       // P1(上): 234-50 = 184
    kScreenCX + kR - kHW,  // P2(右): 234+105-50 = 289
    kScreenCX - kHW,       // P3(下): 234-50 = 184
    kScreenCX - kR - kHW,  // P4(左): 234-105-50 = 79
};
constexpr int32_t kSectorY[edh::kPlayerCount] = {
    kScreenCY - kR - kHH,  // P1(上): 234-105-40 = 89
    kScreenCY - kHH,       // P2(右): 234-40 = 194
    kScreenCY + kR - kHH,  // P3(下): 234+105-40 = 299
    kScreenCY - kHH,       // P4(左): 234-40 = 194
};

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
                "[EdhRenderer] ERROR: sector canvas allocation failed. "
                "Sector drawing will be skipped.");
            sectorReady_ = false;
        } else {
            sectorReady_ = true;
        }
    } else {
        sectorReady_ = true;
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

    // 4 プレイヤーの扇形を描画する（sectorCanvas_ が確保済みの場合のみ）
    if (sectorReady_) {
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
    if (!sectorReady_) {
        return;  // sectorCanvas_ 未確保時はスキップ
    }

    const uint8_t rot = edh_theme::kPlayerRotation[playerIndex];

    if (isCommanderDmg ||
        screenState.playerView(playerIndex) == edh::app::PlayerView::CmdDamageView) {
        renderCmdDamageView(state, screenState, playerIndex, previewDelta, rot);
    } else {
        renderLifeView(state, playerIndex, previewDelta, rot);
    }

    // 部分転送: 全画面キャンバスを使わず直接ディスプレイに転送する
    pushSectorToScreen(&M5.Display, playerIndex);

    // 非重複方式ではキャンバス同士は重ならないが、キャンバスの
    // 背景色が分割線と重なる可能性があるため、分割線を再描画する。
    drawDividers(&M5.Display);
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
    const int32_t cy = ch / 2;

    const uint16_t bgColor = defeated
        ? edh_theme::kDefeatedBgColor
        : edh_theme::kBgColor;

    // プレイヤーラベル（中心の少し上に配置）
    sectorCanvas_.setTextDatum(middle_center);
    sectorCanvas_.setTextSize(edh_theme::kPlayerLabelSize);
    sectorCanvas_.setTextColor(edh_theme::kPlayerColor[playerIndex], bgColor);
    sectorCanvas_.drawString(kPlayerLabel[playerIndex], cx,
                              cy + edh_theme::kLabelOffsetY);

    // ライフ数字
    char lifeBuf[16];
    const uint32_t life = player.life;
    snprintf(lifeBuf, sizeof(lifeBuf), "%u",
             static_cast<unsigned>(life));

    const int32_t numChars = static_cast<int32_t>(strlen(lifeBuf));
    const float fontSize = (numChars >= 4)
        ? edh_theme::kLifeFontSizeSmall
        : edh_theme::kLifeFontSize;

    if (previewDelta == 0) {
        // 通常表示
        const uint16_t textColor = defeated
            ? edh_theme::kDefeatedTextColor
            : edh_theme::kLifeColor;

        sectorCanvas_.setTextSize(fontSize);
        sectorCanvas_.setTextColor(textColor, bgColor);
        sectorCanvas_.drawString(lifeBuf, cx,
                                  cy + edh_theme::kLifeOffsetY);

        // 敗北表示
        if (defeated) {
            sectorCanvas_.setTextSize(1.0f);
            sectorCanvas_.setTextColor(edh_theme::kDefeatedTextColor,
                                        edh_theme::kDefeatedBgColor);
            sectorCanvas_.drawString("DEFEATED", cx,
                                      cy + edh_theme::kDefeatedOffsetY);
        }
    } else {
        // プレビュー表示
        sectorCanvas_.setTextSize(fontSize);
        sectorCanvas_.setTextColor(edh_theme::kPreviewLifeColor, bgColor);
        sectorCanvas_.drawString(lifeBuf, cx,
                                  cy + edh_theme::kLifeOffsetY - 12);

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
        sectorCanvas_.drawString(deltaBuf, cx,
                                  cy + edh_theme::kDeltaOffsetY);
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
    const int32_t cy = ch / 2;

    const uint16_t bgColor = defeated
        ? edh_theme::kDefeatedBgColor
        : edh_theme::kBgColor;

    // "CMD DMG" タイトル
    sectorCanvas_.setTextDatum(middle_center);
    sectorCanvas_.setTextSize(1.0f);
    sectorCanvas_.setTextColor(edh_theme::kHintTextColor, bgColor);
    sectorCanvas_.drawString("CMD DMG", cx,
                              cy + edh_theme::kCmdDmgTitleOffsetY);

    // 3 対戦相手のダメージ一覧を表示する
    const uint8_t selectedSource = screenState.selectedSource();
    int32_t itemY = cy + edh_theme::kCmdDmgFirstItemOffsetY;

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
            sectorCanvas_.setTextColor(
                edh_theme::kCmdDmgSelectedColor, bgColor);
            char label[16];
            snprintf(label, sizeof(label), "> %s", kPlayerLabel[srcIdx]);
            sectorCanvas_.drawString(label, edh_theme::kCmdDmgMarkerX,
                                      itemY);
        } else {
            sectorCanvas_.setTextColor(
                edh_theme::kPlayerColor[srcIdx], bgColor);
            sectorCanvas_.drawString(kPlayerLabel[srcIdx],
                                      edh_theme::kCmdDmgLabelX, itemY);
        }

        // ダメージ数値
        char dmgBuf[16];
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

        uint16_t dmgColor;
        if (displayDmg >= 21) {
            dmgColor = edh_theme::kCmdDmgWarningColor;
        } else if (isSelected) {
            dmgColor = edh_theme::kCmdDmgSelectedColor;
        } else {
            dmgColor = edh_theme::kCmdDmgNormalColor;
        }
        sectorCanvas_.setTextColor(dmgColor, bgColor);
        sectorCanvas_.drawString(dmgBuf,
                                  cw - edh_theme::kCmdDmgValueRightMargin,
                                  itemY);

        itemY += edh_theme::kCmdDmgItemSpacing;
    }

    // 被弾元未選択時のヒント
    if (selectedSource == edh::kSourceNone) {
        sectorCanvas_.setTextDatum(middle_center);
        sectorCanvas_.setTextSize(edh_theme::kCmdDmgHintFontSize);
        sectorCanvas_.setTextColor(edh_theme::kHintTextColor, bgColor);
        sectorCanvas_.drawString("Slide opponent", cx,
                                  cy + edh_theme::kCmdDmgHintOffsetY);
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

        sectorCanvas_.setTextDatum(middle_center);
        sectorCanvas_.setTextSize(edh_theme::kDeltaFontSize);
        sectorCanvas_.setTextColor(deltaColor, bgColor);
        sectorCanvas_.drawString(deltaBuf, cx,
                                  cy + edh_theme::kCmdDmgDeltaOffsetY);
    }
}

// ============================================================
// pushSectorToScreen — 扇形キャンバスの転送
// ============================================================

void EdhRenderer::pushSectorToScreen(LovyanGFX* target, uint8_t playerIndex) {
    sectorCanvas_.pushSprite(target,
                              kSectorX[playerIndex],
                              kSectorY[playerIndex]);
}

// ============================================================
// drawDividers — 対角分割線
// ============================================================

void EdhRenderer::drawDividers(LovyanGFX* target) {
    const int32_t w = config::kDisplayWidth;   // 468
    const int32_t h = config::kDisplayHeight;  // 468

    for (int32_t d = 0; d < edh_theme::kDividerWidth; ++d) {
        target->drawLine(0, d, w - 1, h - 1 - d, edh_theme::kDividerColor);
        if (d > 0) {
            target->drawLine(d, 0, w - 1, h - 1 - d, edh_theme::kDividerColor);
        }

        target->drawLine(w - 1, d, 0, h - 1 - d, edh_theme::kDividerColor);
        if (d > 0) {
            target->drawLine(w - 1 - d, 0, 0, h - 1 - d, edh_theme::kDividerColor);
        }
    }
}

// ============================================================
// drawLockState — タッチロック表示
// ============================================================

void EdhRenderer::drawLockState(const edh::MatchState& state) {
    if (state.touchLocked) {
        if (canvasReady_) {
            drawLockIcon(&canvas_);
        }
        drawLockIcon(&M5.Display);
    } else {
        if (canvasReady_) {
            clearLockRegion(&canvas_);
            drawDividers(&canvas_);
        }
        clearLockRegion(&M5.Display);
        drawDividers(&M5.Display);
    }
}

// ============================================================
// drawLockIcon / clearLockRegion
// ============================================================

void EdhRenderer::drawLockIcon(LovyanGFX* target) {
    const int32_t cx = config::kDisplayWidth / 2;
    const int32_t cy = config::kDisplayHeight / 2;

    target->fillRect(edh_theme::kLockRegionX, edh_theme::kLockRegionY,
                     edh_theme::kLockRegionW, edh_theme::kLockRegionH,
                     edh_theme::kBgColor);

    const int32_t bodyW = 16;
    const int32_t bodyH = 12;
    target->fillRect(cx - bodyW / 2, cy - 2,
                     bodyW, bodyH, edh_theme::kLockIconColor);

    target->drawArc(cx, cy - 2, 8, 5, 180, 360,
                    edh_theme::kLockIconColor);

    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kLockTextSize);
    target->setTextColor(edh_theme::kLockIconColor, edh_theme::kBgColor);
    target->drawString("LOCK", cx, cy + 15);
}

void EdhRenderer::clearLockRegion(LovyanGFX* target) {
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

    char lifeBuf[16];
    snprintf(lifeBuf, sizeof(lifeBuf), "%u",
             static_cast<unsigned>(sc.setupLife()));

    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kSetupLifeFontSize);
    target->setTextColor(edh_theme::kLifeColor, edh_theme::kBgColor);
    target->drawString(lifeBuf, cx, edh_theme::kSetupLifeY);

    target->setTextSize(edh_theme::kSetupHintFontSize);
    target->setTextColor(edh_theme::kHintTextColor, edh_theme::kBgColor);
    target->drawString("Ring: +/- Life", cx, edh_theme::kSetupHintY1);
    target->drawString("A: Life presets", cx, edh_theme::kSetupHintY2);

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

    drawBatteryIcon(target, batteryPercent, charging);

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
            target->setTextSize(edh_theme::kMenuItemFontSize);
            target->setTextColor(edh_theme::kMenuConfirmColor, edh_theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s? <", kItemNames[i]);
            target->drawString(buf, cx, itemY);
        } else if (isSelected) {
            target->setTextSize(edh_theme::kMenuItemFontSize);
            target->setTextColor(edh_theme::kMenuSelectedColor, edh_theme::kBgColor);
            snprintf(buf, sizeof(buf), "> %s", kItemNames[i]);
            target->drawString(buf, cx, itemY);
        } else {
            target->setTextSize(edh_theme::kMenuItemFontSize);
            target->setTextColor(edh_theme::kMenuNormalColor, edh_theme::kBgColor);
            target->drawString(kItemNames[i], cx, itemY);
        }
    }

    if (confirming) {
        target->setTextSize(edh_theme::kMenuConfirmFontSize);
        target->setTextColor(edh_theme::kMenuConfirmColor, edh_theme::kBgColor);
        target->setTextDatum(middle_center);
        target->drawString("Hold B to confirm", cx, edh_theme::kMenuConfirmMsgY);
    }

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
    const auto cx = static_cast<int32_t>(config::kCenterX);
    const bool warning = (percent <= edh_theme::kBatteryWarningThreshold);
    const uint16_t color = warning
        ? edh_theme::kBatteryWarningColor
        : edh_theme::kBatteryNormalColor;

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

    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kHistoryTitleFontSize);
    target->setTextColor(edh_theme::kHistoryTitleColor, edh_theme::kBgColor);
    target->drawString("HISTORY", cx, edh_theme::kHistoryTitleY);

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

            char histBuf[32];
            if (entry.sourceIndex != edh::kSourceNone) {
                snprintf(histBuf, sizeof(histBuf), "P%d <- P%d  %s%d",
                         entry.playerIndex + 1,
                         entry.sourceIndex + 1,
                         (entry.delta > 0) ? "+" : "",
                         static_cast<int>(entry.delta));
            } else {
                snprintf(histBuf, sizeof(histBuf), "P%d  %s%d",
                         entry.playerIndex + 1,
                         (entry.delta > 0) ? "+" : "",
                         static_cast<int>(entry.delta));
            }

            target->setTextSize(edh_theme::kHistoryItemFontSize);
            target->setTextColor(
                edh_theme::kPlayerColor[entry.playerIndex],
                edh_theme::kBgColor);
            target->drawString(histBuf, cx, itemY);
        }
    }

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

    target->setTextDatum(middle_center);
    target->setTextSize(edh_theme::kSensitivityTitleFontSize);
    target->setTextColor(edh_theme::kSensitivityTitleColor, edh_theme::kBgColor);
    target->drawString("SENSITIVITY", cx, edh_theme::kSensitivityTitleY);

    const uint8_t sensIdx = sc.sensitivityIndex();
    const uint8_t sensVal = config::kSensitivityPresets[sensIdx];

    char valBuf[8];
    snprintf(valBuf, sizeof(valBuf), "%u", static_cast<unsigned>(sensVal));

    target->setTextSize(edh_theme::kSensitivityValueFontSize);
    target->setTextColor(edh_theme::kSensitivityValueColor, edh_theme::kBgColor);
    target->drawString(valBuf, cx, edh_theme::kSensitivityValueY);

    target->setTextSize(edh_theme::kSensitivityLabelFontSize);
    target->setTextColor(edh_theme::kSensitivityLabelColor, edh_theme::kBgColor);
    target->drawString("life / rotation", cx, edh_theme::kSensitivityLabelY);

    target->setTextSize(edh_theme::kSensitivityPresetFontSize);
    char presetBuf[32];
    snprintf(presetBuf, sizeof(presetBuf), "%u  %u  %u",
             static_cast<unsigned>(config::kSensitivityPresets[0]),
             static_cast<unsigned>(config::kSensitivityPresets[1]),
             static_cast<unsigned>(config::kSensitivityPresets[2]));
    target->setTextColor(edh_theme::kSensitivityLabelColor, edh_theme::kBgColor);
    target->drawString(presetBuf, cx, edh_theme::kSensitivityPresetY);

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
    const int32_t cx = config::kDisplayWidth / 2;
    const int32_t cy = config::kDisplayHeight / 2;

    if (percent == 0 && lastHoldPercent_ != 0) {
        M5.Display.fillArc(cx, cy,
                           edh_theme::kHoldArcOuterR,
                           edh_theme::kHoldArcInnerR,
                           0, 360,
                           edh_theme::kBgColor);
        lastHoldPercent_ = 0;
        return;
    }

    if (percent == lastHoldPercent_) {
        return;
    }

    if (lastHoldPercent_ == 0) {
        M5.Display.fillArc(cx, cy,
                           edh_theme::kHoldArcOuterR,
                           edh_theme::kHoldArcInnerR,
                           0, 360,
                           edh_theme::kHoldArcTrackColor);
    }

    float endAngle = 360.0f * static_cast<float>(percent) / 100.0f;
    if (endAngle > 0.1f) {
        M5.Display.fillArc(cx, cy,
                           edh_theme::kHoldArcOuterR,
                           edh_theme::kHoldArcInnerR,
                           270,
                           270 + static_cast<int32_t>(endAngle),
                           edh_theme::kHoldArcColor);
    }

    lastHoldPercent_ = percent;
}

}  // namespace counter::ui
