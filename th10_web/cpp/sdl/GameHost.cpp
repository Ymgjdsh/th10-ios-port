#include "../platform/Application.hpp"
#include <SDL3/SDL.h>
#include "Exports.hpp"
#include "Renderer.hpp"
#include "NativeOptions.hpp"
#include "../game/TitleScores.hpp"
#include "../../../portable/input/TouchController.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <cmath>
#include <vector>
#include <map>
using namespace th10;using namespace th10::browser;
extern "C" {
FileSystem* files_create();void files_destroy(FileSystem*);u32 files_attach(FileSystem*,const char*);
Input* input_create();void input_destroy(Input*);GameState* game_state_create(Input*,u32);void game_state_destroy(GameState*);
GraphicsDevice* graphics_create(const GraphicsPresentation*,u32);void graphics_destroy(GraphicsDevice*);
AnimationEngine* animation_engine_create(FileSystem*,GraphicsDevice*,Rng*,Rng*,float*);void animation_engine_destroy(AnimationEngine*);
Fonts* fonts_create(GraphicsDevice*,Rng*,u32);void fonts_destroy(Fonts*);Audio* audio_create(FileSystem*);void audio_destroy(Audio*);
ScreenEffects* effects_create(AnimationEngine*,const u32*,const u32*);void effects_destroy(ScreenEffects*);
Application* application_create(FileSystem*,Input*,GameState*,AnimationEngine*,Fonts*,Audio*,ScreenEffects*);void application_destroy(Application*);
void application_touch_state(Application*,u32*);void sdl_loop_start(Application*);void sdl_loop_stop();void sdl_files_root(u32);
void sdl_loop_pause(u32);void sdl_keys_clear();void sdl_viewport_rect(float*,float*,float*,float*);
void sdl_audio_shutdown();void sdl_fonts_shutdown();void sdl_shutdown();
}
namespace {
struct Session {
    FileSystem* files=nullptr;Input* input=nullptr;GameState* state=nullptr;GraphicsDevice* device=nullptr;
    AnimationEngine* animation=nullptr;Fonts* fonts=nullptr;Audio* audio=nullptr;ScreenEffects* effects=nullptr;Application* app=nullptr;
    Rng random{},visual{};float rate=1;u32 quitting=0;
    ~Session(){sdl_loop_stop();if(app)application_destroy(app);if(effects)effects_destroy(effects);if(audio)audio_destroy(audio);sdl_audio_shutdown();if(fonts)fonts_destroy(fonts);sdl_fonts_shutdown();if(animation)animation_engine_destroy(animation);if(device)graphics_destroy(device);if(state)game_state_destroy(state);if(input)input_destroy(input);if(files)files_destroy(files);sdl_shutdown();}
};
std::unique_ptr<Session> session;
SDL_Joystick* controllers[2]{};
void close_controllers(){for(auto*& p:controllers)if(p){SDL_CloseJoystick(p);p=nullptr;}}
void add_controller(SDL_JoystickID id){for(const auto* p:controllers)if(p&&SDL_GetJoystickID(const_cast<SDL_Joystick*>(p))==id)return;for(auto*& p:controllers)if(!p){p=SDL_OpenJoystick(id);break;}}
void poll_controllers(InputSnapshot& input){for(u32 i=0;i<2;i++){auto* p=controllers[i];if(!p||!SDL_JoystickConnected(p))continue;input.connected[i]=1;auto& d=input.direct[i];auto& l=input.legacy[i];l.size=52;l.flags=255;
    for(int n=0;n<6;n++){const int value=n<SDL_GetNumJoystickAxes(p)?SDL_GetJoystickAxis(p,n):0;const double unit=value<0?value/32768.:value/32767.;d.axes[n]=i32(std::floor(unit*1000+.5));(&l.x)[n]=u32(std::floor((unit+1)*32767.5+.5));}
    for(int n=0;n<std::min(128,SDL_GetNumJoystickButtons(p));n++){const bool down=SDL_GetJoystickButton(p,n);d.buttons[n]=down?128:0;if(down&&n<32)l.buttons|=1u<<n;}
    const int hat=SDL_GetNumJoystickHats(p)?SDL_GetJoystickHat(p,0):0,x=((hat&SDL_HAT_RIGHT)||d.buttons[15]?1:0)-((hat&SDL_HAT_LEFT)||d.buttons[14]?1:0),y=((hat&SDL_HAT_DOWN)||d.buttons[13]?1:0)-((hat&SDL_HAT_UP)||d.buttons[12]?1:0);
    l.pov=(x||y)?u32((int(std::round(std::atan2(double(x),double(-y))*180/3.141592653589793))+360)%360)*100:~0u;d.pov[0]=l.pov;for(int n=1;n<4;n++)d.pov[n]=~0u;if(l.buttons)l.button_number=__builtin_ctz(l.buttons)+1;
}}
struct Key {const char* code;const char* sdl;u32 scan,vk;bool hosted=false;SDL_Scancode native=SDL_SCANCODE_UNKNOWN;};
#include "../../../portable/input/KeyboardMap.inc"
touhou::input::TouchController gestures;
#ifndef __EMSCRIPTEN__
std::map<SDL_FingerID,int> native_fingers;int next_finger=1;
#endif
touhou::input::TouchState touch_state(){touhou::input::TouchState s;if(!session||!session->app)return s;u32 raw[8];application_touch_state(session->app,raw);float values[5];std::memcpy(values,raw+3,20);
    s.context=raw[0];s.instance=raw[1];s.ready=raw[2];s.x=values[0];s.y=values[1];s.fast=values[2];s.slow=values[3];s.min_x=-184;s.max_x=184;s.min_y=32;s.max_y=432;return s;}
void cancel(){gestures.cancel();if(session&&session->app&&session->app->world)session->app->world->motion.target(0,0,0);}
void key(InputSnapshot& s,u32 scan,u32 vk){s.scan_keys[scan]=128;s.virtual_keys[vk]=128;if(vk>=160&&vk<=165)s.virtual_keys[16+(vk-160)/2]=128;}
void touch(int type,int id,float x,float y){gestures.pointer(type,id,x,y,SDL_GetTicks(),touch_state(),session&&session->input&&session->input->snapshot.virtual_keys[16]);}

}
extern "C" {
void sdl_native_events(Application* app){
    if(!session||session->app!=app)return;
    SDL_Event event;while(SDL_PollEvent(&event)){
        if(event.type==SDL_EVENT_FINGER_CANCELED){cancel();
#ifndef __EMSCRIPTEN__
            native_fingers.clear();
#endif
            continue;}
#ifndef __EMSCRIPTEN__
#ifndef TH10_IOS
        // UIKit owns the combined foreground/interruption state on iOS.
        // A queued focus event must not override an active audio interruption.
        if(event.type==SDL_EVENT_WILL_ENTER_BACKGROUND||event.type==SDL_EVENT_WINDOW_FOCUS_LOST){sdl_keys_clear();sdl_loop_pause(1);continue;}
        if(event.type==SDL_EVENT_DID_ENTER_FOREGROUND||event.type==SDL_EVENT_WINDOW_FOCUS_GAINED){sdl_keys_clear();sdl_loop_pause(0);continue;}
#endif
        if(event.type==SDL_EVENT_QUIT){session->quitting=1;sdl_loop_pause(1);continue;}
#endif
        if(event.type==SDL_EVENT_JOYSTICK_ADDED)add_controller(event.jdevice.which);
        if(event.type==SDL_EVENT_JOYSTICK_REMOVED)for(auto*& p:controllers)if(p&&SDL_GetJoystickID(p)==event.jdevice.which){SDL_CloseJoystick(p);p=nullptr;}
        if(event.type==SDL_EVENT_FINGER_DOWN||event.type==SDL_EVENT_FINGER_MOTION||event.type==SDL_EVENT_FINGER_UP){
            const int type=event.type==SDL_EVENT_FINGER_DOWN?0:event.type==SDL_EVENT_FINGER_MOTION?1:2;
#ifdef __EMSCRIPTEN__
            touch(type,int(event.tfinger.fingerID),event.tfinger.x,event.tfinger.y);
#else
            float x,y,w,h;sdl_viewport_rect(&x,&y,&w,&h);if(w<=0||h<=0)continue;
            const float tx=(event.tfinger.x-x)/w,ty=(event.tfinger.y-y)/h;
            if(type==0){if(tx<0||tx>1||ty<0||ty>1)continue;native_fingers[event.tfinger.fingerID]=next_finger++;}
            auto finger=native_fingers.find(event.tfinger.fingerID);if(finger==native_fingers.end())continue;
            touch(type,finger->second,tx,ty);if(type==2)native_fingers.erase(finger);
#endif
        }
    }
}
TH_SDL_EXPORT("sdl_native_input") void sdl_native_input(Application* app){
    if(!session||session->app!=app)return;
    sdl_native_events(app);
    auto& snapshot=app->input.snapshot;std::memset(&snapshot,0,sizeof(snapshot));snapshot.focused=1;
    const bool* physical=SDL_GetKeyboardState(nullptr);
    for(const auto& k:keyboard_map)if(k.hosted||(k.native!=SDL_SCANCODE_UNKNOWN&&physical[k.native]))key(snapshot,k.scan,k.vk);
    poll_controllers(snapshot);
    const auto state=touch_state();
    auto sample=gestures.sample(state,SDL_GetTicks(),snapshot.virtual_keys[16],snapshot.virtual_keys[37]||snapshot.virtual_keys[38]||snapshot.virtual_keys[39]||snapshot.virtual_keys[40]);
    #if defined(TH10_IOS)
    const auto& options=touhou::sdl::nativeOptions;
    if(app->world)app->world->always_hitbox=options.hitbox;
    if(state.context==1&&state.ready){sample.keys[90]|=options.autoFire||(options.dragFire&&gestures.active());sample.keys[16]|=options.autoSlow;}
    if(options.unlocked&&app->startup&&app->startup->scores&&app->startup->scores->data){
        auto& score=*app->startup->scores->data;unlock_score_records(score);std::memset(score.settings.statistics+0x11,1,18);
    }
#endif
    for(const auto& k:keyboard_map)if(sample.keys[k.vk]||(k.vk>=160&&k.vk<=165&&sample.keys[16+(k.vk-160)/2]))key(snapshot,k.scan,k.vk);
    if(app->world)app->world->motion.target(sample.motion,sample.x,sample.y);
}
#define EXPORT(name) TH_SDL_EXPORT(name)
EXPORT("sdl_game_open") Application* sdl_game_open(u32 chinese,u32 seed){
    // A retry must release the old session before the singleton host registries
    // create any new objects.
    if(session){close_controllers();session.reset();}
    gestures.reset();
    for(auto& k:keyboard_map)k.native=SDL_GetScancodeFromName(k.sdl);
    close_controllers();SDL_InitSubSystem(SDL_INIT_JOYSTICK);int controller_count=0;auto* ids=SDL_GetJoysticks(&controller_count);for(int i=0;i<controller_count;i++)add_controller(ids[i]);SDL_free(ids);
    session=std::make_unique<Session>();auto& s=*session;sdl_files_root(chinese);u32 rng[]{seed,0};std::memcpy(&s.random,rng,8);std::memcpy(&s.visual,rng,8);
    s.files=files_create();if(!s.files||!files_attach(s.files,chinese?"th10c.dat":"th10.dat")){session.reset();return nullptr;}
    const u32 parameters[]{640,480,22,1,0,0,1,0,1,1,80,0,0,0};s.input=input_create();s.state=game_state_create(s.input,chinese);s.device=graphics_create(reinterpret_cast<const GraphicsPresentation*>(parameters),0x40);if(!s.device){session.reset();return nullptr;}
    s.animation=animation_engine_create(s.files,s.device,&s.random,&s.visual,&s.rate);s.fonts=fonts_create(s.device,&s.random,chinese);s.audio=audio_create(s.files);s.effects=effects_create(s.animation,&s.quitting,nullptr);
    s.app=application_create(s.files,s.input,s.state,s.animation,s.fonts,s.audio,s.effects);return s.app;
}
EXPORT("sdl_game_close") void sdl_game_close(){cancel();close_controllers();session.reset();}
EXPORT("sdl_key") void sdl_key(const char* code,u32 down){for(auto& k:keyboard_map)if(!std::strcmp(code,k.code)){k.hosted=down!=0;return;}}
EXPORT("sdl_keys_clear") void sdl_keys_clear(){for(auto& k:keyboard_map)k.hosted=false;gestures.reset();gestures.fire=gestures.focus=false;gestures.stick_x=gestures.stick_y=0;cancel();
#ifndef __EMSCRIPTEN__
    native_fingers.clear();
#endif
}
EXPORT("sdl_touch") void sdl_touch(u32 type,i32 id,float x,float y){touch(type,id,x,y);}
EXPORT("sdl_touch_cancel") void sdl_touch_cancel(){cancel();}
EXPORT("sdl_touch_options") void sdl_touch_options(u32 on,u32 free,float speed){gestures.enabled=on;gestures.unlimited=free;gestures.sensitivity=std::clamp(speed,.1f,5.f);if(!on)cancel();}
EXPORT("sdl_touch_gestures") void sdl_touch_gestures(u32 two,u32 taps){gestures.two_finger=two;gestures.double_tap=taps;}
EXPORT("sdl_touch_mode") void sdl_touch_mode(u32 mode){gestures.mode=mode;cancel();}
EXPORT("sdl_touch_controls") void sdl_touch_controls(u32 shoot,u32 slow,u32 bomb,u32 escape,float x,float y){gestures.controls(shoot,slow,bomb,escape,x,y);}
EXPORT("sdl_resource_stats") const u32* sdl_resource_stats(){static u32 out[4]{};if(session&&session->files){auto& f=*session->files;out[0]=f.cache_bytes;out[1]=f.cache_hits;out[2]=f.cache_misses;out[3]=f.decoded.size();}return out;}
EXPORT("sdl_game_status") const i32* sdl_game_status(){static i32 result[10]{};if(session&&session->app){auto& a=*session->app;result[0]=a.value.screen;result[1]=a.state.game.stage;result[2]=a.error;result[3]=a.state.game.lives;result[4]=a.state.game.power;result[5]=gestures.current_context();result[6]=gestures.active();result[7]=gestures.fire;result[8]=gestures.focus;result[9]=a.input.player_profiles[0].input.raw;}return result;}
}

