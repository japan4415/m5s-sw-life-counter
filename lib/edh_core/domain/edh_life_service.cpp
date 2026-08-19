#include "domain/edh_life_service.hpp"

#include <cstdint>
#include <climits>

namespace counter::edh {

namespace {

// 統率者ダメージの上限（2 桁表示のためのソフトクランプ）
constexpr uint8_t kMaxCmdDmg = 99;

// 統率者ダメージ敗北閾値（MTG CR 903.10.1）
constexpr uint8_t kCmdDmgDefeatThreshold = 21;

}  // namespace

void startMatch(MatchState& state, uint32_t startingLife) {
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        state.players[i].startingLife = startingLife;
        state.players[i].life = startingLife;
        for (uint8_t j = 0; j < kPlayerCount; ++j) {
            state.players[i].commanderDamageFrom[j] = 0;
        }
    }
    state.active = true;
    state.touchLocked = false;
    state.nextSequence = 0;
    state.history.clear();
}

LifeChange applyLifeChange(MatchState& state, uint8_t playerIndex,
                           int16_t delta, uint32_t uptimeMs) {
    PlayerState& ps = state.players[playerIndex];
    const uint32_t lifeBefore = ps.life;

    // delta == 0 は意味のない操作なので履歴を積まない
    if (delta == 0) {
        return LifeChange{
            .sequence     = 0,
            .playerIndex  = playerIndex,
            .sourceIndex  = kSourceNone,
            .delta        = 0,
            .lifeBefore   = lifeBefore,
            .lifeAfter    = lifeBefore,
            .cmdDmgBefore = 0,
            .cmdDmgAfter  = 0,
            .uptimeMs     = uptimeMs,
        };
    }

    // int64_t にキャストして加算し、オーバーフローを回避する
    int64_t candidate = static_cast<int64_t>(lifeBefore)
                      + static_cast<int64_t>(delta);

    // 下限クランプ: ライフは 0 未満にならない
    if (candidate < 0) candidate = 0;

    // 上限: 事実上到達しないが安全のため
    if (candidate > static_cast<int64_t>(UINT32_MAX)) candidate = UINT32_MAX;

    ps.life = static_cast<uint32_t>(candidate);

    LifeChange change{
        .sequence     = state.nextSequence,
        .playerIndex  = playerIndex,
        .sourceIndex  = kSourceNone,
        .delta        = delta,
        .lifeBefore   = lifeBefore,
        .lifeAfter    = ps.life,
        .cmdDmgBefore = 0,
        .cmdDmgAfter  = 0,
        .uptimeMs     = uptimeMs,
    };

    ++state.nextSequence;
    state.history.push(change);

    return change;
}

LifeChange applyCommanderDamage(MatchState& state, uint8_t playerIndex,
                                uint8_t sourceIndex, int16_t delta,
                                uint32_t uptimeMs) {
    PlayerState& ps = state.players[playerIndex];
    const uint32_t lifeBefore = ps.life;
    const uint8_t cmdDmgBefore = ps.commanderDamageFrom[sourceIndex];

    // delta == 0 は意味のない操作なので履歴を積まない
    if (delta == 0) {
        return LifeChange{
            .sequence     = 0,
            .playerIndex  = playerIndex,
            .sourceIndex  = sourceIndex,
            .delta        = 0,
            .lifeBefore   = lifeBefore,
            .lifeAfter    = lifeBefore,
            .cmdDmgBefore = cmdDmgBefore,
            .cmdDmgAfter  = cmdDmgBefore,
            .uptimeMs     = uptimeMs,
        };
    }

    // 統率者ダメージのクランプ（0〜99）
    int16_t newDmg = static_cast<int16_t>(cmdDmgBefore) + delta;
    if (newDmg < 0) newDmg = 0;
    if (newDmg > static_cast<int16_t>(kMaxCmdDmg)) newDmg = static_cast<int16_t>(kMaxCmdDmg);

    const uint8_t cmdDmgAfter = static_cast<uint8_t>(newDmg);

    // クランプ後の実際の変化量
    const int16_t actualDmgDelta = static_cast<int16_t>(cmdDmgAfter) - static_cast<int16_t>(cmdDmgBefore);

    ps.commanderDamageFrom[sourceIndex] = cmdDmgAfter;

    // ライフ連動: クランプ後の実際の変化量の符号反転分をライフに適用する
    // 統率者ダメージ +n → ライフ -n、統率者ダメージ -n → ライフ +n
    const int16_t lifeDelta = static_cast<int16_t>(-actualDmgDelta);

    int64_t lifeCandidate = static_cast<int64_t>(lifeBefore)
                          + static_cast<int64_t>(lifeDelta);

    // ライフの下限クランプ
    if (lifeCandidate < 0) lifeCandidate = 0;
    if (lifeCandidate > static_cast<int64_t>(UINT32_MAX)) lifeCandidate = UINT32_MAX;

    ps.life = static_cast<uint32_t>(lifeCandidate);

    LifeChange change{
        .sequence     = state.nextSequence,
        .playerIndex  = playerIndex,
        .sourceIndex  = sourceIndex,
        .delta        = delta,
        .lifeBefore   = lifeBefore,
        .lifeAfter    = ps.life,
        .cmdDmgBefore = cmdDmgBefore,
        .cmdDmgAfter  = cmdDmgAfter,
        .uptimeMs     = uptimeMs,
    };

    ++state.nextSequence;
    state.history.push(change);

    return change;
}

bool undoLast(MatchState& state) {
    if (state.history.empty()) {
        return false;
    }

    const LifeChange& last = state.history.back();

    // ライフを復元する
    state.players[last.playerIndex].life = last.lifeBefore;

    // 統率者ダメージ操作の場合は統率者ダメージも復元する
    if (last.sourceIndex != kSourceNone) {
        state.players[last.playerIndex].commanderDamageFrom[last.sourceIndex] = last.cmdDmgBefore;
    }

    state.history.popBack();

    return true;
}

void rematch(MatchState& state) {
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        state.players[i].life = state.players[i].startingLife;
        for (uint8_t j = 0; j < kPlayerCount; ++j) {
            state.players[i].commanderDamageFrom[j] = 0;
        }
    }
    state.active = true;
    state.touchLocked = false;
    state.nextSequence = 0;
    state.history.clear();
}

bool isDefeated(const MatchState& state, uint8_t playerIndex) {
    const PlayerState& ps = state.players[playerIndex];

    // ライフ 0 以下で敗北（CR 704.5a）
    if (ps.life == 0) {
        return true;
    }

    // いずれかの被弾元からの統率者ダメージが 21 以上で敗北（CR 903.10.1）
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        if (i == playerIndex) continue;  // 自分自身はスキップ
        if (ps.commanderDamageFrom[i] >= kCmdDmgDefeatThreshold) {
            return true;
        }
    }

    return false;
}

}  // namespace counter::edh
