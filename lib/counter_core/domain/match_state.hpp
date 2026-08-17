#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "life_change.hpp"

namespace counter::domain {

struct PlayerState {
    uint32_t startingLife;
    uint32_t life;
};

// 固定長リングバッファ。動的メモリ確保を行わない。
// 添字 0 が最新、size()-1 が最古を指す論理順序で参照できる。
// 容量を超えた場合は最も古い要素を上書きして捨てる。
template <typename T, size_t N>
class RingBuffer {
public:
    // 最新の要素として追加する。容量超過時は最古の要素を上書きする。
    void push(const T& value) {
        // head_ は次に書き込む物理位置を指す
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (count_ < N) {
            ++count_;
        }
        // count_ == N のとき、head_ の前進で最古の要素が上書きされる
    }

    // 最新の要素を取り出して削除する。呼び出し前に empty() でないことを確認すること。
    void popBack() {
        if (count_ == 0) return;
        // head_ は「次の書き込み位置」なので、最新要素は head_ - 1
        head_ = (head_ + N - 1) % N;
        --count_;
    }

    // 最新の要素への参照を返す。呼び出し前に empty() でないことを確認すること。
    T& back() {
        // head_ - 1 が最新要素の物理位置
        return data_[(head_ + N - 1) % N];
    }

    const T& back() const {
        return data_[(head_ + N - 1) % N];
    }

    // 論理添字でアクセスする。0 が最新、size()-1 が最古。
    // Undo 操作では常に最新（index 0）を参照するが、
    // 履歴表示では古い方から順に見たい場合もあるため添字アクセスを提供する。
    T& operator[](size_t index) {
        // 最新 = head_ - 1、そこから index 分だけ遡る
        return data_[(head_ + N - 1 - index) % N];
    }

    const T& operator[](size_t index) const {
        return data_[(head_ + N - 1 - index) % N];
    }

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

private:
    std::array<T, N> data_{};
    size_t head_ = 0;   // 次に書き込む物理位置
    size_t count_ = 0;  // 現在の要素数
};

// PlayerId を配列添字に変換するヘルパ。
// players[0] = Top, players[1] = Bottom（docs/06 の定義に合わせる）
inline size_t toIndex(PlayerId id) {
    return static_cast<size_t>(id);
}

struct MatchState {
    uint16_t schemaVersion;
    PlayerState players[2];
    bool active;
    bool touchLocked;
    uint32_t nextSequence;
    RingBuffer<LifeChange, 64> history;
};

}  // namespace counter::domain
