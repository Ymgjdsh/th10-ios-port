#include "../../th10_web/cpp/game/EclProgram.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
using namespace th10;
struct Services : EclServices {
    i32 integer(i32) override { return 0; }
    Extended floating(i32) override { return number(0); }
    i32* integer_reference(i32) override { return nullptr; }
    float* float_reference(i32) override { return nullptr; }
    i32 command(EclContext&) override { return 0; }
    void* allocate(u32 n) override { return std::malloc(n); }
    void release(void* p) override { std::free(p); }
};
int main(){
    // Start at a backward branch. The earlier instruction terminates the
    // script, so an unsigned +4 GiB displacement cannot pass silently.
    struct Code { EclInstruction end; EclInstruction jump; u32 offset,time; } code{};
    code.end={0,1,16,0,255,0,0};
    code.jump={0,12,24,0,255,2,0};code.offset=0xfffffff0;code.time=0;
    EclContext context{};context.instruction=&code.jump;context.difficulty=255;
    Services services;
    assert(context.update(1,services)==-1);
    assert(!context.instruction);
    std::puts("PASS: ECL backward branch uses signed native pointer displacement");
}
