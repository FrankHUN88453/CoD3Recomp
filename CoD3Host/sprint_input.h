#pragma once
#include <cstdint>
namespace SprintInput {
struct State {bool running=false,wasDown=false;};
// A stick pushed this far forward when the left stick is clicked: a run.
// Less, standing or walking: the campaign's binoculars (below). Half way:
// a click pushes the stick a little on its own (a pad read 12358 forward,
// a third of the way, as its player held it in to raise the binoculars).
constexpr int16_t RunStart=16000;
// standing: the campaign's. L3 held there is the binoculars (the game's
// own control: click and hold the left stick, standing, to raise them), so
// with the stick not well forward and no run latched the real press goes
// through as it is held. Without it a press standing still started a run
// that ended on the spot, and the game never saw L3 at all: the campaign's
// binoculars could not be raised. A press with the stick only a little
// forward (under RunStart) latched a run too, and the run kept the press
// from the binoculars (movement_fixes.cpp): a click that pushed the stick
// a third of the way forward never raised them.
inline uint16_t Filter(State& state,uint16_t buttons,int16_t forward,uint8_t aim,uint8_t fire,bool toggle,bool scopedBreath,bool standing=false) {
 constexpr uint16_t L3=0x0040;
 bool down=(buttons&L3)!=0;
 if(toggle) {
  if(aim>30 && scopedBreath) {
   // Preserve the actual held press for native +sprintbreath. Do not
   // synthesize a press from the sprint latch while aiming.
   state.running=false;
  } else {
   const bool press=down&&!state.wasDown;
   if(press && (!standing || state.running || forward>=RunStart)) state.running=!state.running;
   if(forward<8000||aim>30||fire>30) state.running=false;
   if(state.running) buttons|=L3;
   else if(!(standing && down && forward<RunStart)) buttons&=uint16_t(~L3);
  }
 } else state.running=false;
 state.wasDown=down;
 return buttons;
}
}
