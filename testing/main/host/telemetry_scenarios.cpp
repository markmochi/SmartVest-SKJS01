#include "src/gsm/SIM800LService.h"
#include "src/gps/NEO6MService.h"
#include "src/power/BatteryService.h"
#include "src/link/SensorLink.h"
#include "src/diagnostics/SerialReporter.h"
#include "src/core/Domain.h"
#include <cassert>
#include <cstdarg>
#include <iostream>
#include <memory>
uint32_t clockMs=100;
std::vector<std::string> logLines;
namespace Console {
void line(const char* value){logLines.emplace_back(value);}
void format(const char* value,...){char b[192];va_list a;va_start(a,value);vsnprintf(b,sizeof(b),value,a);va_end(a);line(b);}
}
struct Rig {
 StateStore store;SIM800LService gsm{store};int reg=1,radio=1;
 std::string sim="READY",iccid="8963021234567890123",imsi="515029123456789",number="+639171234567";
 bool quietSim=false,quietBody=false,removeBeforePrompt=false,quietIdentity=false,failSetupOnce=false;
 uint32_t simDelayMs=0,simReplyAt=0;
 std::string delayedSimReply;
 Rig(){modem=FakeModem();clockMs=100;modem.respond=[this](const std::string& c){
  assert(delayedSimReply.empty()); // No command may overtake an unanswered CPIN.
  std::string response;
  if(c=="AT+CPIN?"){if(quietSim)return;response=radio==0?"+CME ERROR: 10\r\n":sim=="READY"?"+CPIN: READY\r\nOK\r\n":sim+"\r\n";}
  else if(c=="ATI")response="SIM800 R14\r\nOK\r\n";
  else if(c=="AT+CMGF=1"&&failSetupOnce){failSetupOnce=false;sim="+CME ERROR: 10";response=sim+"\r\n";}
  else if(c=="AT+CSMS?")response="+CSMS: 0,1,1,1\r\nOK\r\n";
  else if(c=="AT+CREG?")response="+CREG: 1,"+std::to_string(reg)+"\r\nOK\r\n";
  else if(c=="AT+CSQ")response="+CSQ: 20,0\r\nOK\r\n";
  else if(c=="AT+CCID"){if(quietIdentity)return;response=iccid+"\r\nOK\r\n";}
  else if(c=="AT+CIMI")response=imsi+"\r\nOK\r\n";
  else if(c=="AT+CNUM")response="+CNUM: \"\",\""+number+"\",145\r\nOK\r\n";
  else if(c=="AT+COPS?")response="+COPS: 0,0,\"GLOBE\",0\r\nOK\r\n";
  else if(c=="AT+CFUN?")response="+CFUN: "+std::to_string(radio)+"\r\nOK\r\n";
  else if(c=="AT+CFUN=1"){radio=1;response="OK\r\n";}
  else if(c.rfind("AT+CMGS=",0)==0){modem.body=true;response=removeBeforePrompt?"+CSMINS: 1,0\r\n> ":"\r\n> ";}
  else if(c=="BODY"){if(quietBody)return;response="+CMGS: 7\r\nOK\r\n";}
  else response="OK\r\n";
  if(c=="AT+CPIN?"&&simDelayMs){assert(delayedSimReply.empty());delayedSimReply=response;simReplyAt=clockMs+simDelayMs;}
  else modem.inject(response);
 };gsm.begin();}
 void run(uint32_t duration){uint32_t start=clockMs;while(uint32_t(clockMs-start)<duration){if(!delayedSimReply.empty()&&int32_t(clockMs-simReplyAt)>=0){modem.inject(delayedSimReply);delayedSimReply.clear();}gsm.tick();clockMs+=10;}}
 void ready(){run(16000);assert(gsm.simReady());assert(gsm.registered());assert(gsm.subscriberNumber==number.c_str());}
 void queue(){auto& j=store.data.jobs[0];j.id=1;j.kind="check";j.phone="+639170000001";j.text="TEST: coordinates 14.5,120.5";j.state="queued";j.created=j.changed=clockMs;}
};
void modemTests(){
 {Rig r;r.sim="+CPIN: READY";r.run(18000);
  assert(!r.gsm.configured()&&r.gsm.commandTimeouts>0&&r.gsm.synchronizationRecoveries>0);
  r.sim="READY";r.ready();assert(r.gsm.configured());
  std::cout<<"PASS startup CPIN without final OK reacquires command mode before retrying\n";
 }
 {Rig r;r.ready();r.sim="+CPIN: READY";r.queue();r.run(6500);
  assert(modem.submissions==0&&r.gsm.commandTimeouts>0&&r.gsm.synchronizationRecoveries>0);
  r.sim="READY";r.run(20000);assert(modem.submissions==1&&r.store.data.jobs[0].state=="accepted");
  std::cout<<"PASS incomplete preflight CPIN reacquires command mode, submits nothing early and later recovers\n";
 }
 {Rig r;r.sim="+CME ERROR: 10";r.queue();r.run(125000);
  assert(r.gsm.online()&&!r.gsm.simReady()&&!r.gsm.simDetected());
  assert(r.gsm.simStatusText()=="SIM card not detected"&&!r.gsm.configured());
  assert(r.gsm.synchronizationRecoveries==0&&r.gsm.commandTimeouts==0);
  assert(std::count(modem.commands.begin(),modem.commands.end(),"AT+CPIN?")>=20);
  assert(std::count(modem.commands.begin(),modem.commands.end(),"AT+CFUN=1")==0);
  assert(modem.submissions==0&&r.store.data.jobs[0].attempts==0);
  r.sim="READY";r.run(20000);
  assert(r.gsm.configured()&&modem.submissions==1&&r.store.data.jobs[0].state=="accepted");
  std::cout<<"PASS persistent CME10 keeps fresh absence, polls for recovery and never submits until SIM and network become ready\n";
 }
 for(int code:{11,12,13,14,15}){
  Rig r;r.sim="+CME ERROR: "+std::to_string(code);r.run(20000);
  assert(r.gsm.online()&&!r.gsm.simReady()&&!r.gsm.configured());
  assert(r.gsm.simStatusText()!="SIM card not detected");
  assert(r.gsm.synchronizationRecoveries==0&&modem.submissions==0);
  if(code==11||code==12)assert(r.gsm.simDetected());
  r.sim="READY";r.ready();assert(r.gsm.configured());
 }
 std::cout<<"PASS PIN, PUK, failure, busy and wrong-SIM responses stay distinct from absence and recover after READY\n";
 {Rig r;r.sim="ERROR";r.run(20000);assert(!r.gsm.simReady());
  assert(r.gsm.simStatusText()!="SIM card not detected");
  r.sim="READY";r.ready();assert(r.gsm.configured());
  std::cout<<"PASS generic query rejection is unconfirmed, never invented physical absence\n";
 }
 {Rig r;r.ready();modem.respond=[](const std::string&){};r.run(40000);
  modem.inject(std::string(700,'X')+"\r\nnoise\r\n");r.run(10);
  assert(!r.gsm.online()&&!r.gsm.simReady()&&r.gsm.rxAgeMs()<1000);
  assert(r.gsm.simStatusText()!="SIM card not detected");
  std::cout<<"PASS unpowered/silent modem with raw RX noise never gains verified readiness or invented absent-card status\n";
 }
 for(uint32_t delay:{4000u,5000u}){
  Rig r;r.simDelayMs=delay;r.run(45000);
  assert(r.gsm.configured()&&r.gsm.simReady());
  assert(r.gsm.commandTimeouts==0&&r.gsm.synchronizationRecoveries==0);
  r.queue();r.run(30000);assert(modem.submissions==1&&r.store.data.jobs[0].state=="accepted");
  assert(r.gsm.commandTimeouts==0&&r.gsm.synchronizationRecoveries==0);
  std::cout<<"PASS delayed SIM replies within documented five seconds: startup, health polls and SMS preflight\n";
  Rig absent;absent.sim="+CME ERROR: 10";absent.simDelayMs=delay;absent.run(25000);
  assert(absent.gsm.simStatusText()=="SIM card not detected"&&!absent.gsm.configured());
  assert(absent.gsm.commandTimeouts==0&&absent.gsm.synchronizationRecoveries==0);
  absent.sim="READY";absent.run(45000);assert(absent.gsm.configured()&&absent.gsm.simReady());
  assert(absent.gsm.commandTimeouts==0&&absent.gsm.synchronizationRecoveries==0);
  std::cout<<"PASS delayed genuine CME10 remains absence and a later readable card recovers without resynchronization\n";
 }
 {Rig r;r.ready();r.quietSim=true;
  for(int i=0;i<600&&!(r.gsm.commandPending()&&r.gsm.lastCommand=="AT+CPIN?");++i)r.run(10);
  assert(r.gsm.commandPending()&&r.gsm.lastCommand=="AT+CPIN?");
  for(char c:std::string("+CPIN: READY\r\nOK\r\n")){modem.inject(std::string(1,c));r.run(10);}
  assert(r.gsm.simReady()&&r.gsm.commandTimeouts==0&&r.gsm.synchronizationRecoveries==0);
  std::cout<<"PASS fragmented CPIN reply and final OK survive one-byte UART delivery\n";
 }
 for(int status:{0,64})for(int completion:{0,1,2}){
  Rig r;r.ready();r.quietBody=true;r.queue();r.run(3000);
  assert(r.store.data.jobs[0].state=="sending"&&modem.submissions==1);
  auto report=[](int value){return "+CDS: 2,7,\"+639170000001\",145,\"26/10/26,10:00:00+32\",\"26/10/26,10:00:01+32\","+std::to_string(value)+"\r\n";};
  modem.inject("+CMGS: 7\r\n"+report(status));r.run(20);
  assert(r.store.data.jobs[0].state=="sending"&&r.store.data.jobs[0].deliveryStatus==status);
  const String carrierDetail=r.store.data.jobs[0].detail;
  modem.inject(report(32));r.run(20);
  assert(r.store.data.jobs[0].deliveryStatus==status&&r.store.data.jobs[0].detail==carrierDetail);
  for(size_t i=1;i<Config::MAX_SMS_JOBS;++i){auto& job=r.store.data.jobs[i];job.id=uint32_t(i+1);job.kind="crash";job.state="sending";}
  r.store.data.nextJob=Config::MAX_SMS_JOBS+1;
  assert(!StateStore::enqueueSms(r.store.data,"+639170000002","Second incident","crash"));
  // clear_history uses this same production terminal-state predicate.
  size_t cleared=0;
  for(auto& job:r.store.data.jobs)if(job.id&&Domain::isTerminalJobState(job.state.c_str())){job=SmsJob();++cleared;}
  assert(cleared==0&&r.store.data.jobs[0].id==1&&r.store.data.jobs[0].state=="sending");
  if(completion==0){modem.inject("OK\r\n");r.run(20);}
  else if(completion==1)r.run(Config::SMS_RESULT_TIMEOUT_MS+1000);
  else{modem.inject("RDY\r\n");r.run(20);}
  assert(r.store.data.jobs[0].state==(status==0?"delivered":"undelivered"));
  assert(r.store.data.jobs[0].deliveryStatus==status&&r.store.data.jobs[0].detail==carrierDetail);
  assert(modem.submissions==1);
  assert(StateStore::enqueueSms(r.store.data,"+639170000002","Second incident","crash"));
  assert(r.store.data.jobs[0].id==Config::MAX_SMS_JOBS+1&&r.store.data.jobs[0].state=="queued");
 }
 std::cout<<"PASS early delivered/undelivered report reserves active SMS slot; later temporary report cannot replace terminal result; final OK, timeout and RDY release slot with carrier outcome retained\n";
 for(int mode:{0,4}){Rig r;r.radio=mode;r.ready();assert(r.radio==1&&r.gsm.configured());assert(std::count(modem.commands.begin(),modem.commands.end(),"AT+CFUN=1")==1);std::cout<<"PASS confirmed minimum-function mode enables SIM/radio once before testing the card\n";}
 {Rig r;r.sim="+CME ERROR: 10";r.run(18000);assert(r.gsm.simStatusText()=="SIM card not detected");assert(!r.gsm.configured());assert(r.gsm.synchronizationRecoveries==0);assert(std::count(modem.commands.begin(),modem.commands.end(),"AT+CFUN=1")==0);assert(std::count(modem.commands.begin(),modem.commands.end(),"AT+CMGF=1")==0);r.sim="READY";r.ready();assert(r.gsm.configured());std::cout<<"PASS cold absent SIM waits without setup loop and completes setup when readable\n";}
 {Rig r;r.failSetupOnce=true;r.run(18000);assert(!r.gsm.configured());assert(r.gsm.synchronizationRecoveries==0);assert(std::count(modem.commands.begin(),modem.commands.end(),"AT+CMGF=1")==1);r.sim="READY";r.ready();assert(r.gsm.configured());std::cout<<"PASS CME10 during CMGF pauses setup; no UART resync loop, fresh SIM resumes setup\n";}
 {Rig r;r.ready();modem.inject("+CSMINS: 1,0\r\n");r.gsm.tick();assert(r.gsm.sim!="NOT INSERTED");assert(!r.gsm.simReady());r.ready();assert(r.gsm.configured());std::cout<<"PASS card-holder hint requires CPIN verification and cannot invent an absent SIM\n";}
 {Rig r;r.ready();r.sim="+CME ERROR: 10";r.run(6000);assert(!r.gsm.simDetected());assert(!r.gsm.simReady());assert(!r.gsm.registered());assert(r.gsm.simStatusText()=="SIM card not detected");assert(r.gsm.subscriberNumber.length()==0);assert(r.gsm.iccid.length()==0);assert(r.gsm.simCarrierCode.length()==0);assert(r.gsm.operatorCode.length()==0);std::cout<<"PASS SIM removal clears readiness, carrier, ICCID and phone number\n";}
 {Rig r;r.ready();modem.inject("+CSMINS: 1,0\r\n+CNUM: \"\",\"+639999999999\",145\r\n");r.gsm.tick();assert(!r.gsm.simReady());assert(r.gsm.subscriberNumber.length()==0);assert(!r.gsm.registered());std::cout<<"PASS removal URC and late number reply cannot revive old SIM identity\n";}
 {Rig r;r.ready();r.sim="+CME ERROR: SIM not inserted";r.run(6000);assert(r.gsm.simStatusText()=="SIM card not detected");std::cout<<"PASS textual SIM-removal error\n";}
 {Rig r;r.ready();r.quietSim=true;r.run(9000);assert(!r.gsm.simReady());assert(r.gsm.subscriberNumber.length()==0);assert(r.gsm.simCarrierCode.length()==0);assert(r.gsm.online());std::cout<<"PASS SIM-query silence invalidates identity despite other modem replies\n";}
 {Rig r;r.ready();r.iccid="8963039876543210123";r.imsi="515039123456789";r.number="+639181234567";r.run(45000);assert(r.gsm.iccid==r.iccid.c_str());assert(r.gsm.simCarrierName=="Smart");assert(r.gsm.subscriberNumber==r.number.c_str());std::cout<<"PASS changed ICCID reacquires new carrier and number\n";}
 {Rig r;r.ready();r.quietIdentity=true;r.run(40000);assert(r.gsm.synchronizationRecoveries>0);std::cout<<"PASS identity timeout recovers command ownership\n";}
 {Rig r;r.ready();r.sim="+CME ERROR: 10";r.queue();r.run(8000);assert(modem.submissions==0);assert(r.store.data.jobs[0].state=="queued");r.sim="READY";r.run(12000);assert(modem.submissions==1);assert(r.store.data.jobs[0].state=="accepted");std::cout<<"PASS queued SMS waits for SIM and resumes after a fresh CPIN/CREG check\n";}
 {Rig r;r.ready();r.quietBody=true;r.queue();r.run(3000);assert(r.store.data.jobs[0].state=="sending");const auto commands=modem.commands.size();r.run(20000);assert(!r.gsm.simReady());assert(r.gsm.subscriberNumber.length()==0);assert(modem.commands.size()==commands);modem.inject("+CMGS: 7\r\nOK\r\n");r.run(2000);assert(r.store.data.jobs[0].state=="accepted");assert(modem.submissions==1);std::cout<<"PASS expired status during SMS is unconfirmed; no AT commands interrupt the body/result\n";}
 {Rig r;r.ready();r.removeBeforePrompt=true;r.queue();r.run(3000);assert(modem.submissions==0);assert(r.store.data.jobs[0].state=="retry");std::cout<<"PASS SIM removal before prompt cancels input without submitting\n";}
 {Rig r;r.ready();r.quietBody=true;r.queue();r.run(90000);assert(modem.submissions==1);assert(r.store.data.jobs[0].state=="unknown");std::cout<<"PASS uncertain submitted SMS is never automatically duplicated\n";}
 {Rig r;clockMs=0xffffe000U;r.ready();r.sim="+CME ERROR: 10";r.run(6000);assert(!r.gsm.simReady());std::cout<<"PASS removal/freshness across millis wraparound\n";}
}
void peripheralTests(){
 modem=FakeModem();clockMs=100;NEO6MService gps;gps.begin();
 modem.inject("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");gps.tick();assert(gps.telemetry().decoding);assert(gps.telemetry().fresh);auto clockSentence=[](const std::string& text){unsigned checksum=0;for(char c:text)checksum^=unsigned(c);char suffix[12];snprintf(suffix,sizeof(suffix),"*%02X\r\n",checksum);return "$"+text+suffix;};
 uint64_t utc=0;
 modem.inject(clockSentence("GPRMC,023334.00,A,4807.038,N,01131.000,E,022.4,084.4,230319,003.1,W"));gps.tick();assert(!gps.latestUtcMs(utc));
 modem.inject(clockSentence("GPRMC,023334.00,A,4807.038,N,01131.000,E,022.4,084.4,021026,003.1,W"));gps.tick();assert(gps.latestUtcMs(utc));assert(utc==1790908414000ULL);
 clockMs+=1000;
 modem.inject(clockSentence("GPGGA,000000.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));gps.tick();assert(gps.latestUtcMs(utc));assert(utc==1790908415000ULL); // no date/time mixing at midnight
  clockMs+=5100;
 assert(!gps.telemetry().decoding);assert(!gps.telemetry().fresh);JsonDocument doc;gps.writeJson(doc.to<JsonObject>());assert(doc["state"].as<std::string>()=="stale");
 modem.inject(clockSentence("GPGGA,000010.00,4807.038,N,01131.000,E,1,08,5.0,545.4,M,46.9,M,,"));gps.tick();assert(!gps.weakGeometry());
 modem.inject(clockSentence("GPGGA,000011.00,4807.038,N,01131.000,E,1,08,5.1,545.4,M,46.9,M,,"));gps.tick();assert(gps.weakGeometry());
 JsonDocument geometry;gps.writeJson(geometry.to<JsonObject>());assert(geometry["hdop"].as<double>()==5.1);
 clockMs+=9999;assert(gps.weakGeometry());++clockMs;assert(!gps.weakGeometry());
 std::cout<<"PASS actual NMEA decode, valid UTC clock extraction, rejected old dates and disconnected GPS never shows a current fix\n";
 EmergencyCoordinator emergency;SensorLink sensor(emergency);sensor.begin();
 CrashProtocol::Frame frame;frame.boot=42;frame.type=CrashProtocol::Type::Heartbeat;frame.flags=CrashProtocol::ImuReady|CrashProtocol::PiezoReady|CrashProtocol::Armed|CrashProtocol::Durable;
 uint8_t bytes[CrashProtocol::FRAME_SIZE];CrashProtocol::encode(frame,bytes);modem.inject(std::string(reinterpret_cast<char*>(bytes),sizeof(bytes)));sensor.tick();assert(sensor.armedAndHealthy());clockMs+=Config::SENSOR_OFFLINE_MS;
 sensor.writeJson(doc.to<JsonObject>());assert(!sensor.telemetry().online);assert(!sensor.armedAndHealthy());assert(doc["reportedAccelerationMg"].isNull());
 std::cout<<"PASS stale sensing heartbeat hides readiness and sensor readings\n";
 BatteryService power;BatteryProtocol::Tracker batterySource;
 power.attachBattery(&batterySource);power.setRemoteLink(true);
 BatteryProtocol::Reading reading;reading.flags=3;reading.sequence=1;reading.uptime=1000;reading.age=0;
 reading.busMV=12000;reading.currentMA=100;reading.powerMW=1200;reading.profile=1;reading.percent=80;
 assert(batterySource.accept(reading,clockMs));assert(power.telemetry().valid);
 clockMs+=BatteryProtocol::STALE_MS;power.writeJson(doc.to<JsonObject>());
 assert(!power.telemetry().valid&&doc["sourceV"].isNull());
 power.setRemoteLink(false);assert(!power.telemetry().responding);
 std::cout<<"PASS remote battery sample expiry and link loss make measurements unavailable\n";
 StateStore store;SIM800LService gsm(store);RuntimeTelemetry runtime;runtime.networkOnline=true;runtime.webReady=false;std::snprintf(runtime.ip,sizeof(runtime.ip),"192.168.1.45");
 SerialReporter reporter;logLines.clear();reporter.begin();clockMs+=1000;reporter.tick(store,gsm,gps,power,sensor,emergency,runtime);
 for(auto& line:logLines)assert(line.find("Dashboard:")==std::string::npos);
 runtime.webReady=true;clockMs+=1000;reporter.tick(store,gsm,gps,power,sensor,emergency,runtime);size_t count=logLines.size();clockMs+=1000;reporter.tick(store,gsm,gps,power,sensor,emergency,runtime);assert(logLines.size()==count);
 bool link=false;for(auto& line:logLines){link|=line=="Dashboard: http://192.168.1.45/";assert(line.find("Password:")==std::string::npos);assert(line.find("UART")==std::string::npos);assert(line.find("heap")==std::string::npos);assert(line.find("Test-Home-Password")==std::string::npos);assert(line.find("Test-Vest-Password")==std::string::npos);}
 assert(link);
 std::cout<<"PASS concise serial output, unchanged states stay quiet; mode="<<(Config::USE_HOME_WIFI?"home":"hotspot")<<"\n";
 assert(Domain::smsLocation(14.5,120.5).find("://")==std::string::npos);
 assert(Domain::crashTestMessage("Alex",14.5,120.5,true,true).find("://")==std::string::npos);
 std::cout<<"PASS new SMS text contains coordinates without URLs\n";
}
int main(){modemTests();peripheralTests();}
