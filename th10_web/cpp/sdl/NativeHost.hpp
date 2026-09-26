#pragma once
#include <cstdint>
#include <SDL3/SDL.h>

namespace th10::browser { struct Application; }

// Main-thread APIs for the native app shell. Timestamps are monotonic seconds;
// the shared cadence, not the display refresh rate, determines game ticks.
extern "C" {
th10::browser::Application* sdl_game_open(std::uint32_t chinese, std::uint32_t seed);
void sdl_game_close();
void sdl_loop_start(th10::browser::Application*);
int sdl_loop_frame(double timestamp_seconds);
void sdl_loop_pause(std::uint32_t paused);
void sdl_loop_stop();
int sdl_loop_is_paused();
void sdl_native_events(th10::browser::Application*);

// resource_root contains game/, fonts/, music/. nullptr selects bundle assets.
// save_root is writable app-private storage. nullptr selects SDL_GetPrefPath.
void sdl_paths(const char* resource_root, const char* save_root);
const char* sdl_resource_path(const char* relative_path);
const char* sdl_save_path();
const char* sdl_file_error();
SDL_Window* sdl_window();

// Normalized top-left viewport in the SDL window. The shell can reserve space
// for safe areas and controls; width/height <= 0 restores automatic 4:3 fit.
void sdl_set_viewport(float x, float y, float width, float height);
void sdl_viewport_rect(float* x, float* y, float* width, float* height);
void sdl_key(const char* code, std::uint32_t down);
void sdl_keys_clear();
// Touch coordinates here are normalized within the 640x480 game viewport.
void sdl_touch(std::uint32_t type, std::int32_t id, float x, float y);
void sdl_touch_cancel();
void sdl_touch_options(std::uint32_t on, std::uint32_t free_motion, float speed);
void sdl_touch_gestures(std::uint32_t two, std::uint32_t taps);
void sdl_touch_mode(std::uint32_t mode);
void sdl_touch_controls(std::uint32_t shoot, std::uint32_t slow,
                        std::uint32_t bomb_serial, std::uint32_t pause_serial,
                        float stick_x, float stick_y);
const std::int32_t* sdl_game_status();
const char* sdl_error();
void sdl_native_options(int fire,int slow,int drag,int bomb,int hitbox);
int sdl_unlock_all();
int sdl_developer_action(int action,int value);
void sdl_display_options(int fps,int smooth);

}
