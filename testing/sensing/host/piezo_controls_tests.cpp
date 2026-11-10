#include "src/sensing/PiezoInputs.h"
#include "src/link/EventLink.h"
#include <cassert>
#include <iostream>
namespace L=ImuLabProtocol;
void settle(PiezoInputs& p){for(unsigned i=0;i<2200;++i){++clockMs;p.tick();}}
void hello(EventLink& e,uint32_t session=123){L::Frame f;f.type=L::Type::Hello;f.boot=e.bootId();f.session=session;f.token=L::AUTOMATIC_CRASH_HELLO_TOKEN;uint8_t b[L::MAX_FRAME];auto n=L::encode(f,b);for(size_t i=0;i<n;++i)ports[2].rx.push_back(b[i]);e.tick(CrashProtocol::Frame());}
L::Frame command(EventLink& e,L::Op op,uint32_t token,uint32_t revision,uint16_t front=0,uint16_t rear=0){L::Frame f;f.type=L::Type::Command;f.boot=e.bootId();f.session=123;f.token=token;f.length=40;f.data[0]=uint8_t(op);L::put16(f.data+2,front);L::put16(f.data+4,rear);L::put32(f.data+8,revision);return f;}
void seed(uint8_t version,uint16_t front,uint16_t rear,uint32_t revision){
 std::vector<uint8_t> b(16);b[0]='P';b[1]='Z';b[2]=version;
 CrashProtocol::put16(b.data()+4,front);CrashProtocol::put16(b.data()+6,rear);CrashProtocol::put32(b.data()+8,revision);
 CrashProtocol::put16(b.data()+14,CrashProtocol::crc16(b.data(),14));FakeNvs::blobs["sv_piezo/thresholds"]=b;
}
int main(){
 FakeNvs::reset();FakeAdc::reset();clockMs=0;FakeAdc::raw[6]=0;FakeAdc::raw[7]=0;
 EventLink e;assert(e.begin());
 {PiezoInputs p;p.begin();settle(p);hello(e);assert(p.impact.ready());
  auto f=command(e,L::Op::PiezoThresholds,1,1,650,750);p.handleCommand(f,e,false);assert(p.impact.data.revision==2&&p.impact.data.threshold[0]==650);
  const auto writes=FakeNvs::writes;p.handleCommand(f,e,false);assert(FakeNvs::writes==writes);
  f.data[2]^=1;p.handleCommand(f,e,false);assert(FakeNvs::writes==writes);f.data[2]^=1;
  f=command(e,L::Op::PiezoThresholds,2,1,700,800);p.handleCommand(f,e,false);assert(FakeNvs::writes==writes);
  f=command(e,L::Op::PiezoThresholds,3,2,0,800);p.handleCommand(f,e,false);assert(FakeNvs::writes==writes);
  f=command(e,L::Op::PiezoNormal,4,2);p.handleCommand(f,e,false);assert(p.impact.capturing());
  f=command(e,L::Op::PiezoThresholds,5,2,700,800);p.handleCommand(f,e,false);assert(p.impact.data.revision==2);
  f=command(e,L::Op::PiezoStop,6,2);p.handleCommand(f,e,false);assert(!p.impact.capturing());
  FakeNvs::failWrite=true;f=command(e,L::Op::PiezoThresholds,7,2,700,800);p.handleCommand(f,e,false);assert(p.impact.data.revision==2);FakeNvs::failWrite=false;
  f=command(e,L::Op::PiezoThresholds,8,2,700,800);p.handleCommand(f,e,false);assert(p.impact.data.revision==3);
  assert(e.pending()==0);
 }
 {PiezoInputs p;p.begin();assert(p.impact.data.threshold[0]==700&&p.impact.data.threshold[1]==800&&p.impact.data.revision==3);assert(!p.impact.ready());settle(p);assert(p.impact.ready());}
 auto& bytes=FakeNvs::blobs["sv_piezo/thresholds"];bytes[0]^=1;const auto corrupt=bytes;
 {PiezoInputs p;p.begin();assert(FakeNvs::blobs["sv_piezo/thresholds"]==corrupt&&p.impact.data.revision==1);settle(p);hello(e);p.report(e,0,0,false);assert(p.impact.data.flags&PiezoProtocol::SettingsFault);}
 FakeNvs::reset();seed(1,50,500,10);
 {PiezoInputs p;p.begin();assert(p.impact.data.threshold[0]==100&&p.impact.data.threshold[1]==100&&p.impact.data.revision==11);assert(FakeNvs::blobs["sv_piezo/thresholds"][2]==3);}
 const auto migratedWrites=FakeNvs::writes;
 {PiezoInputs p;p.begin();assert(p.impact.data.revision==11&&p.impact.data.threshold[0]==100&&FakeNvs::writes==migratedWrites);}
 seed(2,4000,4094,12);
 {PiezoInputs p;p.begin();assert(p.impact.data.threshold[0]==100&&p.impact.data.threshold[1]==100&&p.impact.data.revision==13);settle(p);hello(e);
  auto f=command(e,L::Op::PiezoThresholds,100,13,200,300);p.handleCommand(f,e,false);assert(p.impact.data.revision==14&&p.impact.data.threshold[0]==200);
  f=command(e,L::Op::PiezoThresholds,101,14,801,100);p.handleCommand(f,e,false);assert(p.impact.data.revision==14);
 }
 seed(1,50,50,20);const auto legacy=FakeNvs::blobs["sv_piezo/thresholds"];FakeNvs::failWrite=true;
 {PiezoInputs p;p.begin();assert(FakeNvs::blobs["sv_piezo/thresholds"]==legacy&&p.impact.data.revision==20);settle(p);hello(e);p.report(e,0,0,false);assert(p.impact.data.flags&PiezoProtocol::SettingsFault);
  FakeNvs::failWrite=false;auto f=command(e,L::Op::PiezoThresholds,102,20,100,100);p.handleCommand(f,e,false);assert(p.impact.data.revision==21);p.report(e,0,0,false);
 }
 seed(1,50,50,UINT32_MAX);const auto exhausted=FakeNvs::blobs["sv_piezo/thresholds"];
 {PiezoInputs p;p.begin();assert(FakeNvs::blobs["sv_piezo/thresholds"]==exhausted&&p.impact.data.revision==UINT32_MAX);}
 seed(3,100,100,22);
 {PiezoInputs p;p.begin();settle(p);hello(e);FakeNvs::corruptReadback=true;
  auto f=command(e,L::Op::PiezoThresholds,104,22,200,300);p.handleCommand(f,e,false);assert(p.impact.data.revision==22&&p.impact.data.threshold[0]==100);
  p.report(e,0,0,false);assert(p.impact.data.flags&PiezoProtocol::SettingsFault);FakeNvs::corruptReadback=false;
  f=command(e,L::Op::PiezoThresholds,105,22,100,100);p.handleCommand(f,e,false);assert(p.impact.data.revision==23);
 }
 seed(3,100,100,30);FakeAdc::raw[6]=3995;
 {PiezoInputs p;p.begin();settle(p);hello(e);assert(!p.impact.ready());auto f=command(e,L::Op::PiezoThresholds,103,30,100,100);p.handleCommand(f,e,false);assert(p.impact.data.revision==30);}
 FakeAdc::raw[6]=0;
 std::cout<<"PASS support settings: one-time verified legacy migration to 100/100, no repeat rewrite, V3 explicit overrides survive reboot, 801 rejection, failed upgrade retention/recovery, revision exhaustion and headroom rejection\n";
 std::cout<<"PASS actual piezo commands: verified isolated NVS, reboot thresholds, volatile baseline, duplicates, malformed/stale writes, active-trial gate, failed-write recovery, corrupt-record retention and no incident enqueue\n";
}
