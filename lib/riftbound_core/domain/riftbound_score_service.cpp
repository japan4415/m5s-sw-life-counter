#include "riftbound_score_service.hpp"

#include <cstdint>
#include <climits>

namespace counter::riftbound {

ScoreChange applyScoreChange(MatchState& state, PlayerId player,
                             int32_t requestedDelta, uint32_t uptimeMs) {
    // requestedDelta == 0 は意味のない操作なので履歴を積まない。
    // 外周スライドで十分にスライドせずに離した場合に該当する。
    if (requestedDelta == 0) {
        PlayerState& ps = state.players[toIndex(player)];
        return ScoreChange{
            .sequence       = 0,
            .player         = player,
            .requestedDelta = 0,
            .appliedDelta   = 0,
            .before         = ps.score,
            .after          = ps.score,
            .uptimeMs       = uptimeMs,
        };
    }

    PlayerState& ps = state.players[toIndex(player)];
    const uint32_t before = ps.score;

    // int64_t にキャストして加算し、UINT32_MAX 近傍でのオーバーフローを回避する。
    // FaB 版 life_service.cpp と同じ算術方針。
    int64_t candidate = static_cast<int64_t>(before)
                      + static_cast<int64_t>(requestedDelta);

    // 下限クランプ: 得点は 0 未満にならない
    if (candidate < 0) candidate = 0;

    // 上限クランプ: 事実上到達しないが、オーバーフロー防止のため UINT32_MAX で制限。
    // 勝利点は上限ではない -- 本機は記録補助であり、Undo や入力修正の
    // 余地を残すため勝利点を超える値も許容する（表示で到達を警告する）。
    if (candidate > static_cast<int64_t>(UINT32_MAX)) candidate = UINT32_MAX;

    ps.score = static_cast<uint32_t>(candidate);

    // appliedDelta は変更前後の得点差に一致する。
    // requestedDelta とは異なる場合がある（例: 得点 2 で -5 なら applied = -2）。
    // 両方を保持する理由: Undo は appliedDelta の逆適用ではなく before への復元で
    // 行うため、requested と applied の区別が履歴の正確性に必要。
    const int32_t appliedDelta = static_cast<int32_t>(ps.score)
                               - static_cast<int32_t>(before);

    ScoreChange change{
        .sequence       = state.nextSequence,
        .player         = player,
        .requestedDelta = requestedDelta,
        .appliedDelta   = appliedDelta,
        .before         = before,
        .after          = ps.score,
        .uptimeMs       = uptimeMs,
    };

    // sequence は単調増加
    ++state.nextSequence;

    // リングバッファは最大 64 件。超過分は古いものから自動的に上書きされる
    state.history.push(change);

    return change;
}

bool undoLast(MatchState& state) {
    if (state.history.empty()) {
        return false;
    }

    const ScoreChange& last = state.history.back();

    // Undo は差分の逆適用 (+appliedDelta を戻す) ではなく、
    // 履歴に記録された before 値への直接復元で行う。
    // 例: 得点 2 で requestedDelta=-5 -> applied=-2, after=0 の場合、
    // Undo は +5 でも +2 でもなく、before=2 に戻す。
    // これによりクランプが発生した変更でも正確に元の状態へ戻せる。
    state.players[toIndex(last.player)].score = last.before;

    state.history.popBack();

    return true;
}

void startMatch(MatchState& state) {
    state.players[toIndex(PlayerId::Top)] = PlayerState{
        .score = 0,
    };
    state.players[toIndex(PlayerId::Bottom)] = PlayerState{
        .score = 0,
    };
    state.victoryScore = kDefaultVictoryScore;
    state.active = true;
    state.touchLocked = false;
    state.nextSequence = 0;
    state.history.clear();
}

void rematch(MatchState& state) {
    // 得点 0 でやり直す。victoryScore は維持する。
    state.players[toIndex(PlayerId::Top)].score = 0;
    state.players[toIndex(PlayerId::Bottom)].score = 0;
    state.active = true;
    state.touchLocked = false;
    state.nextSequence = 0;
    state.history.clear();
}

bool hasReachedVictory(const MatchState& state, PlayerId player) {
    return state.players[toIndex(player)].score
           >= state.victoryScore;
}

}  // namespace counter::riftbound
