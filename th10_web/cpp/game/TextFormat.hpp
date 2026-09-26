#pragma once
#include "Types.hpp"
namespace th10 {
// Game numbers use 32-bit Windows argument words while string pointers use
// native-width TextWord storage; this formatter does not call the old CRT or execute code tokens.
// Returns the full output length, or -1 for an unsupported/invalid format.
i32 format_text(char* output,u32 capacity,const char* format,const TextWord* words,u32 word_count=0xffffffffu) noexcept;
}
