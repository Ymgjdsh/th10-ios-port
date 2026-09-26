#pragma once
#include "Arithmetic.hpp"
namespace th10 {
struct Timer {
    i32 previous;
    i32 current;
    float fractional;
    const float* rate;
    void reset() noexcept;
    void initialize(i32 previous_value) noexcept;
    i32 tick() noexcept;
    void advance(float frames) noexcept;
};
static_assert(sizeof(void*) != 4 || (offsetof(Timer, fractional) == 8 && offsetof(Timer, rate) == 12), "Reconstructed 32-bit runtime layout");
}
