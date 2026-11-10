#include <cassert>
#include "battery/not_charging_capture.h"
#include <cmath>
#include <iostream>
#include <random>
#include "src/link/EventLink.h"
#include "src/power/NanoBattery.h"
#include "SensorLink.h"
// Capture Arduino Print output as it actually appears on the shared USB/D1 TX.
struct NanoConsoleSerial:UsbSerial {
  using UsbSerial::write;
  size_t write(uint8_t b){output.push_back(char(b));return 1;}
  void print(const char* s){output+=s;} void print(char c){output+=c;}
  template<class T> void print(T v){output+=std::to_string(v);}
  void print(uint8_t v,int){const char* h="0123456789ABCDEF";if(v>=16)output+=h[v>>4];output+=h[v&15];}
  void println(){output+="\r\n";} template<class T> void println(T v){print(v);println();}
};
NanoConsoleSerial NanoSerial;
#define F(x) x
constexpr int HEX=16;
constexpr int LED_BUILTIN=13,OUTPUT=1;
void pinMode(int,int){} void digitalWrite(int,bool){}
#define Serial NanoSerial
#define setup nanoSetup
#define loop nanoLoop
#include "../../../Nano/SmartVest_INA226_Nano/SmartVest_INA226_Nano.ino"
#undef Serial
#undef setup
#undef loop
void nanoToSense(){for(uint8_t b:NanoSerial.output)ports[1].rx.push_back(b);NanoSerial.output.clear();}
void drainNano(NanoBattery& n){while(!ports[1].rx.empty())n.tick();}
int main(){
  using namespace BatteryProtocol;
  const uint32_t curveMV[]={10600,10705,10810,11125,11440,11650};
  const uint8_t curvePercent[]={0,10,20,50,80,100};
  for(unsigned i=0;i<6;++i)assert(estimate3S(curveMV[i])==curvePercent[i]);
  assert(estimate3S(0)==0&&estimate3S(9000)==0&&estimate3S(UINT32_MAX)==100);
  uint8_t previous=0;
  for(uint32_t mv=8000;mv<=15000;++mv){
    const auto percent=estimate3S(mv);assert(percent<=100&&percent>=previous);previous=percent;
  }
  assert(estimate3S(10000)==0&&estimate3S(10700)==9&&estimate3S(11300)==66);
  // Only the presented percentage is filtered; physical readings stay raw.
  BatteryDisplay charging;assert(charging.update(11125,0)==50);
  for(uint32_t t=1000;t<=60000;t+=1000){assert(charging.update(11650,t)==50);}
  assert(charging.smoothedPercent()<=50);
  uint8_t previousShown=50;
  for(uint32_t t=61000;t<=3600000;t+=1000){
    const auto shown=charging.update(11650,t);
    assert(shown%10==0&&shown>=previousShown&&shown<=previousShown+10);
    if(t<=300000)assert(shown==50);
    previousShown=shown;
  }
  assert(previousShown==100);
  BatteryDisplay spike;assert(spike.update(11545,0)==90);
  assert(spike.update(11650,1000)==90&&!spike.riseHeld());
  assert(spike.update(11545,2000)==90);
  assert(spike.update(9600,3000)==90); // Isolated low spike cannot declare empty.
  assert(spike.update(11545,4000)==90);
  BatteryDisplay low;assert(low.update(11545,0)==90);
  for(uint32_t t=1000;t<=3000;t+=1000)low.update(10810,t);
  assert(low.update(10810,4000)==20);
  low.update(10705,5000);low.update(10705,6000);assert(low.update(10705,7000)==10);
  low.update(9600,8000);low.update(9600,9000);assert(low.update(9600,10000)==0);
  BatteryDisplay outage;assert(outage.update(11125,0)==50);outage.invalidate();
  assert(outage.update(11650,3600000)==50&&outage.riseHeld());
  BatteryDisplay wrapDisplay;assert(wrapDisplay.update(11125,0xffffff00u)==50);
  assert(wrapDisplay.update(11650,uint32_t(0xffffff00u+1000u))==50&&!wrapDisplay.riseHeld());
  for(uint32_t t=2000;t<120000;t+=1000)assert(wrapDisplay.update(11650,uint32_t(0xffffff00u+t))==50);
  BatteryDisplay boundaries;assert(boundaries.update(11230,0)==60);
  for(uint32_t t=1000;t<=300000;t+=1000)assert(boundaries.update(t%2000?11245:11260,t)==60);
  nanoSetup();assert(configured);assert(Wire.regs[0]==0x4527);
  Wire.regs[2]=9280;Wire.regs[1]=20000;measureBattery();
  assert(reading.busMV==11600&&reading.shuntUV==50000&&reading.percent==100);
  assert((reading.flags&Valid)&&reading.currentMA==2500);
  batteryDisplay=BatteryDisplay();Wire.regs[2]=9600;Wire.regs[1]=10000;
  clockMs=1000;nanoLoop();assert(NanoSerial.output.size()<256);
  assert(NanoSerial.output.find("[NANO] SmartVest firmware 1.0")==0);
  assert(NanoSerial.output.find("[BATTERY] valid=1 voltage=12.000V percent=100% raw=100 smooth=100 hold=0 current_mA=2500 error=0")!=std::string::npos);
  assert(NanoSerial.output.find("charging=")==std::string::npos&&NanoSerial.output.find("net_mA=")==std::string::npos);
  assert(NanoSerial.output.find(" error=0 |B1:")!=std::string::npos);
  for(unsigned char b:NanoSerial.output)assert((b>=32&&b<=126)||b=='\r'||b=='\n');
  NanoBattery nano;nano.begin();assert(ports[1].rxPin==32&&ports[1].txPin==33&&ports[1].baud==19200);
  nanoToSense();nano.tick();assert(!nano.samples.seen); // Per-loop budget leaves an incomplete text frame.
  drainNano(nano);assert(nano.samples.fresh(clockMs));assert(nano.samples.data.busMV==12000);
  assert(nano.samples.data.percent==100&&nano.samples.data.currentMA==2500);
  EventLink sense;assert(sense.begin());EmergencyCoordinator events;SensorLink main(events);main.begin();
  assert(!sense.sendBattery(nano.samples.snapshot(clockMs)));
  sense.tick(CrashProtocol::Frame());transfer(2,0);main.tick();transfer(0,2);sense.tick(CrashProtocol::Frame());
  assert(sense.labActive());assert(!main.labOnline()); // No IMU/gyro calibration needed for battery.
  assert(sense.sendBattery(nano.samples.snapshot(clockMs)));transfer(2,0);main.tick();
  assert(main.batterySource().fresh(clockMs)&&main.batterySource().data.percent==100);
  const unsigned nvs=FakeNvs::writes;
  clockMs=2000;assert(sense.sendBattery(nano.samples.snapshot(clockMs)));transfer(2,0);main.tick();
  assert(main.batterySource().age(clockMs)==1000&&main.batterySource().duplicates==1);
  assert(FakeNvs::writes==nvs);
  clockMs=7000;assert(!nano.samples.fresh(clockMs)&&!main.batterySource().fresh(clockMs));
  assert(nano.samples.snapshot(clockMs).percent==255);
  // Signed current, immediate offline status, recovery, short reads and reset.
  Wire.regs[4]=uint16_t(int16_t(-16384));Wire.regs[3]=65535;nanoLoop();nanoToSense();drainNano(nano);
  assert(nano.samples.fresh(clockMs)&&nano.samples.data.currentMA==-2500&&nano.samples.data.powerMW==30000);
  Wire.regs[3]=5243;
  sense.tick(CrashProtocol::Frame());transfer(2,0);main.tick();transfer(0,2);sense.tick(CrashProtocol::Frame());
  assert(sense.sendBattery(nano.samples.snapshot(clockMs)));transfer(2,0);main.tick();assert(main.batterySource().fresh(clockMs));
  // A partial first transaction must recover through a STOP-separated retry,
  // but neither a persistent partial read nor a timeout may become valid data.
  Wire.repeatedStartShort=true;const auto recoveredBefore=recoveredReads;
  clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(nano.samples.fresh(clockMs));
  assert(recoveredReads>=recoveredBefore+7&&nano.samples.data.error==0);
  Wire.repeatedStartShort=false;Wire.shortRead=true;const auto requestsBefore=Wire.requests;
  clockMs+=1000;nanoLoop();
  assert(NanoSerial.output.find("valid=0 voltage=unavailable percent=unavailable")==10);
  assert(NanoSerial.output.size()<256);
  assert(NanoSerial.output.find("charging=")==std::string::npos);
  nanoToSense();drainNano(nano);assert(!nano.samples.fresh(clockMs));
  assert(nano.samples.data.error==11&&!(nano.samples.data.flags&Valid));
  assert(Wire.requests==requestsBefore+2); // Bounded attempts even on a broken bus.
  // Reconfiguration takes real time. Do not unsigned-underflow and sample
  // immediately with a timestamp captured before configuration completed.
  Wire.shortRead=false;Wire.configureDelay=10;clockMs+=5000;
  const auto sequenceBefore=reading.sequence;nanoLoop();assert(configured);
  assert(reading.sequence==sequenceBefore&&NanoSerial.output.empty());
  clockMs+=999;nanoLoop();assert(reading.sequence==sequenceBefore);
  clockMs+=1;nanoLoop();nanoToSense();drainNano(nano);assert(nano.samples.fresh(clockMs));
  Wire.configureDelay=0;Wire.readTimeout=true;clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);
  assert(!nano.samples.fresh(clockMs)&&nano.samples.data.error==12&&lastReadTimeout);
  Wire.readTimeout=false;clockMs+=5000;nanoLoop();clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(nano.samples.fresh(clockMs));
  Wire.regs[5]=0;clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(!nano.samples.fresh(clockMs)&&nano.samples.data.error==8);
  clockMs+=5000;nanoLoop();clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(nano.samples.fresh(clockMs));
  Wire.regs[6]=4;clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(!nano.samples.fresh(clockMs)&&nano.samples.data.flags&Overflow);
  Wire.regs[6]=8;Wire.connected=false;clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(!nano.samples.fresh(clockMs));
  assert(nano.samples.data.error==2);
  Wire.connected=true;Wire.regs[0xfe]=0;clockMs+=5000;nanoLoop();clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);
  assert(!configured&&nano.samples.data.error==6&&!nano.samples.fresh(clockMs));
  Wire.regs[0xfe]=0x5449;clockMs+=5000;nanoLoop();clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(nano.samples.fresh(clockMs));
  // Displayed empty remains a genuine valid measurement: reserve indication
  // must not become unavailable or stop the measurement/relay at low voltage.
  Wire.regs[1]=0; // The 10.6 V endpoint is source voltage, not bus plus a drop.
  for(uint32_t mv:{10600u,9600u,9500u,9000u}){
    Wire.regs[2]=uint16_t(mv*4/5);
    for(unsigned i=0;i<3;++i){clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);}
    clockMs+=1000;nanoLoop();
    assert(NanoSerial.output.find("percent=0%")!=std::string::npos);
    nanoToSense();drainNano(nano);assert(nano.samples.fresh(clockMs));
    assert(nano.samples.data.percent==0&&nano.samples.data.busMV==mv);
    sense.tick(CrashProtocol::Frame());transfer(2,0);main.tick();transfer(0,2);sense.tick(CrashProtocol::Frame());
    assert(sense.sendBattery(nano.samples.snapshot(clockMs)));transfer(2,0);main.tick();
    assert(main.batterySource().fresh(clockMs)&&main.batterySource().data.percent==0);
  }
  Wire.regs[2]=10080;clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);
  assert(nano.samples.fresh(clockMs)&&nano.samples.data.percent==0&&!batteryDisplay.riseHeld());
  assert(nano.samples.data.busMV==12600); // Voltage updates immediately, percentage does not jump.
  Wire.regs[2]=7;clockMs+=1000;nanoLoop();
  assert(NanoSerial.output.find("valid=0 voltage=unavailable")!=std::string::npos);
  assert(NanoSerial.output.find("charging=")==std::string::npos);
  assert(NanoSerial.output.find("error=10 raw_mV=9")!=std::string::npos);
  assert(NanoSerial.output.size()<256);
  nanoToSense();drainNano(nano);assert(!nano.samples.fresh(clockMs)&&nano.samples.data.error==10);
  Wire.regs[2]=9600;clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);
  assert(nano.samples.fresh(clockMs));
  // Exactly one physical sensor: exercise signed current across both UARTs.
  auto pumpSamples=[&](int rawMA,unsigned seconds){
    Wire.regs[4]=uint16_t(int16_t(std::lround(rawMA/(5000.0/32768))));
    for(unsigned i=0;i<seconds;++i){
      clockMs+=1000;nanoLoop();assert(NanoSerial.output.size()<256);
      nanoToSense();drainNano(nano);
      sense.tick(CrashProtocol::Frame());transfer(2,0);main.tick();transfer(0,2);sense.tick(CrashProtocol::Frame());
      assert(sense.sendBattery(nano.samples.snapshot(clockMs)));transfer(2,0);main.tick();
    }
  };
    // Owner-confirmed unplugged trace caused 127 false Charging samples in
    // the withdrawn voltage classifier. Replay each captured voltage through
    // actual Nano sampling and both UART links; current is zero as captured.
    for(auto mv:NOT_CHARGING_CAPTURE_MV){
      Wire.regs[2]=uint16_t(uint32_t(mv)*4/5);pumpSamples(0,1);
      assert(main.batterySource().data.charge==ChargeUnknown);
      assert(main.batterySource().data.netCurrentMA==0);
      assert(main.batterySource().fresh(clockMs));
    }
    // Huge voltage rises and signed load current never populate the retired
    // charging fields. There is no remaining charging mode to enable.
    Wire.regs[2]=uint16_t(11100*4/5);pumpSamples(-500,45);
    Wire.regs[2]=uint16_t(12600*4/5);pumpSamples(-500,120);
    assert(main.batterySource().data.charge==ChargeUnknown);
    Wire.regs[2]=uint16_t(11100*4/5);pumpSamples(0,16);
    assert(main.batterySource().data.charge==ChargeUnknown);
    assert(main.batterySource().snapshot(clockMs+6000).charge==ChargeUnknown);
  // Bounded parser survives noise, corrupt frames and arbitrary byte splits.
  Reading r;r.sequence=1;r.uptime=100;r.age=0;r.flags=Responding|Valid;r.busMV=12000;r.profile=1;r.percent=60;
  uint8_t packet[SIZE];encode(r,packet);Parser parser;Reading out;
  auto crcPacket=[&](){const auto c=crc16(packet,SIZE-2);packet[38]=uint8_t(c);packet[39]=uint8_t(c>>8);};
  packet[2]=1;crcPacket();assert(decode(packet,out)&&out.charge==ChargeUnknown);
  packet[35]=1;crcPacket();assert(!decode(packet,out)); // Legacy version has no charge field.
  packet[2]=2;packet[36]=244;packet[37]=1;crcPacket();assert(decode(packet,out)&&out.charge==Charging&&out.netCurrentMA==500);
  packet[35]=4;crcPacket();assert(!decode(packet,out)); // v2 cannot silently carry an estimate.
  packet[2]=3;crcPacket();assert(!decode(packet,out)); // Estimates cannot carry net current.
  packet[36]=packet[37]=0;crcPacket();assert(decode(packet,out)&&out.charge==VoltageCharging);
  packet[35]=VoltageNotCharging;crcPacket();assert(decode(packet,out));
  packet[35]=6;crcPacket();assert(!decode(packet,out));
  packet[36]=1;
  packet[35]=0;crcPacket();assert(!decode(packet,out)); // Unknown cannot carry a fabricated net value.
  encode(r,packet);

  auto textFrame=[&](){
    std::string s="readable status |B1:";const char* h="0123456789ABCDEF";
    for(uint8_t b:packet){s+=h[b>>4];s+=h[b&15];}return s+"\r\n";
  };
  BatteryTextFrame textParser;auto line=textFrame();bool textAccepted=false;
  for(uint8_t b:line)textAccepted|=textParser.feed(b,10,out);
  assert(textAccepted&&out.percent==60&&out.busMV==12000);
  auto corrupt=line;corrupt[corrupt.size()-4]=corrupt[corrupt.size()-4]=='0'?'1':'0';
  textAccepted=false;for(uint8_t b:corrupt)textAccepted|=textParser.feed(b,20,out);assert(!textAccepted);
  textAccepted=false;for(uint8_t b:line.substr(0,line.size()-2)+"0\n")textAccepted|=textParser.feed(b,30,out);assert(!textAccepted);
  for(unsigned i=0;i<line.size()/2;++i)textParser.feed(line[i],40,out);
  textAccepted=false;for(unsigned i=line.size()/2;i<line.size();++i)textAccepted|=textParser.feed(line[i],140,out);assert(!textAccepted);
  textAccepted=false;for(uint8_t b:line)textAccepted|=textParser.feed(b,150,out);assert(textAccepted);
  // New decoder still accepts the previous raw binary firmware.
  r.sequence=99;encode(r,packet);for(uint8_t b:packet)ports[1].rx.push_back(b);
  drainNano(nano);assert(nano.samples.data.sequence==99&&nano.samples.fresh(clockMs));
  std::mt19937 rng(42);for(unsigned i=0;i<100000;++i)parser.feed(uint8_t(rng()),i,out);
  for(unsigned i=0;i<100000;++i)textParser.feed(uint8_t(rng()),i,out);
  line=textFrame();textAccepted=false;for(uint8_t b:line)textAccepted|=textParser.feed(b,200000,out);assert(textAccepted);
  bool accepted=false;for(unsigned i=0;i<SIZE;++i)accepted|=parser.feed(packet[i],200000+i,out);assert(accepted);
  packet[20]^=1;accepted=false;for(uint8_t b:packet)accepted|=parser.feed(b,200100,out);assert(!accepted);
  Tracker wrap;wrap.accept(r,0xfffffff0);assert(wrap.fresh(0x10)&&wrap.age(0x10)==32);assert(!wrap.fresh(0x2000));
  r.age=5000;r.sequence=2;wrap.accept(r,100);assert(wrap.fresh(1099)&&!wrap.fresh(1100));
  sense.tick(CrashProtocol::Frame());transfer(2,0);main.tick();transfer(0,2);sense.tick(CrashProtocol::Frame());
  CrashProtocol::Frame crash;crash.type=CrashProtocol::Type::AutomaticCrash;crash.flags=7;crash.uptimeMs=millis();
  Wire.regs[2]=uint16_t(11200*4/5); // Owner's reported 11.2 V is inside the accepted pack range.
  assert(sense.enqueue(crash));assert(sense.pending());assert(!sense.sendBattery(nano.samples.snapshot(clockMs)));
  // A lost/withheld durable ACK must not starve battery data for minutes.
  // Preserve the Hello lease but intentionally withhold every incident ACK.
  auto helloOnly=[&](){
    ImuLabProtocol::Parser reader;CrashProtocol::Frame v1;ImuLabProtocol::Frame v2;
    for(uint8_t byte:ports[0].tx)if(reader.feed(byte,clockMs,v1,v2)==2&&v2.type==ImuLabProtocol::Type::Hello){
      uint8_t bytes[ImuLabProtocol::MAX_FRAME];const auto n=ImuLabProtocol::encode(v2,bytes);
      for(size_t i=0;i<n;++i)ports[2].rx.push_back(bytes[i]);
    }
    ports[0].tx.clear();
  };
  const auto writesBeforePendingTelemetry=FakeNvs::writes;
  for(unsigned second=0;second<10;++second){
    clockMs+=1000;nanoLoop();nanoToSense();drainNano(nano);assert(nano.samples.fresh(clockMs));
    sense.tick(CrashProtocol::Frame());transfer(2,0);main.tick();helloOnly();sense.tick(CrashProtocol::Frame());
    assert(sense.pending()==1&&sense.labActive());
    // Crash bytes are offered first; available idle capacity remains usable.
    assert(sense.sendBattery(nano.samples.snapshot(clockMs)));
    transfer(2,0);main.tick();helloOnly();
    assert(main.batterySource().fresh(clockMs)&&main.batterySource().data.busMV==11200);
  }
  assert(FakeNvs::writes==writesBeforePendingTelemetry);
  ImuLabProtocol::Frame probe;probe.length=SIZE;probe.boot=sense.bootId();probe.session=1;
  uint8_t probeBytes[ImuLabProtocol::MAX_FRAME];const size_t batteryBytes=ImuLabProtocol::encode(probe,probeBytes);
  ports[2].capacity=int(batteryBytes+CrashProtocol::FRAME_SIZE-1);
  assert(!sense.sendBattery(nano.samples.snapshot(clockMs)));
  ports[2].capacity=512;
  clockMs+=SensorConfig::EVENT_RETRY_MS;
  assert(!sense.sendBattery(nano.samples.snapshot(clockMs))); // Due incident retains priority.
  sense.tick(CrashProtocol::Frame());transfer(2,0);main.tick();helloOnly();sense.tick(CrashProtocol::Frame());
  auto fault=nano.samples.snapshot(clockMs);++fault.sequence;fault.flags=0;fault.percent=255;
  fault.age=UINT32_MAX;fault.error=2;fault.charge=ChargeUnknown;fault.netCurrentMA=0;
  assert(sense.sendBattery(fault));transfer(2,0);main.tick();
  assert(main.batterySource().seen&&!main.batterySource().fresh(clockMs)&&main.batterySource().data.error==2);
  std::cout<<"PASS pending incident ACK loss does not starve valid or fault battery packets; due-event priority, TX reserve, age/invalid flags and zero telemetry NVS writes retained\n";
  std::cout<<"PASS Nano charging feature removed: captured voltages, signed load currents and both UARTs preserve unknown legacy fields; Serial contains no charging/net-current status\n";
  std::cout<<"PASS actual Nano sampling -> Sensing UART1 -> negotiated shared UART -> actual Main receiver, independent of IMU/calibration\n";
  std::cout<<"PASS CRC/noise/splits, source-age expiry, duplicate no-refresh, signed current, short reads, INA reset/overflow/disconnect/recovery, rollover, zero telemetry NVS writes\n";
  std::cout<<"PASS STOP retry recovery, bounded partial reads/timeouts, retry conversion wait, wrong device rejected\n";
  std::cout<<"PASS readable validity/voltage/percentage, text CRC corruption/length/timeout/noise recovery, UART budget splits and legacy raw frame compatibility\n";
  std::cout<<"PASS requested reserve-curve endpoints, interpolation, monotonic range/clamping and actual valid 0% relay at/below 9.6V\n";
  std::cout<<"PASS ten-percent display, median-confirmed rise hold/rate, symmetric voltage filtering and spike rejection, hysteresis, sustained low-voltage bypass, outage and rollover\n";
}
