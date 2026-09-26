#include "../platform/Application.hpp"
#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#include <cmath>
#include "FrameCadence.hpp"
#include "Renderer.hpp"
#include "Exports.hpp"
#include "NativePerformance.hpp"
#include <algorithm>

extern "C" void sdl_audio_pump();
extern "C" void sdl_audio_pause(th10::u32);
extern "C" void sdl_native_input(th10::browser::Application*);
extern "C" void sdl_native_events(th10::browser::Application*);
#ifdef __EMSCRIPTEN__
EM_JS(int, browser_prepare_frame, (), { return Module['runtimePrepare'] ? Module['runtimePrepare']() : 0; });
EM_JS(void, browser_finish_frame, (int result,double milliseconds), { Module['runtimeFinish'](result,milliseconds); });
EM_JS(void, browser_loop_stopped, (), { if(Module['runtimeStopped'])Module['runtimeStopped'](); });
#endif
namespace {
th10::browser::Application* application=nullptr;
unsigned loop_epoch=0;bool running=false,suspended=false;double elapsed=0,last=-1,audio_remainder=0,callback_begin=0;touhou::sdl::FrameCadence cadence;
double host_now(){
#ifdef __EMSCRIPTEN__
    return emscripten_get_now();
#else
    return double(SDL_GetTicksNS())/1000000.;
#endif
}
#ifdef __EMSCRIPTEN__
EM_BOOL frame(double timestamp,void* epoch){
    if(!running||uintptr_t(epoch)!=loop_epoch)return EM_FALSE;
    const double now=timestamp/1000.,delta=last<0?0:std::max(0.,now-last);last=now;callback_begin=emscripten_get_now();
    // Browser responsibilities end at resource readiness and input snapshots.
    // The C++ ApplicationLoop owns deadlines, logic, draw and the original
    // original 60Hz cadence, independent of display callback frequency.
    const int ready=browser_prepare_frame();if(!running)return EM_FALSE;
    if(ready<=0||suspended){sdl_audio_pause(1);cadence.reset();return EM_TRUE;}
    sdl_audio_pause(0);elapsed+=delta;audio_remainder+=delta*1000;
    const auto milliseconds=th10::u32(std::floor(audio_remainder));audio_remainder-=milliseconds;
    application->audio.advance(milliseconds);
    if(application->world&&application->world->loading){
        // Share the exact object creation sequence with the synchronous test
        // entry point, but yield between owners on the browser thread. Keep
        // gameplay/RNG/recording frozen until the original loading barrier.
        const double deadline=emscripten_get_now()+2.;
        do{application->world->advance_loading_step();}while(application->world->loading&&emscripten_get_now()<deadline);
        application->sync_views();
        if(application->world->loading){application->engine.device.present_frame();sdl_audio_pump();browser_finish_frame(0,emscripten_get_now()-callback_begin);cadence.reset();return EM_TRUE;}
    }
    // Some Emscripten SDL builds fall back to millisecond gettimeofday for
    // performance counters. Use the display's timestamp for cadence, avoiding
    // a late/early callback's CPU work moving the next deadline across a VSync.
    const auto ticks=cadence.advance(delta);int result=0;sdl_defer(1);
    for(unsigned i=0;i<ticks&&!result;++i){sdl_native_input(application);result=application->step(true);}
    sdl_commit();sdl_defer(0);
    sdl_audio_pump();browser_finish_frame(result,emscripten_get_now()-callback_begin);
    return running?EM_TRUE:EM_FALSE;
}
#else
int native_frame(double timestamp){
    if(!running||!application)return 0;
    // Poll lifecycle events even while suspended so foreground can resume.
    sdl_native_events(application);
    if(!running||!application)return 0;
    callback_begin=host_now();
    const double delta=last<0?0:std::max(0.,timestamp-last);last=timestamp;
    if(suspended){sdl_audio_pause(1);cadence.reset();return 0;}
    sdl_audio_pause(0);elapsed+=delta;audio_remainder+=delta*1000;
    const auto milliseconds=th10::u32(std::floor(audio_remainder));audio_remainder-=milliseconds;
    {touhou::sdl::PerformanceTimer timer(touhou::sdl::nativePerformance.audio);application->audio.advance(milliseconds);}
    ++touhou::sdl::nativePerformance.callbacks;
    if(application->world&&application->world->loading){
        touhou::sdl::PerformanceTimer timer(touhou::sdl::nativePerformance.loading);
        const double deadline=host_now()+2.;
        do{application->world->advance_loading_step();}while(application->world->loading&&host_now()<deadline);
        application->sync_views();
        if(application->world->loading){application->engine.device.present_frame();sdl_audio_pump();cadence.reset();return 0;}
    }
    const auto ticks=cadence.advance(delta);int result=0;sdl_defer(1);
    for(unsigned i=0;i<ticks&&!result&&!suspended;++i){sdl_native_input(application);if(!suspended)result=application->step(true);}
    {touhou::sdl::PerformanceTimer timer(touhou::sdl::nativePerformance.present);sdl_commit();}
    sdl_defer(0);
    {touhou::sdl::PerformanceTimer timer(touhou::sdl::nativePerformance.audio);sdl_audio_pump();}
    return result;
}
#endif
}
extern "C" {
TH_SDL_EXPORT("sdl_loop_time") double sdl_loop_time(){return elapsed+(running&&!suspended?std::max(0.,host_now()-callback_begin)/1000.:0.);}
// A deterministic, stopped-loop entry point for replay/regression runners.
// It shares native input, audio progression and the same Application tick.
TH_SDL_EXPORT("sdl_loop_tick") int sdl_loop_tick(th10::browser::Application* app,double seconds,th10::u32 milliseconds){
    if(running)return -1;elapsed+=seconds;app->audio.advance(milliseconds);sdl_native_input(app);return app->step(true);
}
TH_SDL_EXPORT("sdl_loop_pause") void sdl_loop_pause(th10::u32 pause){
#ifndef __EMSCRIPTEN__
    if(pause&&!suspended&&application)application->save();
#endif
    suspended=pause!=0;last=-1;cadence.reset();sdl_audio_pause(pause);
}
int sdl_loop_is_paused(){return suspended;}
TH_SDL_EXPORT("sdl_loop_start") void sdl_loop_start(th10::browser::Application* app){
    application=app;elapsed=audio_remainder=0;cadence.reset();last=-1;callback_begin=host_now();running=true;
#ifndef __EMSCRIPTEN__
    suspended=false;
#endif
#ifdef __EMSCRIPTEN__
    emscripten_request_animation_frame_loop(frame,reinterpret_cast<void*>(uintptr_t(++loop_epoch)));
#endif
}
TH_SDL_EXPORT("sdl_loop_stop") void sdl_loop_stop(){
    if(!running)return;running=false;++loop_epoch;sdl_audio_pause(1);
#ifdef __EMSCRIPTEN__
    browser_loop_stopped();
#endif
    application=nullptr;
#ifndef __EMSCRIPTEN__
    elapsed=audio_remainder=0;last=-1;suspended=false;cadence.reset();
#endif
}
#ifndef __EMSCRIPTEN__
int sdl_loop_frame(double timestamp_seconds){return native_frame(timestamp_seconds);}
#endif
}
