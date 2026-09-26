#pragma once
#include <cstdint>
#if defined(TH10_IOS)
#include <SDL3/SDL.h>
#endif

namespace touhou::sdl {
struct NativePerformance {
    double logic=0,draw=0,flush=0,audio=0,present=0,loading=0,stream=0,submit=0;
    std::uint64_t ticks=0,draws=0,callbacks=0;
};
inline NativePerformance nativePerformance;
struct PerformanceTimer {
#if defined(TH10_IOS)
    double& total;
    std::uint64_t began;
    explicit PerformanceTimer(double& destination):total(destination),began(SDL_GetTicksNS()){}
    ~PerformanceTimer(){total+=double(SDL_GetTicksNS()-began)/1000000.;}
#else
    explicit PerformanceTimer(double&){}
#endif
};
}
