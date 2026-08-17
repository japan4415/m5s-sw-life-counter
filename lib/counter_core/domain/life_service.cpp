#include "life_service.hpp"

#include <cstdint>
#include <climits>

namespace counter::domain {

LifeChange applyLifeChange(MatchState& state, PlayerId player,
                           int32_t requestedDelta, uint32_t uptimeMs) {
    // requestedDelta == 0 は意味のない操作なので履歴を積まない。
    // 外周スライドで十分にスライドせずに離した場合に該当する。
    if (requestedDelta == 0) {
        PlayerState& ps = state.players[toIndex(player)];
        return LifeChange{
            .sequence       = 0,
            .player         = player,
            .requestedDelta = 0,
            .appliedDelta   = 0,
            .before         = ps.life,
            .after          = ps.life,
            .uptimeMs       = uptimeMs,
        };
    }

    PlayerState& ps = state.players[toIndex(player)];
    const uint32_t before = ps.life;

    // int64_t にキャストして加算し、UINT32_MAX 近傍でのオーバーフローを回避する。
    // docs/06: before(uint32_t) + requestedDelta(int32_t) の直接加算は
    // UINT32_MAX 付近で算術オーバーフローを起こすため int64_t で計算する。
    int64_t candidate = static_cast<int64_t>(before)
                      + static_cast<int64_t>(requestedDelta);

    // 下限クランプ: ライフは 0 未満にならない
    if (candidate < 0) candidate = 0;

    // 上限クランプ: 事実上到達しないが、オーバーフロー防止のため UINT32_MAX で制限。
    // 開始ライフは上限ではない -- FaB のルール上、ライフは開始値を超えて増加できる。
    // startingLife は初期値および Rematch 用の参照値であり、上限として機能しない。
    if (candidate > static_cast<int64_t>(UINT32_MAX)) candidate = UINT32_MAX;

    ps.life = static_cast<uint32_t>(candidate);

    // appliedDelta は変更前後のライフ差に一致する (不変条件 3)。
    // requestedDelta とは異なる場合がある（例: ライフ 2 で -5 なら applied = -2）。
    // 両方を保持する理由: Undo は appliedDelta の逆適用ではなく before への復元で
    // 行うため、requested と applied の区別が履歴の正確性に必要。
    const int32_t appliedDelta = static_cast<int32_t>(ps.life)
                               - static_cast<int32_t>(before);

    LifeChange change{
        .sequence       = state.nextSequence,
        .player         = player,
        .requestedDelta = requestedDelta,
        .appliedDelta   = appliedDelta,
        .before         = before,
        .after          = ps.life,
        .uptimeMs       = uptimeMs,
    };

    // sequence は単調増加 (不変条件 8)
    ++state.nextSequence;

    // リングバッファは最大 64 件。超過分は古いものから自動的に上書きされる (不変条件 7)
    state.history.push(change);

    return change;
}

bool undoLast(MatchState& state) {
    if (state.history.empty()) {
        return false;
    }

    const LifeChange& last = state.history.back();

    // Undo は差分の逆適用 (+appliedDelta を戻す) ではなく、
    // 履歴に記録された before 値への直接復元で行う (不変条件 5/8)。
    // 例: ライフ 2 で requestedDelta=-5 -> applied=-2, after=0 の場合、
    // Undo は +5 でも +2 でもなく、before=2 に戻す。
    // これによりクランプが発生した変更でも正確に元の状態へ戻せる。
    state.players[toIndex(last.player)].life = last.before;

    state.history.popBack();

    return true;
}

void swapSides(MatchState& state) {
    PlayerState tmp = state.players[toIndex(PlayerId::Top)];
    state.players[toIndex(PlayerId::Top)] = state.players[toIndex(PlayerId::Bottom)];
    state.players[toIndex(PlayerId::Bottom)] = tmp;
}

void startMatch(MatchState& state, uint32_t topStartingLife,
                uint32_t bottomStartingLife) {
    state.players[toIndex(PlayerId::Top)] = PlayerState{
        .startingLife = topStartingLife,
        .life         = topStartingLife,
    };
    state.players[toIndex(PlayerId::Bottom)] = PlayerState{
        .startingLife = bottomStartingLife,
        .life         = bottomStartingLife,
    };
    state.active = true;
    state.touchLocked = false;
    state.nextSequence = 0;
    state.history.clear();
}

void rematch(MatchState& state) {
    // 同じ開始ライフでやり直す。startingLife は変更しない。
    state.players[toIndex(PlayerId::Top)].life =
        state.players[toIndex(PlayerId::Top)].startingLife;
    state.players[toIndex(PlayerId::Bottom)].life =
        state.players[toIndex(PlayerId::Bottom)].startingLife;
    state.active = true;
    state.touchLocked = false;
    state.nextSequence = 0;
    state.history.clear();
}

}  // namespace counter::domain
