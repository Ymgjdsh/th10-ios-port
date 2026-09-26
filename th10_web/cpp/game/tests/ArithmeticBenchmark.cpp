#include "../Arithmetic.hpp"
#include "../GameMath.hpp"
#include "../Movement.hpp"
#include "../../../../portable/numeric/ExactFloat.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>

// Standalone benchmark: this file is outside the game's source glob. Compile
// normally for timings, or rename the corresponding SoftFloat C entry points
// to bench_* and define TH10_BENCH_SOFTFLOAT_COUNTERS for fallback counts.
using namespace th10;
namespace {
struct Counters {u64 add=0,sub=0,mul=0,div=0,to_float=0,round=0;} counters;
volatile u32 checksum;
struct FloatBits32 {u32 value;};
}
#ifdef TH10_BENCH_SOFTFLOAT_COUNTERS
extern "C" {
void bench_extF80M_add(const Extended*,const Extended*,Extended*);
void bench_extF80M_sub(const Extended*,const Extended*,Extended*);
void bench_extF80M_mul(const Extended*,const Extended*,Extended*);
void bench_extF80M_div(const Extended*,const Extended*,Extended*);
FloatBits32 bench_extF80M_to_f32(const Extended*);
void bench_extF80M_roundToInt(const Extended*,u8,bool,Extended*);
void extF80M_add(const Extended* a,const Extended* b,Extended* out){++counters.add;bench_extF80M_add(a,b,out);}
void extF80M_sub(const Extended* a,const Extended* b,Extended* out){++counters.sub;bench_extF80M_sub(a,b,out);}
void extF80M_mul(const Extended* a,const Extended* b,Extended* out){++counters.mul;bench_extF80M_mul(a,b,out);}
void extF80M_div(const Extended* a,const Extended* b,Extended* out){++counters.div;bench_extF80M_div(a,b,out);}
FloatBits32 extF80M_to_f32(const Extended* a){++counters.to_float;return bench_extF80M_to_f32(a);}
void extF80M_roundToInt(const Extended* a,u8 rounding,bool exact,Extended* out){++counters.round;bench_extF80M_roundToInt(a,rounding,exact,out);}
}
#endif
namespace {
float inputs[4096];
// Candidate only: integer-bit rounding avoids changing the process FP mode.
// Finite f32 input rounds exactly to an integral f32 with ties to even.
float nearest_integral(float x){
    const u32 word=touhou::numeric::bits(x),sign=word&0x80000000u;
    const u32 magnitude=word&0x7fffffffu,exponent=magnitude>>23;
    if(exponent>=150)return x;
    if(exponent<126 || magnitude==0x3f000000u)return touhou::numeric::value(sign);
    if(exponent==126)return touhou::numeric::value(sign|0x3f800000u);
    const u32 shift=150-exponent,mask=(1u<<shift)-1,half=1u<<(shift-1);
    u32 integral=magnitude&~mask;const u32 fraction=magnitude&mask;
    if(fraction>half || (fraction==half && (integral&(1u<<shift))))integral+=1u<<shift;
    return touhou::numeric::value(sign|integral);
}
template<class F> void bench(const char* name,u32 iterations,F calculate){
    counters={};u32 sum=0;const auto begin=std::chrono::steady_clock::now();
    for(u32 i=0;i<iterations;++i)sum+=touhou::numeric::bits(calculate(inputs[i&4095]));
    const auto end=std::chrono::steady_clock::now();checksum=sum;
    const double ns=std::chrono::duration<double,std::nano>(end-begin).count()/iterations;
    std::printf("%s,%.2f,%u,%llu,%llu,%llu,%llu,%llu,%llu\n",name,ns,sum,
        static_cast<unsigned long long>(counters.add),static_cast<unsigned long long>(counters.sub),
        static_cast<unsigned long long>(counters.mul),static_cast<unsigned long long>(counters.div),
        static_cast<unsigned long long>(counters.to_float),static_cast<unsigned long long>(counters.round));
}
}
int main(int argc,char** argv){
    const u32 iterations=argc>1?static_cast<u32>(std::strtoul(argv[1],nullptr,10)):1000000;
    for(u32 i=0;i<4096;++i)inputs[i]=static_cast<float>(static_cast<i32>(i)-2048)*0.0009765625f;
    arithmetic_mode(Precision::Single,Rounding::NearestEven);
    u32 state=0x19831012,compared=0;
    for(u32 i=0;i<1000000;++i){
        state=state*1664525u+1013904223u;
        if((state&0x7f800000u)==0x7f800000u)continue;
        const float x=touhou::numeric::value(state);
        const float original=number(x).round_to_integer().to_float();
        if(touhou::numeric::bits(original)!=touhou::numeric::bits(nearest_integral(x))){std::fprintf(stderr,"nearest-even mismatch: %08x\n",state);return 1;}
        ++compared;
    }
    std::fprintf(stderr,"finite nearest-even comparisons passed: %u\n",compared);
    std::printf("case,ns_per_iteration,checksum,soft_add,soft_sub,soft_mul,soft_div,soft_to_f32,soft_round\n");
    bench("extended_move",iterations,[](float x){return (number(x)*number(1.25f)+number(180.f)).to_float();});
    bench("scalar_move",iterations,[](float x){return Scalar::add(Scalar::mul(x,1.25f),180.f);});
    bench("raw_bounded_move",iterations,[](float x){return x*1.25f+180.f;});
    bench("polar_current",iterations,[](float x){const auto p=polar(x,3.5f);return p.x+p.y;});
    bench("cosine_to_float",iterations,[](float x){return cosine(number(x)).to_float();});
    bench("sprite_alignment",iterations,[](float x){return (number(x).round_to_integer()-number(.5f)).to_float();});
    bench("sprite_alignment_candidate",iterations,[](float x){return Scalar::sub(nearest_integral(x),.5f);});
    bench("movement_current",iterations,[](float x){Movement m{};m.position={x*100.f,x*75.f,0};m.velocity={1.25f,.75f,0};m.update();return m.position.x+m.position.y;});
    arithmetic_mode(Precision::Extended,Rounding::NearestEven);
    bench("extended80_move",iterations,[](float x){return (number(x)*number(1.25f)+number(180.f)).to_float();});
}
