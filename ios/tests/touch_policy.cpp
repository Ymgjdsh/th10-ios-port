#include "../../portable/input/TouchController.hpp"
#include <cassert>
#include <cstdio>
using namespace touhou::input;
int main(){
  TouchController input;
  TouchState battle{1,1,true,0,240,4,2,-184,32,184,432};
  input.controls(true,true,0,0,0,0);
  auto keys=input.sample(battle,0,false,false);
  assert(keys.keys[90]&&keys.keys[16]&&!keys.keys[88]);
  input.pointer(0,11,.5f,.5f,10,battle,false);
  input.pointer(1,11,.6f,.6f,20,battle,false);
  auto motion=input.sample(battle,20,false,false);
  assert(motion.motion==1&&motion.x>0&&motion.x<64&&motion.y>240);
  input.controls(true,false,1,0,0,0);
  assert(input.sample(battle,30,false,false).keys[88]);
  input.pointer(2,11,.6f,.6f,40,battle,false);
  assert(input.sample(battle,40,false,false).motion==0);
  input.controls(false,false,1,1,0,0);
  assert(input.sample(battle,50,false,false).keys[27]);
  input.reset();input.controls(false,false,1,1,0,0);
  auto neutral=input.sample(battle,60,false,false);
  assert(!neutral.keys[90]&&!neutral.keys[16]&&!neutral.keys[88]&&!neutral.keys[27]);
  TouchState dialogue=battle;dialogue.context=2;
  input.pointer(0,12,.3f,.3f,100,dialogue,false);
  input.pointer(2,12,.3f,.3f,180,dialogue,false);
  assert(input.sample(dialogue,180,false,false).keys[90]);
  input.reset();
  input.pointer(0,13,.4f,.4f,200,dialogue,false);
  assert(input.sample(dialogue,800,false,false).keys[17]);
  input.pointer(2,13,.4f,.4f,810,dialogue,false);
  auto released=input.sample(dialogue,820,false,false);
  assert(!released.keys[90]&&!released.keys[17]);
  // Menus remain controllable by both directional drags and confirm taps.
  TouchState menu=battle;menu.context=0;menu.ready=false;
  input.pointer(0,14,.4f,.4f,900,menu,false);
  input.pointer(1,14,.4f,.6f,950,menu,false);
  assert(input.sample(menu,950,false,false).keys[40]);
  input.pointer(2,14,.4f,.6f,980,menu,false);
  auto menuReleased=input.sample(menu,980,false,false);
  assert(!menuReleased.keys[40]&&!menuReleased.keys[90]);
  input.pointer(0,15,.4f,.4f,1000,menu,false);
  input.pointer(2,15,.4f,.4f,1050,menu,false);
  assert(input.sample(menu,1050,false,false).keys[90]);
  // Rotation/suspension uses reset: no held motion or queued action survives.
  input.controls(true,true,2,2,0,0);
  input.reset();input.controls(false,false,2,2,0,0);
  auto cancelled=input.sample(battle,1100,false,false);
  assert(!cancelled.motion&&!cancelled.keys[90]&&!cancelled.keys[16]&&!cancelled.keys[88]&&!cancelled.keys[27]);
  std::puts("PASS: touch movement, focus, bomb, pause, cancellation, menus and dialogue");
}
