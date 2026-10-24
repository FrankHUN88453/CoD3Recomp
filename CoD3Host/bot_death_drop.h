#pragma once
#include <cstdint>
#include "bot_weapon_selection.h"
namespace BotDeathDrop {
struct State {
    uint32_t entity=0,weapon=0;
    bool captured=false,attempted=false;
    void NewLife(uint32_t owner) {entity=owner;weapon=0;captured=attempted=false;}
    // Native respawn may complete after its initiating call has returned.
    // The caller invokes this only after observing positive health.
    void ObserveAlive(uint32_t owner) {
        if(entity!=owner || captured || attempted)NewLife(owner);
    }
    template<class Owned>
    void Capture(uint32_t owner,uint32_t held,uint32_t primary,uint32_t secondary,Owned owned) {
        if(entity!=owner)NewLife(owner);
        if(captured)return;
        captured=true;
        weapon=BotWeaponSelection::Recover(held,primary,secondary,owned);
    }
    bool Begin(uint32_t owner,uint32_t& selected) {
        if(entity!=owner||!captured||attempted)return false;
        attempted=true;selected=weapon;return weapon!=0;
    }
};
}
