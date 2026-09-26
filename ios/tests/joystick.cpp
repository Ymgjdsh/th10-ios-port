#include "../Joystick.hpp"
#include "../../portable/input/TouchController.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main(){
    using touhou::ios::joystickSample;
    const auto center=joystickSample(0,0,44);
    assert(center.x==0&&center.y==0);
    const auto dead=joystickSample(4,4,44);
    assert(dead.x==0&&dead.y==0&&dead.knobX>0);
    const auto edge=joystickSample(44,0,44);
    assert(edge.x==1&&edge.y==0);
    const auto diagonal=joystickSample(100,-100,44);
    assert(std::abs(std::hypot(diagonal.x,diagonal.y)-1)<.00001f);
    assert(diagonal.x>0&&diagonal.y<0);
    assert(joystickSample(1,1,0).x==0);
    assert(joystickSample(std::numeric_limits<float>::quiet_NaN(),1,44).x==0);

    touhou::input::TouchController input;
    touhou::input::TouchState battle{1,1,true,0,240,4,2,-184,32,184,432};
    input.mode=3;input.controls(true,false,0,0,edge.x*32767,edge.y*32767);
    auto fast=input.sample(battle,0,false,false);
    assert(fast.motion==1&&fast.x==4&&fast.y==240&&fast.keys[90]);
    input.controls(true,true,0,0,edge.x*32767,0);
    auto slow=input.sample(battle,1,false,false);
    assert(slow.motion==1&&slow.x==2&&slow.keys[16]&&slow.keys[90]);
    auto menu=battle;menu.context=0;
    assert(input.sample(menu,2,false,false).keys[39]);

    // A release cancels joystick movement, but keeps a separately held fire
    // button. Restoring mode 0 permits a new drag in the game viewport.
    input.mode=0;input.cancel();input.controls(true,false,0,0,0,0);
    auto released=input.sample(battle,3,false,false);
    assert(released.motion==0&&released.keys[90]);
    input.pointer(0,7,.5f,.5f,4,battle,false);
    input.pointer(1,7,.6f,.5f,5,battle,false);
    assert(input.sample(battle,5,false,false).motion==1);
    input.reset();input.controls(false,false,0,0,0,0);
    auto cancelled=input.sample(battle,6,false,false);
    assert(cancelled.motion==0&&!cancelled.keys[90]&&!cancelled.keys[16]);
    std::puts("PASS: joystick dead zone, radial clamp, slow/fire multitouch policy, menus, release and restored viewport drag");
}
