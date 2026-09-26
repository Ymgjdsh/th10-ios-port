#pragma once
#include "EclStack.hpp"
namespace th10 {
struct EclOwner;
struct EclServices;
struct EclInstruction {
    i32 time;
    u16 opcode;
    u16 length;
    u16 references;
    u8 difficulty;
    u8 parameter_count;
    u32 stack_adjustment;
    u32 argument(u32 index) const noexcept {
        u32 result; std::memcpy(&result, reinterpret_cast<const u8*>(this) + 16 + index * 4, 4); return result;
    }
    bool is_reference(u32 index) const noexcept { return (references & (1u << (index & 31))) != 0; }
};
static_assert(sizeof(EclInstruction) == 16);
struct EclGlobals {
    virtual i32 integer(i32 variable) = 0;
    virtual Extended floating(i32 variable) = 0;
    virtual i32* integer_reference(i32 variable) = 0;
    virtual float* float_reference(i32 variable) = 0;
};
struct EclContext {
    float time;
    EclInstruction* instruction;
    EclStack stack;
    i32 thread_id;
    EclOwner* owner;
    u32 state_1018;
    u32 difficulty;
    u32 flags;
#if UINTPTR_MAX > UINT32_MAX
    u32 return_pointer_high[1024];
#endif
    void push_return_instruction(EclInstruction* value) noexcept {
        const auto address=reinterpret_cast<std::uintptr_t>(value);
#if UINTPTR_MAX > UINT32_MAX
        if(stack.top<0||stack.top>=4096)__builtin_trap();
        return_pointer_high[stack.top/4]=static_cast<u32>(address>>32);
#endif
        const u32 low=static_cast<u32>(address);if(stack.push(EclValueType::Untyped,&low,4))__builtin_trap();
    }
    void pop_return_instruction() noexcept {
        u32 low=0;if(stack.pop(EclValueType::Untyped,&low,4))__builtin_trap();
        std::uintptr_t address=low;
#if UINTPTR_MAX > UINT32_MAX
        address|=static_cast<std::uintptr_t>(return_pointer_high[stack.top/4])<<32;
#endif
        instruction=reinterpret_cast<EclInstruction*>(address);
    }
    i32 integer_argument(u32 index, EclGlobals& globals);
    Extended float_argument(u32 index, EclGlobals& globals);
    i32 resolve_integer(u32 index, i32 value, EclGlobals& globals);
    Extended resolve_float(u32 index, float value, EclGlobals& globals);
    i32* integer_reference(u32 index, EclGlobals& globals);
    float* float_reference(u32 index, EclGlobals& globals);
    i32 update(float elapsed, EclServices& services);
};
static_assert(sizeof(void*) != 4 || (offsetof(EclContext, stack) == 8), "Reconstructed 32-bit runtime layout");
static_assert(sizeof(void*) != 4 || (offsetof(EclContext, owner) == 0x1014), "Reconstructed 32-bit runtime layout");
}
