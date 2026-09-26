#pragma once
#include <cstdint>

namespace touhou::ios {
// Each owner clears only its own reason. A foreground notification must not
// resume the game during an audio interruption or after a terminal result.
class Lifecycle {
public:
    enum Reason : std::uint32_t { inactive=1, audioInterruption=2, stopped=4, settings=8 };

    bool running() const { return reasons==0; }
    bool set(Reason reason,bool paused) {
        const bool wasRunning=running();
        if(paused)reasons|=reason;
        else reasons&=~std::uint32_t(reason);
        return wasRunning!=running();
    }

private:
    std::uint32_t reasons=0;
};
}
