#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/EvidencePolicy.h"
#include <cassert>
int main() {
  using namespace EvidencePolicy;
  auto w=window(800000,100000,true,300000,300000,2000);
  assert(w.trigger==700000 && w.start==398000 && w.end==1002000);
  assert(overlaps(360000,420000,w));
  assert(!overlaps(0,359999,w));
  assert(canProtect(w,360100,1019999,true,true));
  assert(!canProtect(w,400000,1019999,true,true));
  assert(!canProtect(w,360100,1001999,true,true));
  assert(!canProtect(w,360100,1019999,false,true));
  assert(!canProtect(w,360100,1019999,true,false));
  auto slow=window(800000,100000,true,300000,300000,5000);
  assert(slow.start==395000 && slow.end==1005000);
  assert(slow.start<=700000-5000-300000); // covers earliest possible trigger
  auto warm=window(100000,0,true,300000,300000,2000);
  assert(!warm.prePossible && !canProtect(warm,0,500000,true,true));
  auto reboot=window(10000,20000,true,300000,300000,2000);
  assert(!reboot.known && !canProtect(reboot,0,500000,true,true));
  auto unknown=window(800000,0,false,300000,300000,2000);
  assert(!unknown.known);
  // 64-bit camera clock remains correct beyond the 32-bit millis wrap.
  auto wrap=window(4294967296ULL+800000,100000,true,300000,300000,2000);
  assert(wrap.trigger==4294967296ULL+700000);
}
