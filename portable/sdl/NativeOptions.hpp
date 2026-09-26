#pragma once
namespace touhou::sdl {
// Native shell options are separate from the original serialized game config.
struct NativeOptions {bool autoFire=false,autoSlow=false,dragFire=false,autoBomb=false,hitbox=false,invincible=false,unlocked=false;};
inline NativeOptions nativeOptions;
}
