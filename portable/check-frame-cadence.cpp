#include "sdl/FrameCadence.hpp"
#include <cassert>
int main(){
 for(unsigned hz:{15u,20u,30u,60u,90u,120u,144u,165u}){
  touhou::sdl::FrameCadence clock;unsigned ticks=0;
  for(unsigned i=0;i<hz*60;++i)ticks+=clock.advance(1./hz);
  assert(ticks==3600);
 }
 touhou::sdl::FrameCadence clock;
 assert(clock.advance(.1)==4);assert(clock.advance(0)==2);
 assert(clock.advance(30)==4);assert(clock.advance(0)==2);
 clock.advance(.01);clock.reset();assert(clock.advance(0)==0);
 assert(clock.advance(-1)==0);
}
