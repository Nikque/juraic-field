#pragma once
#include "follow14.h"
#include <atomic>
#include <cstring>

namespace play::realgame {

// 読み取る相手の席。既定ではどちらの入力も読まない。
inline std::atomic<int> primaryInputSeat14{-1};

struct PrimaryInputRead14 {
    uint64_t object = 0;
    uint8_t held = 0;
    bool operator==(const PrimaryInputRead14&) const = default;
};

// ローカル対戦の席に割り当てられたゲーム内の入力。呼び出し側は盤面を読む前後で
// オブジェクトと値が一致した場合だけ採用する。アドレスは対戦記録には残さない。
inline std::optional<PrimaryInputRead14> readPrimaryInput14(const FieldContext& context, uint64_t player, int seat, const FieldRead14& read) {
    const FieldProcess& process = context.process;
    if (seat < 0 || seat > 1 || !process.pid || !process.creationTime || !process.imageBase || !verifiedFieldImage(process.image) ||
        process.imageBase > UINT64_MAX - process.image.imageSize) return {};
    const uint64_t image = process.imageBase, end = image + process.image.imageSize;
    const auto heap = [&](uint64_t a, uint64_t size) {
        return a >= 4096 && a <= UINT64_MAX - size && (a + size <= image || a >= end);
    };
    uint64_t playerType = 0, object = 0, inputType = 0;
    uint8_t playerSeat = 0, enabled = 0;
    if (!heap(player, 0x90) || !read(player, &playerType, sizeof playerType) || playerType != image + 0x2ac500 ||
        !read(player + 0x5e, &playerSeat, 1) || playerSeat != seat || !read(player + 0x88, &object, sizeof object) || !heap(object, 0x55) ||
        !read(object, &inputType, sizeof inputType) || inputType != image + 0x2aac18) return {};
    // 親、席番号、現在と更新完了時の値は連続した16バイト。上位8ビットは常に0。
    std::array<uint8_t, 16> state{};
    if (!read(object + 0x20, state.data(), state.size()) || !read(object + 0x54, &enabled, 1) || enabled != 1) return {};
    uint64_t parent = 0;
    uint32_t inputSeat = 0;
    std::memcpy(&parent, state.data(), sizeof parent);
    std::memcpy(&inputSeat, state.data() + 8, sizeof inputSeat);
    if (parent != player || inputSeat != uint32_t(seat) || state[13] || state[15] || state[12] != state[14]) return {};
    return PrimaryInputRead14{object, state[12]};
}

} // namespace play::realgame
