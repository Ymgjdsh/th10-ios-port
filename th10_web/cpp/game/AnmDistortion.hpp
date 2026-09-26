#pragma once
#include "AnmManager.hpp"
#include "Rng.hpp"
namespace th10 {
struct AnmDistortionEnvironment {
    Rng* random;
    u32 update_callback,draw_callback;
    virtual void* allocate(u32 bytes)=0;
    virtual void release(void* bytes)=0;
};
struct AnmDistortion {
    AnmVertex vertices[33];
    float radii[33];
    float radial_velocity[33];
    float u_velocity,v_velocity;
    u32 reserved;
    static i32 initialize(AnmVm& vm,AnmDistortionEnvironment& environment);
    i32 update(const AnmVm& vm) noexcept;
};
static_assert(sizeof(void*) != 4 || (sizeof(AnmDistortion)==0x4b0), "Reconstructed 32-bit runtime layout");
static_assert(sizeof(void*) != 4 || (offsetof(AnmDistortion,radii)==0x39c), "Reconstructed 32-bit runtime layout");
static_assert(sizeof(void*) != 4 || (offsetof(AnmDistortion,u_velocity)==0x4a4), "Reconstructed 32-bit runtime layout");
}