extern "C" {
void sdl_native_options(int fire,int slow,int drag,int bomb,int hitbox){auto& o=touhou::sdl::nativeOptions;o.autoFire=fire;o.autoSlow=slow;o.dragFire=drag;o.autoBomb=bomb;o.hitbox=hitbox;}
int sdl_unlock_all(){
    touhou::sdl::nativeOptions.unlocked=true;
    if(!session||!session->app||!session->app->startup||!session->app->startup->scores)return 0;
    auto& scores=*session->app->startup->scores;if(!scores.data)return 0;
    unlock_score_records(*scores.data);std::memset(scores.data->settings.statistics+0x11,1,18);
    return scores.save()==0;
}
int sdl_developer_action(int action,int value){
    if(action==0){touhou::sdl::nativeOptions.invincible=value!=0;return 1;}
    if(!session||!session->app||!session->app->world)return 0;
    auto& w=*session->app->world;auto& g=w.state.game;
    if(!w.actors.player||!w.actors.session||!w.state.replay||w.state.replay->mode!=0)return 0;
    switch(action){
      case 1:g.score=999999999;g.score_units=0;break;
      case 2:g.power=100;w.configure_player();if(w.hud)w.hud->update_power(5,0);break;
      case 3:g.lives=9;if(w.actors.gui)w.actors.gui->update_lives(9);break;
      case 4:w.cancel_bullets(false);w.cancel_lasers(false);break;
      case 5:for(int i=0;i<8;i++)w.spawn_item({w.actors.player->position.x+float((i-4)*12),w.actors.player->position.y-40,0},i%2?1:4,0xffffff,0,0);break;
      default:return 0;
    }return 1;
}
}
