#include "../CoD3Host/bot_weapon_selection.h"
#include <cstdio>
int main(){using namespace BotWeaponSelection;
auto kit=[](uint32_t n){return (0x00801300u&(1u<<n))!=0;};
for(uint32_t old:{14u,17u,18u,29u,0u,128u,255u})if(Recover(old,8,12,kit)!=8)return 1;
if(Recover(12,8,12,kit)!=12||Recover(17,0,12,kit)!=12||Recover(17,0,0,kit)!=0)return 2;
if(Recover(17,8,12,[](uint32_t){return false;})!=0)return 3;
auto changed=[](uint32_t n){return n==19||n==15;};if(Recover(8,19,15,changed)!=19)return 4;
puts("PASS: observed stale requests recover owned main; valid cached gun retained; secondary fallback, invalid IDs, no-kit and changed-kit cases pass.");}
