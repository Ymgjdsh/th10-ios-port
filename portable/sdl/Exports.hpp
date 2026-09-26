#pragma once

// Named Wasm exports are not supported by the native Mach-O toolchain.
#ifdef __EMSCRIPTEN__
#define TH_SDL_EXPORT(name) __attribute__((export_name(name)))
#else
#define TH_SDL_EXPORT(name)
#endif
