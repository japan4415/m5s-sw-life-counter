#pragma once

#include <cstdint>

namespace counter::input {

// ============================================================
// ボタン入力のしきい値定数
// ============================================================

// A+B 長押しの判定しきい値 (ms)
// 両方が押されている状態がこの時間以上続いたら MenuRequested を返す。
// 変更履歴:
//   - 当初 2000 ms（A+B）/ 1500 ms（単独）で設計
//   - 実機評価で「長すぎる」と判断し 1500 ms に統一（2026-08-18）
//   - さらに実機評価で「まだ長い」と判断し 1000 ms へ短縮（2026-08-18 確定）
constexpr uint32_t kMenuLongPressMs = 1000;

// 単独ボタン長押しの判定しきい値 (ms)
// AOnly / BOnly 状態でこの時間以上押し続けたら ALongPressed / BLongPressed を返す。
// 変更履歴:
//   - docs/05-ui-ux.md では START を「1.5 秒長押しで確定」と定めており
//     当初はそれに合わせて 1500 ms としていた
//   - 実機評価で 1500 ms に統一（2026-08-18）
//   - さらに実機評価で「まだ長い」と判断し 1000 ms へ短縮（2026-08-18 確定）
// 注意: docs/05-ui-ux.md は「1.5 秒長押し」と記載されたままだが、
// 実機評価により 1 秒（1000 ms）を採用した。ドキュメント側は別途更新する。
//
// 現在 kMenuLongPressMs と同じ値 (1000 ms) だが、意味が異なるため統合しない:
//   - kSingleLongPressMs: 片方のボタンだけを長押しした場合のしきい値
//   - kMenuLongPressMs:   A+B 同時長押し（メニュー起動）のしきい値
// 将来いずれか一方のみ調整する可能性がある。
// 同じ値であるため、AOnly/BOnly で長押しが成立するタイミングと
// BothHeld での長押し成立タイミングが一致する。片方だけ押し続けると
// 1000 ms 時点で単独長押しが先に成立し、その後もう片方を押しても
// A+B 長押し（MenuRequested）にはならない。これは意図した挙動である。
constexpr uint32_t kSingleLongPressMs = 1000;

enum class ButtonEvent : uint8_t {
    None,
    UndoRequested,        // A（画面左）短押し
    LockToggleRequested,  // B（画面右）短押し
    MenuRequested,        // A+B 長押し
    ALongPressed,         // A 単独長押し（しきい値到達時に 1 回だけ）
    BLongPressed,         // B 単独長押し（しきい値到達時に 1 回だけ）
};

/// 物理ボタン A/B の入力判定。
/// ハードウェアに一切依存せず、押下状態と時刻を引数で受け取る。
/// これにより Native テスト環境でコンパイル・実行できる。
///
/// 設計上の難所: A+B 長押し（MenuRequested）と単独短押しの切り分け。
///
/// - 短押しは「離した瞬間」に確定させる。押した瞬間に確定すると、
///   A+B 長押しの途中で A の短押しが先に発火してしまうため。
/// - MenuRequested を返した後、両方が離されるまで他のイベントを返さない。
///   そうしないとメニューを開いた直後に指を離した瞬間 Undo やロックが誤発火する。
/// - 片方を押している間にもう片方が押されたら、A+B の同時押しとみなす。
///   完全に同時に押すのは人間には困難なため。合流に時間制限は設けない。
///   片方を押したまま考えてからもう片方を押す操作も受け付ける。
///   制限を設けると「メニューが開かない」という分かりにくい失敗になるため。
class ButtonInput {
public:
    void reset();

    /// 毎ループ呼ぶ。押下状態と時刻を渡し、確定したイベントを返す。
    ButtonEvent update(bool aPressed, bool bPressed, uint32_t nowMs);

    /// 現在押されているボタンの継続時間（ms）。押されていなければ 0。
    /// 長押し確認の進捗バーなどを描画するために使う。
    /// AOnly / BOnly / BothHeld では状態に入ってからの経過時間を返す。
    /// Idle / Suppressed では 0 を返す。
    uint32_t heldMs(uint32_t nowMs) const;

private:
    /// 内部状態機械
    enum class State : uint8_t {
        Idle,            // 何も押されていない
        AOnly,           // A だけ押されている（B の合流を待機中）
        BOnly,           // B だけ押されている（A の合流を待機中）
        BothHeld,        // 両方押されている（長押し判定中）
        Suppressed,      // 長押し成立後の後処理中。両方離されるまで全イベントを抑制
                         // A+B 長押し・単独長押し・A+B しきい値未達のいずれでも使う
    };

    State    state_    = State::Idle;
    uint32_t pressMs_  = 0;  // 現在の状態に入った時刻（ms）
};

}  // namespace counter::input
