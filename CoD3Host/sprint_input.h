#pragma once
#include <cstdint>
namespace SprintInput {
struct State {bool running=false,wasDown=false;};
inline uint16_t Filter(State& state,uint16_t buttons,int16_t forward,uint8_t aim,uint8_t fire,bool toggle,bool scopedBreath) {
 constexpr uint16_t L3=0x0040;
 bool down=(buttons&L3)!=0;
 if(toggle) {
  if(aim>30 && scopedBreath) {
   // Preserve the actual held press for native +sprintbreath. Do not
   // synthesize a press from the sprint latch while aiming.
   state.running=false;
  } else {
   if(down&&!state.wasDown) state.running=!state.running;
   if(forward<8000||aim>30||fire>30) state.running=false;
   if(state.running) buttons|=L3;else buttons&=uint16_t(~L3);
  }
 } else state.running=false;
 state.wasDown=down;
 return buttons;
}
}
