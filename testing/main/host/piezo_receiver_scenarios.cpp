#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <atomic>
#include <cassert>
#include <iostream>
#define private public
#include "src/link/SensorLink.h"
#undef private
uint32_t clockMs=1000;
int main(){
 namespace L=ImuLabProtocol;namespace P=PiezoProtocol;
 EmergencyCoordinator e;SensorLink link(e);link.begin();link.seen=true;link.heartbeat.boot=9;link.lastSeen=clockMs;
 link.labSession=11;link.labBoot=9;link.labSeen=true;link.labSeenAt=clockMs;link.labLive.healthy=true;
 auto enabled=[&](int expected){JsonDocument imu;link.writeLabJson(imu.to<JsonObject>());const auto v=imu["detector"]["piezoImpactDetectionEnabled"];if(expected<0)assert(v.isNull());else assert(v.is<bool>()&&v.as<bool>()==bool(expected));assert(imu["detector"]["piezoCorroborationRequired"]==false);};
 enabled(-1);
 P::Status p;p.sequence=1;p.uptime=1000;p.epoch=1;p.flags=P::Enabled|P::Healthy;p.sampleAgeMs=1;
 p.bank[0].raw=1001;p.bank[0].baseline=1000;p.bank[0].rise=1;p.bank[0].flags=P::Ready;
 p.bank[0].hits=1;p.bank[0].lastPeak=800;p.bank[0].supportPeak=800;p.bank[0].supportAt=900;p.bank[0].supportHit=1;p.candidate=1;p.candidateAt=1000;p.bank[0].lastAt=900;p.matchedBanks=1;p.matchedHits[0]=1;
 L::Frame f;f.boot=9;f.session=11;P::encode(p,f);link.receiveLab(f);enabled(1);
 JsonDocument doc;link.writePiezoJson(doc.to<JsonObject>());assert(doc["fresh"]==true&&doc["front"]["raw"]==1001&&doc["front"]["imuMatched"]==true);
 link.piezoLive.bank[0].hits=2;link.piezoLive.bank[0].lastPeak=100;link.piezoLive.bank[0].lastAt=980;
 doc.clear();link.writePiezoJson(doc.to<JsonObject>());assert(doc["front"]["imuMatched"]==true&&doc["front"]["supportPeak"]==800);
 for(unsigned problem=0;problem<4;++problem){
  link.piezoLive=p;
  if(problem==0)link.piezoLive.flags&=uint8_t(~P::Enabled);
  if(problem==1)link.piezoLive.flags|=P::SettingsFault;
  if(problem==2)link.piezoLive.bank[0].flags|=P::Clipped;
  if(problem==3)link.piezoLive.bank[0].supportPeak=799;
  doc.clear();link.writePiezoJson(doc.to<JsonObject>());assert(!doc["front"]["imuMatched"].as<bool>());
 }
 link.piezoLive=p;
 assert(link.piezoCommandReady(L::Op::PiezoNormal)==L::Result::Accepted);
 clockMs=1500;link.receiveLab(f);assert(link.piezoAt==1000);
 clockMs=2001;doc.clear();link.writePiezoJson(doc.to<JsonObject>());assert(!doc["fresh"].as<bool>()&&doc["front"]["raw"].isNull()&&!doc["front"]["imuMatched"].as<bool>());enabled(-1);
 p.sequence=2;p.uptime=2001;p.bank[0].hits=2;p.bank[0].supportHit=2;p.bank[0].lastPeak=800;p.bank[0].lastAt=1990;P::encode(p,f);link.receiveLab(f);
 doc.clear();link.writePiezoJson(doc.to<JsonObject>());assert(doc["fresh"]==true&&!doc["front"]["imuMatched"].as<bool>());
 f.session=12;link.receiveLab(f);assert(link.piezoSequence==2);f.session=11;
 p.sequence=3;P::encode(p,f);L::put16(f.data+36,4096);link.receiveLab(f);assert(link.piezoSequence==2);
 p.sequence=4;p.flags=P::Healthy;P::encode(p,f);link.receiveLab(f);enabled(0);
 p.sequence=5;p.flags=P::Enabled|P::Healthy;P::encode(p,f);link.receiveLab(f);enabled(1);
 link.commandPending=true;assert(link.piezoCommandReady(L::Op::PiezoStop)==L::Result::Busy);link.commandPending=false;
 link.piezoLive.normal.state=P::Recording;assert(link.piezoCommandReady(L::Op::PiezoNormal)==L::Result::Busy);assert(link.piezoCommandReady(L::Op::PiezoStop)==L::Result::Accepted);
 link.piezoLive.normal.state=P::Empty;link.piezoLive.flags&=uint8_t(~P::Healthy);assert(link.piezoCommandReady(L::Op::PiezoThresholds)==L::Result::Offline);assert(link.piezoCommandReady(L::Op::PiezoStop)==L::Result::Accepted);enabled(1);
 CrashProtocol::Frame heartbeat;heartbeat.type=CrashProtocol::Type::Heartbeat;heartbeat.boot=10;heartbeat.uptimeMs=2000;uint8_t wire[32];CrashProtocol::encode(heartbeat,wire);
 for(auto b:wire)modem.rx.push_back(char(b));link.tick();assert(!link.piezoSeen&&link.piezoSequence==0);enabled(-1);
 L::Live live;live.healthy=true;L::encodeLive(live,f);f.boot=10;f.session=11;link.receiveLab(f);p.sequence=1;p.flags=P::Enabled|P::Healthy;p.bank[0].flags=P::Ready|P::Old;P::encode(p,f);link.receiveLab(f);
 doc.clear();link.writePiezoJson(doc.to<JsonObject>());assert(doc["boot"]==10&&doc["front"]["lastAgeMs"].isNull());
 f.boot=9;link.receiveLab(f);assert(link.piezoSequence==1);
 clockMs=UINT32_MAX-20;link.lastSeen=link.labSeenAt=clockMs;link.piezoAt=clockMs;
 clockMs=30;assert(link.piezoOnline());clockMs=1100;assert(!link.piezoOnline());
 std::cout<<"PASS actual Main piezo receiver: matched boot/session, strict status values, no duplicate freshness, stale null readings, current-hit correlation, command readiness, stop on input fault, reboot isolation, retained old-hit age and uptime rollover\n";
}
