#include "../CoD3Host/bot_team_assignment.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
int main() {
 using BotTeamAssignment::Choose;
 assert(Choose(1,0,2)==2); // solo allied host
 assert(Choose(0,1,1)==1); // solo axis host
 assert(Choose(1,1,2)==2); // humans on opposing teams
 assert(Choose(2,0,2)==2); // friends together
 for(unsigned humansA=0;humansA<=2;++humansA)
 for(unsigned humansB=0;humansB<=2;++humansB) {
  unsigned a=humansA,b=humansB;
  for(unsigned i=0;i<20;++i) {
   unsigned team=Choose(a,b,2);
   assert(team==1 || team==2);
   assert(a==b || team==(a<b?1u:2u));
   if(team==1)++a;else ++b;
   if(i>=1)assert(std::abs(int(a)-int(b))<=1);
  }
 }
 puts("PASS: solo/either-side host, split/together humans, 20 additions; smaller team and balance.");
}
