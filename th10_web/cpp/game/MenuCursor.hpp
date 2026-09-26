#pragma once
#include "Types.hpp"
namespace th10 {
struct MenuCursor {
    i32 selected;
    i32 reserved;
    i32 item_count;
    i32 selection_stack[16];
    i32 count_stack[16];
    i32 stack_depth;
    i32 disabled_items[16];
    i32 wrap;
    i32 disabled_count;
    void push() noexcept;
    void pop() noexcept;
    i32 move(i32 delta) noexcept;
    i32 select(i32 value) noexcept;
};
static_assert(sizeof(void*) != 4 || (sizeof(MenuCursor) == 0xd8), "Reconstructed 32-bit runtime layout");
static_assert(sizeof(void*) != 4 || (offsetof(MenuCursor, stack_depth) == 0x8c), "Reconstructed 32-bit runtime layout");
static_assert(sizeof(void*) != 4 || (offsetof(MenuCursor, wrap) == 0xd0), "Reconstructed 32-bit runtime layout");
}
