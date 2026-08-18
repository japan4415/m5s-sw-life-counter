#include "input/button_input.hpp"

namespace counter::input {

void ButtonInput::reset() {
    state_   = State::Idle;
    pressMs_ = 0;
}

ButtonEvent ButtonInput::update(bool aPressed, bool bPressed, uint32_t nowMs) {
    // 状態機械で A+B 長押しと単独短押しを切り分ける。
    //
    // 優先順位（同時成立時）:
    //   MenuRequested > UndoRequested / LockToggleRequested
    //
    // 理由: A+B 長押しは意図的な操作であり、最も強い意図を示す。
    // 単独短押しは A+B 長押しの経路上で「通過」してしまうため、
    // 長押しが成立しうる限り短押しを確定させてはならない。

    switch (state_) {

    case State::Idle:
        if (aPressed && bPressed) {
            // 両方同時に押された（稀だが可能）
            state_   = State::BothHeld;
            pressMs_ = nowMs;
        } else if (aPressed) {
            state_   = State::AOnly;
            pressMs_ = nowMs;
        } else if (bPressed) {
            state_   = State::BOnly;
            pressMs_ = nowMs;
        }
        // 何も押されていなければ Idle のまま
        return ButtonEvent::None;

    case State::AOnly:
        if (aPressed && bPressed) {
            // もう片方（B）が合流した → 同時押し扱い。
            // 合流に時間制限は設けない。A を押したまま考えてから B を押す
            // 操作も受け付ける。制限を設けると「メニューが開かない」という
            // 分かりにくい失敗になるため。
            state_   = State::BothHeld;
            pressMs_ = nowMs;
            return ButtonEvent::None;
        }
        if (!aPressed) {
            // A が離された。B が合流しなかったので単独短押し確定。
            // 短押しは「離した瞬間」に確定させる。押した瞬間に確定すると、
            // A+B 長押しの途中で A の短押しが先に発火してしまうため。
            state_ = State::Idle;
            return ButtonEvent::UndoRequested;
        }
        // A だけ押され続けている。長押し判定。
        // しきい値に達したら ALongPressed を 1 回だけ返し、Suppressed へ遷移する。
        // Suppressed に入ることで、離した瞬間の短押し（UndoRequested）を抑制し、
        // 追加の B 押下による A+B 長押しへの遷移も防ぐ。
        if (nowMs - pressMs_ >= kSingleLongPressMs) {
            state_ = State::Suppressed;
            return ButtonEvent::ALongPressed;
        }
        // しきい値未達。B の合流または離しを待つ。
        return ButtonEvent::None;

    case State::BOnly:
        if (aPressed && bPressed) {
            // もう片方（A）が合流した → 同時押し扱い。AOnly と同じ理由。
            state_   = State::BothHeld;
            pressMs_ = nowMs;
            return ButtonEvent::None;
        }
        if (!bPressed) {
            // B が離された。A が合流しなかったので単独短押し確定。
            state_ = State::Idle;
            return ButtonEvent::LockToggleRequested;
        }
        // B だけ押され続けている。長押し判定。AOnly と同じロジック。
        if (nowMs - pressMs_ >= kSingleLongPressMs) {
            state_ = State::Suppressed;
            return ButtonEvent::BLongPressed;
        }
        // しきい値未達。A の合流または離しを待つ。
        return ButtonEvent::None;

    case State::BothHeld:
        if (aPressed && bPressed) {
            // 両方押され続けている。長押し判定。
            if (nowMs - pressMs_ >= kMenuLongPressMs) {
                // 長押し成立。MenuRequested を返す。
                // その後 Suppressed へ遷移し、両方離されるまでイベントを抑制する。
                state_ = State::Suppressed;
                return ButtonEvent::MenuRequested;
            }
            return ButtonEvent::None;
        }
        // 長押し成立前にどちらかが離された。
        // A+B 同時押しの意図だったが長押し時間に達しなかった。
        // Suppressed へ遷移し、両方が離されるまで全イベントを抑制する。
        //
        // なぜ: 両方押す意図があった以上、単独短押しとして処理すると
        // ユーザーの意図と異なる動作になる。「メニューを開こうとしたが
        // 押しが足りなかった」場合に、残っている側のボタンを離した瞬間に
        // Undo やロックが誤発火するのを防ぐため、安全側に倒して
        // 両方が離されるまで何も返さない。
        state_ = State::Suppressed;
        return ButtonEvent::None;

    case State::Suppressed:
        // 長押し成立後または A+B しきい値未達後の後処理。
        // 両方のボタンが離されるまで全イベントを抑制する。
        //
        // なぜ抑制するのか:
        // - A+B 長押し成立後: メニューを開いた直後に指を離した瞬間に
        //   Undo やロックが誤発火するのを防ぐため。
        // - 単独長押し成立後: 長押し確定後に指を離した瞬間に
        //   短押し（UndoRequested / LockToggleRequested）が誤発火するのを防ぐため。
        //   また、長押し成立後にもう片方を押しても A+B にならないようにするため。
        // - A+B しきい値未達後: メニューを開こうとして中断しただけの操作で、
        //   まだ押されている側のボタンを離した瞬間に意図しない
        //   Undo やロックを起こさないため。
        if (!aPressed && !bPressed) {
            state_ = State::Idle;
        }
        return ButtonEvent::None;
    }

    // switch の全 case を網羅しているが、コンパイラ警告を避けるための到達不能パス
    return ButtonEvent::None;
}

uint32_t ButtonInput::heldMs(uint32_t nowMs) const {
    switch (state_) {
    case State::AOnly:
    case State::BOnly:
    case State::BothHeld:
        // いずれかのボタンが押されている状態。
        // pressMs_ はその状態に入った時刻を記録しているため、
        // 差分がボタンの継続押下時間となる。
        return nowMs - pressMs_;

    case State::Idle:
    case State::Suppressed:
        // Idle: ボタンが押されていないため 0。
        // Suppressed: 長押し成立済み（または A+B 中断後）であり、
        // 進捗バーを表示する意味がないため 0 を返す。
        return 0;
    }

    // switch の全 case を網羅しているが、コンパイラ警告を避けるための到達不能パス
    return 0;
}

}  // namespace counter::input
