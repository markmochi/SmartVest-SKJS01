#include <cassert>
#include <iostream>
#include "bmi323_fake.h"
#include "esp_adc/adc_oneshot.h"
#include "SmartVest_SJKS01_Sensing.ino"
#include "SensorLink.h"
namespace L=ImuLabProtocol;
EmergencyCoordinator coordinator;
SensorLink mainLink(coordinator);
bool returnConnected=true,uplinkConnected=true;
void step(unsigned n){for(unsigned i=0;i<n;++i){
  if(i%2==0)acquisitionStep();loop();
  if(uplinkConnected)transfer(2,0);else ports[2].tx.clear();
  mainLink.tick();
  if(returnConnected)transfer(0,2);else ports[0].tx.clear();
}}
JsonDocument detectorStatus(){JsonDocument d;mainLink.writeLabJson(d.to<JsonObject>());return d;}
void raw(FakeBMI323& f,int ax,int ay,int az,int gx=16,int gy=-8,int gz=3){f.accel={int16_t(ax),int16_t(ay),int16_t(az)};f.gyro={int16_t(gx),int16_t(gy),int16_t(gz)};}
void calibration(L::Op op){assert(mainLink.requestLab(op));step(2600);assert(detectorStatus()["calibration"]["result"].as<int>()==2);}
void fall(FakeBMI323& f){
  for(int j=1;j<=80;++j){const float a=j*.9f/57.2957795f;raw(f,int(-std::sin(a)*2048),0,int(std::cos(a)*2048),16,2941,3);step(5);}
  raw(f,-1948,0,633);step(1500);
}
void recover(FakeBMI323& f){raw(f,0,0,2048);step(6000);}
int main(){
  FakeBMI323 fake;fake.regs[0]=0x1143;fake.automatic=true;raw(fake,0,0,2048);
  FakeAdc::raw[6]=123;FakeAdc::raw[7]=456;
  setup();initializeAcquisition();mainLink.begin();step(1200);
  assert(imu.healthy() && mainLink.labOnline() && !mainLink.armedAndHealthy());
  assert(eventLink.automaticPeerCurrent() && automaticQueued==0);
  calibration(L::Op::Upright);raw(fake,0,-1024,1774);calibration(L::Op::Forward);
  assert(!mainLink.armedAndHealthy());raw(fake,0,0,2048);calibration(L::Op::Neutral);step(1100);
  assert(mainLink.armedAndHealthy() && detectorStatus()["detector"]["automaticReady"].as<bool>());
  JsonDocument impactStatus;mainLink.writePiezoJson(impactStatus.to<JsonObject>());assert(impactStatus["fresh"].as<bool>()&&impactStatus["front"]["ready"].as<bool>());
  FakeAdc::raw[6]=3123;step(3);FakeAdc::raw[6]=123;step(400);
  assert(piezo.impact.data.bank[0].hits==1&&coordinator.unique==0&&eventLink.pending()==0);
  assert(mainLink.requestLab(L::Op::PiezoThresholds,600,700,1));step(700);
  impactStatus.clear();mainLink.writePiezoJson(impactStatus.to<JsonObject>());assert(impactStatus["revision"]==2&&impactStatus["front"]["threshold"]==600);
  step(5000);
  fall(fake);assert(imuLab.detector.count==1 && automaticQueued==1);
  assert(piezo.impact.data.matchedBanks==0); // No fresh piezo hit: IMU remains independent.
  assert(coordinator.unique==1 && !eventLink.pending());
  assert(coordinator.lastFrame.type==CrashProtocol::Type::AutomaticCrash);
  assert(coordinator.lastFrame.uptimeMs==workerDecisionAt); // Correlation and incident identity use one decision timestamp.
  assert((coordinator.lastFrame.flags&7)==7 && coordinator.lastAgeKnown);
  assert(eventLink.lastAckSequence==automaticLastSequence && eventLink.lastAckDecisionAgeMs<100);
  step(2500);assert(automaticQueued==1 && coordinator.unique==1);
  std::cout<<"PASS raw SPI -> calibrated detector -> acquisition queue -> durable automatic crash UART -> Main acceptance -> sensing ACK; one incident per latched candidate\n";

  recover(fake);FakeAdc::raw[6]=923;step(3);FakeAdc::raw[6]=123;step(400);returnConnected=false;fall(fake);
  assert(automaticQueued==2 && coordinator.unique==2 && eventLink.pending()==1);
  assert(piezo.impact.data.matchedBanks==1); // Only the front bank hit; rear is unnecessary.
  step(1200);assert(coordinator.unique==2 && eventLink.pending()==1);
  returnConnected=true;step(1600);assert(!eventLink.pending() && coordinator.unique==2);
  std::cout<<"PASS lost ACK and lease recovery: durable retry, no duplicate Main incident\n";

  // A legacy Main's token-zero Hello must disarm automatic events even though
  // its diagnostic lease and ordinary heartbeat stream can remain healthy.
  L::Frame hello;hello.type=L::Type::Hello;hello.boot=eventLink.bootId();hello.session=99;
  uint8_t b[L::MAX_FRAME];const auto size=L::encode(hello,b);ports[2].rx.clear();
  for(size_t i=0;i<size;++i)ports[2].rx.push_back(b[i]);eventLink.tick(telemetry());
  assert(eventLink.labActive() && !eventLink.automaticPeerCurrent());
  acquisitionStep();assert(!(telemetry().flags&CrashProtocol::Armed));
  step(1200);assert(eventLink.automaticPeerCurrent());recover(fake);

  // Break the uplink while retaining Main->Sensing Hello, fill the durable
  // outbox, then verify the worker snapshot waits rather than overwriting it.
  uplinkConnected=false;CrashProtocol::Frame manual;manual.type=CrashProtocol::Type::Crash;manual.flags=7;manual.uptimeMs=millis();
  for(unsigned i=0;i<SensorConfig::MAX_PENDING_EVENTS;++i)assert(eventLink.enqueue(manual));
  const auto before=automaticQueued;fall(fake);
  assert(imuLab.detector.count==3 && automaticQueued==before);
  assert(automaticQueue->items.size()==1 && !automaticQueueDropped.load());
  assert(!eventLink.automaticEventReady()&&!(telemetry().flags&CrashProtocol::Armed));
  uplinkConnected=true;step(6500);
  assert(automaticQueued==before+1 && automaticQueue->items.empty() && !eventLink.pending());
  assert(eventLink.automaticEventReady());
  std::cout<<"PASS full durable outbox retains candidate until space becomes available; eventual delivery and explicit queue diagnostics\n";

  // Invalid gyro readiness cannot create a later surprise alert.
  calibrationStore.valid=false;const auto queued=automaticQueued;const auto skips=automaticSkipped.load();
  ++imuLab.detector.count;acquisitionStep();loop();
  assert(automaticQueued==queued && automaticSkipped.load()==skips+1);
  assert(!(telemetry().flags&CrashProtocol::Armed));
  std::cout<<"PASS missing Neutral, legacy Main capability, and invalid gyro prevent automatic arming\n";
}
