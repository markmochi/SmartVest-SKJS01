#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/LinkPolicy.h"
#include <cassert>
#include <initializer_list>
int main(){
  using namespace CameraLink;
  Retry p;p.start(0);
  assert(p.tick(0,false)==Action::Begin);
  for(uint32_t t=50;t<30000;t+=50)assert(p.tick(t,false)==Action::None);
  assert(p.tick(30000,false)==Action::Cancel);
  assert(p.tick(31999,false)==Action::None);
  p.failed(31000); // the cancel's delayed callback must not shift the retry
  assert(p.tick(32000,false)==Action::Begin);
  p.failed(32500);
  assert(p.tick(36499,false)==Action::None);
  assert(p.tick(36500,false)==Action::Begin); // exponential 4s
  assert(p.tick(37000,true)==Action::None);
  assert(p.online());
  for(uint32_t t=38000;t<200000;t+=1000)assert(p.tick(t,true)==Action::None);
  // An HTTP failure has no input to this radio state machine.
  assert(p.tick(200000,true)==Action::None);
  assert(p.tick(201000,false)==Action::None);
  assert(p.tick(202999,false)==Action::None);
  assert(p.tick(203000,false)==Action::Begin); // reset to 2s after success
  Retry driver;driver.start(0);driver.tick(0,true);driver.awaitDriverRetry(100);
  assert(driver.tick(29999,false)==Action::None);assert(driver.tick(30099,false)==Action::None);
  assert(driver.tick(30100,false)==Action::Cancel);
  Retry phase;phase.start(100,350);
  assert(phase.tick(449,false)==Action::None);
  assert(phase.tick(450,false)==Action::Begin);
  Retry jitter;jitter.start(0);jitter.tick(0,false);jitter.failed(100,500);
  assert(jitter.tick(2599,false)==Action::None);assert(jitter.tick(2600,false)==Action::Begin);
  Retry wrapped;wrapped.start(0xfffffff0U,350);
  assert(wrapped.tick(333,false)==Action::None);assert(wrapped.tick(334,false)==Action::Begin);
  Retry cap;cap.start(0);uint32_t t=0;
  for(uint32_t gap:{2000U,4000U,8000U,16000U,30000U,30000U}){
    assert(cap.tick(t,false)==Action::Begin);cap.failed(t+1);
    assert(cap.tick(t+gap,false)==Action::None);
    t+=gap+1;
  }
}
