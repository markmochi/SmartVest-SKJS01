#include "src/power/BatteryService.h"
#include "Config.h"
#include <cassert>
#include <cmath>
#include <iostream>
uint32_t clockMs=100;
struct Rig {
  BatteryProtocol::Tracker source;
  BatteryService power;
  BatteryProtocol::Reading r;
  Rig(){power.attachBattery(&source);power.setRemoteLink(true);r.flags=3;r.age=0;r.profile=1;r.currentMA=500;r.powerMW=6000;}
  void push(uint32_t sourceMV,uint8_t legacyPercent=43,int32_t shuntUV=0){
    clockMs+=1000;++r.sequence;r.uptime+=1000;r.busMV=sourceMV-shuntUV/1000;r.shuntUV=shuntUV;r.percent=legacyPercent;
    assert(source.accept(r,clockMs));power.updateIndicator();
  }
};
void check(Rig& rig,int percent){
  JsonDocument admin,rider;rig.power.writeJson(admin.to<JsonObject>());rig.power.writeRiderBattery(rider.to<JsonObject>());
  assert(rig.power.batteryPercent()==percent&&admin["batteryPercent"]==percent&&rider["percent"]==percent);
  assert(admin["sampleValid"]==true&&rider["current"]==true&&admin["batteryEstimated"]==true);
  assert(admin["batteryCalibrated"]==false&&admin["estimateMethod"]=="Main voltage estimate");
  assert(rider["low"]==(percent<=30));
}
int main(){
  static_assert(sizeof(BatteryDisplay)<=96,"Bounded Main percentage filter");
  Rig rig;JsonDocument json,rider;
  rig.power.writeJson(json.to<JsonObject>());rig.power.writeRiderBattery(rider.to<JsonObject>());
  assert(json["enabled"]==true&&json["sensorModel"]=="INA226"&&json["sdaPin"].isNull());
  assert(json["batteryPercent"].isNull()&&json["sourceV"].isNull()&&rider["percent"].isNull());
  rig.push(11650,40,50000);check(rig,100);
  rig.power.writeJson(json.to<JsonObject>());
  assert(std::fabs(json["sourceV"].as<float>()-11.65f)<.001f&&std::fabs(json["busV"].as<float>()-11.6f)<.001f);
  assert(json["currentMA"]==500&&json["powerMW"]==6000&&json["viaSensing"]==true);
  assert(rig.power.indicatorState()==BatteryIndicator::State::Normal);
  for(int p=0;p<=100;++p){rig.push(p%2?11650:11800,uint8_t(p));check(rig,100);}
  std::cout<<"PASS installed Nano percentages 0..100 ignored: owner full-charge range stays 100 on Main, both APIs and LED; raw voltage/current/power preserved\n";

  // A real low-voltage sample is counted once, regardless of request/loop rate.
  rig.push(10600,100);assert(rig.power.batteryPercent()>0);
  for(unsigned i=0;i<200;++i){clockMs+=10;rig.power.updateIndicator();rig.power.writeJson(json.to<JsonObject>());}
  assert(rig.power.batteryPercent()>0&&json["lowBatteryVoltage"]==true);
  assert(!rig.source.accept(rig.r,clockMs));rig.power.updateIndicator();assert(rig.power.batteryPercent()>0);
  rig.push(10600,100);assert(rig.power.batteryPercent()>0);
  rig.push(10600,100);check(rig,0);assert(rig.power.indicatorState()==BatteryIndicator::State::Critical);
  for(unsigned i=0;i<8;++i)rig.push(10810,0);
  assert(rig.power.batteryPercent()>=10&&rig.power.batteryPercent()<=20);
  std::cout<<"PASS fresh sample counting: duplicate packets and 200 fast Main/query cycles do not satisfy low-voltage debounce; three real empty samples force zero; recovery remains bounded\n";

  // Fresh raw measurements also work when an older sender has no percentage.
  for(int version=1;version<=3;++version){
    Rig compatible;compatible.r.sequence=1;compatible.r.uptime=1000;compatible.r.busMV=11625;compatible.r.shuntUV=25000;compatible.r.percent=45;
    uint8_t packet[BatteryProtocol::SIZE];BatteryProtocol::encode(compatible.r,packet);packet[2]=uint8_t(version);
    const auto crc=BatteryProtocol::crc16(packet,BatteryProtocol::SIZE-2);packet[38]=uint8_t(crc);packet[39]=uint8_t(crc>>8);
    BatteryProtocol::Reading decoded;assert(BatteryProtocol::decode(packet,decoded));
    assert(compatible.source.accept(decoded,clockMs));compatible.power.updateIndicator();check(compatible,100);
  }
  Rig unknown;unknown.r.profile=0;unknown.push(11650,255);check(unknown,100);
  std::cout<<"PASS unchanged version 1/2/3 Nano packets and unknown legacy percentage/profile: Main estimates from validated raw voltage, no Nano update required\n";

  for(int p=0;p<=100;p+=10){
    Rig boundary;boundary.push(10600+uint32_t(p)*105/10,uint8_t(100-p));check(boundary,p);
    const auto expected=p<=10?BatteryIndicator::State::Critical:p<=30?BatteryIndicator::State::Low:BatteryIndicator::State::Normal;
    assert(boundary.power.indicatorState()==expected&&boundary.power.ledOutputState()==expected);
  }
  std::cout<<"PASS Main-estimated percentage/LED boundaries and matching rider low status; independent of the Nano percentage\n";

  Rig charge;charge.r.charge=BatteryProtocol::Charging;charge.r.netCurrentMA=500;charge.push(11125);
  charge.power.writeJson(json.to<JsonObject>());charge.power.writeRiderBattery(rider.to<JsonObject>());
  assert(json["charging"]==true&&rider["charging"]==true&&json["netCurrentMA"]==500);
  charge.r.charge=BatteryProtocol::Discharging;charge.r.netCurrentMA=-500;charge.r.currentMA=-500;charge.push(11125,0,-25000);
  charge.power.writeJson(json.to<JsonObject>());assert(json["charging"]==false&&json["netCurrentMA"]==-500);
  for(auto state:{BatteryProtocol::Idle,BatteryProtocol::VoltageCharging,BatteryProtocol::VoltageNotCharging,BatteryProtocol::ChargeUnknown}){
    charge.r.charge=state;charge.r.netCurrentMA=0;charge.push(11125);charge.power.writeJson(json.to<JsonObject>());charge.power.writeRiderBattery(rider.to<JsonObject>());
    if(state==BatteryProtocol::Idle)assert(json["charging"]==false&&rider["charging"]==false);
    else assert(json["charging"].isNull()&&rider["charging"].isNull()&&json["netCurrentMA"].isNull());
    assert(json["chargingEstimated"]==false&&rider["chargingEstimated"]==false);
  }
  std::cout<<"PASS measured charging compatibility and signed shunt correction; no charging inference from voltage or load current\n";

  Rig retained;retained.r.age=1200;retained.push(10810,90);check(retained,20);
  retained.power.setRemoteLink(false);clockMs+=30000;retained.power.updateIndicator();
  retained.power.writeRiderBattery(rider.to<JsonObject>());assert(rider["percent"]==20&&rider["ageMs"]==31200&&rider["current"]==false);
  assert(retained.power.indicatorState()==BatteryIndicator::State::Unknown);
  JsonDocument reloaded;retained.power.writeRiderBattery(reloaded.to<JsonObject>());assert(reloaded["percent"]==20);
  clockMs=0xfffffff0;retained.power.updateIndicator();retained.power.writeRiderBattery(rider.to<JsonObject>());
  const auto age=rider["ageMs"].as<uint64_t>();clockMs=16;retained.power.updateIndicator();retained.power.writeRiderBattery(rider.to<JsonObject>());
  assert(rider["ageMs"].as<uint64_t>()==age+32&&rider["percent"]==20);
  retained.power.setRemoteLink(true);retained.r.age=0;retained.push(10810,100);check(retained,20);
  clockMs+=BatteryProtocol::STALE_MS;retained.power.updateIndicator();retained.power.writeJson(json.to<JsonObject>());
  assert(retained.power.batteryPercent()<0&&json["sourceV"].isNull()&&retained.power.indicatorState()==BatteryIndicator::State::Unknown);
  std::cout<<"PASS source age, link loss, six-second expiry, retained rider snapshot, page reload, clock wrap and fresh recovery; no stale estimate presented as current\n";

  Rig manual;manual.push(10810,100);check(manual,20);
  assert(manual.power.startLedTest("blue")&&manual.power.ledTestBlue());
  assert(manual.power.indicatorState()==BatteryIndicator::State::Low);
  manual.power.setRemoteLink(false);manual.power.updateIndicator();assert(manual.power.ledTestActive()&&manual.power.indicatorState()==BatteryIndicator::State::Unknown);
  clockMs+=15000;manual.power.updateIndicator();assert(!manual.power.ledTestActive()&&manual.power.ledOutputState()==BatteryIndicator::State::Unknown);
  manual.power.setRemoteLink(true);manual.push(10810);assert(manual.power.startLedTest("full")&&manual.power.ledOutputState()==BatteryIndicator::State::Full);
  assert(manual.power.startLedTest("stop")&&manual.power.ledOutputState()==BatteryIndicator::State::Low);
  std::cout<<"PASS manual LED override isolation, loss, expiry and stop; normal LED uses Main's estimate\n";

  Rig faulty;faulty.r.flags=0;faulty.r.age=UINT32_MAX;faulty.r.error=2;faulty.push(11200,255);
  faulty.power.writeJson(json.to<JsonObject>());assert(json["nanoConnected"]==true&&json["responding"]==false&&json["sampleValid"]==false&&json["nanoI2cError"]==2);
  assert(json["sourceV"].isNull()&&json["batteryPercent"].isNull());
  clockMs+=BatteryProtocol::STALE_MS;faulty.power.writeJson(json.to<JsonObject>());assert(json["nanoConnected"]==false);
  faulty.r.flags=3;faulty.r.age=0;faulty.r.error=0;faulty.push(11200,0);
  faulty.power.writeJson(json.to<JsonObject>());assert(json["nanoConnected"]==true&&json["sampleValid"]==true);
  assert(std::fabs(json["busV"].as<double>()-11.2)<.001);
  BatteryService restarted;restarted.writeRiderBattery(reloaded.to<JsonObject>());assert(reloaded["percent"].isNull());
  std::cout<<"PASS Nano transport vs measurement failure, fresh raw-voltage recovery and no invented value after Main restart\n";
}
