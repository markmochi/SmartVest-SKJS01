#include <cassert>
#include <cmath>
#include <iostream>
#include <new>
#include "bmi323_fake.h"
#include "src/sensing/ImuLab.h"
#include "SensorLink.h"
namespace L=ImuLabProtocol;
Imu imu;
GyroCalibration gyro;
CalibrationStore bias;
FakeBMI323 fake;
EventLink events;
ImuLab lab(imu,gyro,bias,events);
EmergencyCoordinator coordinator;
SensorLink mainLink(coordinator);
L::Frame observedCommand;L::Parser mainTxParser;
void step(unsigned ms=1){
  for(unsigned j=0;j<ms;++j){++clockMs;events.tick(CrashProtocol::Frame());imu.tick();lab.tick();transfer(2,0);mainLink.tick();for(auto b:ports[0].tx){L::Frame f;CrashProtocol::Frame v1;if(mainTxParser.feed(b,clockMs,v1,f)==2 && f.type==L::Type::Command)observedCommand=f;}transfer(0,2);}
}
JsonDocument status(){JsonDocument d;mainLink.writeLabJson(d.to<JsonObject>());return d;}
void raw(int16_t ax,int16_t ay,int16_t az,int16_t gx=16,int16_t gy=-8,int16_t gz=3){
  fake.accel={ax,ay,az};fake.gyro={gx,gy,gz};
}
uint32_t command(L::Op op){
  const auto token=mainLink.requestLab(op);assert(token);step(160);
  auto d=status();assert(d["command"]["token"].as<uint32_t>()==token);assert(d["command"]["state"].as<int>()==2);return token;
}
void calibrate(L::Op op){command(op);step(2300);if(status()["calibration"]["result"].as<int>()!=2)std::cerr<<"Calibration op="<<int(op)<<" result="<<status()["calibration"]["result"].as<int>()<<" gyro="<<int(gyro.result)<<" n="<<gyro.count<<"\n";assert(status()["calibration"]["result"].as<int>()==2);}
void restartAcquisition(){
  lab.~ImuLab();imu=Imu();gyro=GyroCalibration();bias=CalibrationStore();
  new(&lab) ImuLab(imu,gyro,bias,events);imu.begin();lab.begin();
  assert(!lab.automaticReady());step(1200);
}
void persistenceTests(){
  const auto original=FakeNvs::blobs;
  raw(-356,0,2017);calibrate(L::Op::Neutral);step(200);
  const auto saved=FakeNvs::blobs;const auto writes=FakeNvs::writes;
  const auto revision=status()["calibration"]["revision"].as<uint32_t>();
  const float savedBias=bias.bias[0];
  raw(-1024,0,1774);restartAcquisition();
  assert(bias.valid&&lab.automaticReady());
  assert(status()["calibration"]["flags"].as<int>()==15);
  assert(status()["calibration"]["revision"].as<uint32_t>()==revision);
  assert(std::fabs(status()["pose"]["sideDeg"].as<float>()-20)<.4f);
  assert(bias.bias[0]==savedBias&&FakeNvs::blobs==saved&&FakeNvs::writes==writes);
  raw(-356,0,2017);restartAcquisition();
  assert(lab.automaticReady()&&std::fabs(status()["pose"]["sideDeg"].as<float>())<.2f);
  std::cout<<"PASS saved axes, nonzero neutral and gyro restore after acquisition reboot; tilted boot does not redefine neutral; zero NVS writes\n";

  fake.allHigh=true;step(200);assert(!imu.healthy()&&!lab.automaticReady()&&!bias.valid);
  fake.allHigh=false;step(6000);
  assert(imu.healthy()&&bias.valid&&lab.automaticReady());
  assert(FakeNvs::blobs==saved&&FakeNvs::writes==writes);
  assert(std::fabs(status()["pose"]["sideDeg"].as<float>())<.2f);
  command(L::Op::Gyro);fake.allHigh=true;step(200);fake.allHigh=false;step(6000);
  assert(bias.valid&&lab.automaticReady()&&FakeNvs::blobs==saved&&FakeNvs::writes==writes);
  std::cout<<"PASS sensor-loss and interrupted-capture recovery reuse saved calibration without manual capture or writes\n";

  for(int failure=0;failure<6;++failure){
    FakeNvs::blobs=saved;
    auto& b=FakeNvs::blobs["sv_bmi_gyro/bias"];
    if(failure==0)b.back()^=1;
    if(failure==1){CrashProtocol::put16(b.data()+6,0x1234);CrashProtocol::put16(b.data()+22,CrashProtocol::crc16(b.data(),22));}
    if(failure==2)FakeNvs::blobs["sv_bmi_mount/body"].back()^=1;
    if(failure==3){float other[3]={2,0,0};assert(bias.save(imu.whoAmI,other));}
    if(failure==4)fake.regs[0]=0x2143;
    if(failure==5)b.clear();
    const auto before=FakeNvs::writes;restartAcquisition();
    assert(!lab.automaticReady()&&FakeNvs::writes==before);
    if(failure!=2)assert(!bias.valid);
    fake.regs[0]=0x1143;
  }
  FakeNvs::blobs=saved;FakeNvs::failWrite=true;restartAcquisition();
  assert(lab.automaticReady()&&bias.valid);FakeNvs::failWrite=false;
  command(L::Op::ClearBody);restartAcquisition();assert(!lab.automaticReady());
  FakeNvs::blobs=original;raw(0,0,2048);restartAcquisition();assert(lab.automaticReady());
  std::cout<<"PASS corrupt/missing/profile/identity/mismatched calibration rejected; read-only restore works; explicit rider-axis reset remains effective\n";
}
int main(){
  // Match the full ID reported by the user's working physical v0.8.3 lab.
  fake.regs[0]=0x1143;fake.automatic=true;raw(0,0,2048);
  events.begin();mainLink.begin();imu.begin();lab.begin();events.attachLab(nullptr,[](void*,const L::Frame& f){lab.handle(f);});step(500);raw(0,0,2048);step(100);
  assert(imu.healthy() && imu.whoAmI==0x1143);assert(mainLink.labOnline());
  assert(status()["whoAmI"].as<int>()==0x1143);
  calibrate(L::Op::Upright);assert(bias.valid);assert(status()["calibration"]["flags"].as<int>()&L::UprightSet);
  raw(0,-1024,1774);calibrate(L::Op::Forward);assert(status()["calibration"]["flags"].as<int>()&L::BodyCalibrated);
  step(500);assert(std::fabs(status()["pose"]["forwardDeg"].as<float>()-30)<0.2f);
  raw(0,0,2048);calibrate(L::Op::Neutral);step(100);assert(status()["pose"]["valid"].as<bool>());
  assert(std::fabs(status()["pose"]["sideDeg"].as<float>())<0.1f);
  raw(-1024,0,1774);step(5000);assert(std::fabs(status()["pose"]["sideDeg"].as<float>()-30)<0.3f);
  raw(0,0,2048);step(2000);
  std::cout<<"PASS lab: BMI323 SPI, fresh stationary captures, rider-axis and tilt signs, gyro/neutral persistence\n";
  persistenceTests();
  step(3000);const auto candidates=lab.detector.count;
  for(int j=1;j<=80;j++){float rad=j*.9f/57.2957795f;raw(int16_t(-std::sin(rad)*2048),0,int16_t(std::cos(rad)*2048),16,2941,3);step(5);}
  raw(-1948,0,633);step(1400);assert(lab.detector.count==candidates+1);assert(events.pending()==0&&coordinator.unique==0);step(150);
  assert(status()["detector"]["lastCandidate"]["rotationHoldMs"].as<float>()>=100);assert(status()["detector"]["lastCandidate"]["tiltHoldMs"].as<float>()>=800);
  command(L::Op::ResetCandidate);raw(0,0,2048);step(5000);
  std::cout<<"PASS actual acquisition/attitude/detector -> V2 candidate JSON; no incident/ACK/SMS job transition; explicit diagnostic reset\n";
  assert(!mainLink.requestLab(L::Op::Start));
  assert(!mainLink.requestLab(L::Op::Stop));
  assert(!mainLink.requestLab(L::Op::ReadSamples));
  assert(status()["trial"].isNull());
  std::cout<<"PASS removed recorder commands are rejected; calibration and live acquisition remain available\n";
  // Mixed parser must honor boundaries, CRC and gap expiry; payload can contain SV magic.
  L::Frame f;f.boot=1;f.session=1;f.length=100;f.data[4]='S';f.data[5]='V';uint8_t bytes[L::MAX_FRAME];auto size=L::encode(f,bytes);
  L::Parser parser;L::Frame out;CrashProtocol::Frame incident;unsigned kinds=0;
  for(size_t i=0;i<size;++i)kinds+=parser.feed(bytes[i],clockMs,incident,out);
  assert(kinds==2 && out.length==100);bytes[size-1]^=1;kinds=0;
  for(size_t i=0;i<size;++i)kinds+=parser.feed(bytes[i],clockMs,incident,out);
  assert(!kinds);
  std::cout<<"PASS lab: mixed-frame boundaries, embedded incident magic, corrupted-frame rejection\n";
  FakeBMI323 invalid;invalid.regs[0]=0x98;Imu unknown;unknown.begin();assert(!unknown.healthy()&&unknown.error==Imu::Error::Identity);
  FakeBMI323 config;config.ignoreAccWrite=true;Imu bad;bad.begin();assert(!bad.healthy()&&bad.error==Imu::Error::Configuration&&bad.faultRegister==0x20);
  std::cout<<"PASS invalid BMI identity and configuration fail closed\n";
}
