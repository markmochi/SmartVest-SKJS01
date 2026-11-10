#include "src/link/SensorLink.h"
#include "src/gsm/SIM800LService.h"
#include "src/gps/NEO6MService.h"
#include "src/power/BatteryService.h"
#include "src/web/ResponseMemory.h"
#include "src/web/HttpRequest.h"
#include "src/network/ApState.h"
#include "src/core/Version.h"
#include "src/diagnostics/Console.h"
#include "CameraPresence.h"
#include "CameraAuth.h"
#include <esp_system.h>
#include <cassert>
#include <iostream>
uint32_t clockMs=1000;
static_assert(sizeof(void*)==4,"Use a 32-bit host target for the ESP32 JSON pool profile");
size_t heapBudget=65536;
ResponseMemory::Health* liveHealth=nullptr;
bool allowReserve(size_t n){return n+(liveHealth?liveHealth->lastDocumentPeakBytes:0)<=heapBudget;}
struct FakeESP {
 uint32_t getFreeHeap(){return uint32_t(heapBudget);}
 uint32_t getMinFreeHeap(){return 40000;}
 uint32_t getMaxAllocHeap(){return uint32_t(heapBudget);}
} ESP;
namespace Console {
 void line(const char*){} void format(const char*,...){}
 uint32_t droppedLines(){return 0;} uint32_t transmittedBytes(){return 1000;}
}
constexpr int HTTP_POST=1;
const char* cameraKey(int){return "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";}
const char* cameraRole(int){return "front";}
bool cameraFrameFreshAtReport(const JsonDocument& d){return d["currentFrames"].as<unsigned>()>0;}
struct DashboardServer {
 static JsonDocument makeJson(){return JsonDocument();}
 struct Web {
  uint32_t admitted=1,rejected=0,expired=0,maxHandleMs=1,malformedRequests=0,oversizedRequests=0,requestMemoryFailures=0,responseMemoryFailures=0,headerTimeouts=0,bodyTimeouts=0,peerDisconnects=0,responseCompleted=0,maxQueuedBytes=0,writeAborts=0,flashCompleted=0,maxFlashMs=0;
  unsigned waiting(){return 0;}unsigned sending(){return 0;}unsigned queuedBytes(){return 0;}
  int method(){return 0;}String uri(){return "/api/v1/status";}
  String view;String arg(const char*){return view;}
  int code=0;String output;void sendHeader(const char*,const char*){}
  void sendOwned(int status,const char*,String&& data){code=status;output=std::move(data);}
  void send(int status,const char*,const String& data){code=status;output=data;}
 } web;
 struct Network {
  unsigned apStarts(){return 1;}unsigned apStops(){return 0;}unsigned clientJoins(){return 4;}unsigned clientLeaves(){return 0;}unsigned hotspotClients(){return 4;}
  bool apActive(){return true;}ApState radioState(){return {};}
  String networkName(){return "Test-Vest";}const char* modeName(){return "vest_hotspot";}
  bool stationConnected(){return false;}String dashboardUrl(){return "http://192.168.4.1/";}String localIp(){return "192.168.4.1";}
  bool mdnsActive(){return false;}bool captivePortalActive(){return false;}bool localDeviceMode(){return true;}bool connecting(){return false;}
  unsigned attempts(){return 1;}unsigned retryInMs(){return 0;}String error(){return "";}int signalDbm(){return -50;}
 }network;
 StateStore store;SIM800LService sim800l{store};NEO6MService neo6m;BatteryService battery;
 EmergencyCoordinator coordinator{store,neo6m};SensorLink sensor{coordinator};
 SensorLink* sensorLink=&sensor;EmergencyCoordinator* emergency=&coordinator;
 struct Camera:CameraPresence {bool recording=true,sdReady=true,lockSupported=true;String firmware="camera-1.0";uint32_t heartbeats=1,commands=1,acknowledgements=1;JsonDocument telemetry;}cameras[2];
 struct {bool busy()const{return false;}uint32_t lastMs=0,maxMs=0;}login;
 ResponseMemory::Health responseHealth;int cameraResponseRole=-1;CameraAuth::Exchange cameraExchange;
 bool locked=true;
 void releaseState(){locked=false;}void addSecurityHeaders(){assert(!locked);}
 void sendJson(JsonDocument&,int=200);void sendStatus();void sendRiderStatus();void writeMessageQueue(JsonObject,bool=true);void cancelMessageSelection(JsonObjectConst);
 void sendError(int status,const String& error){JsonDocument doc;doc["error"]=error;sendJson(doc,status);}
};
#include "handlers.inc"
void check(DashboardServer& d,unsigned incidents){
 d.locked=true;d.sendStatus();
 if(d.web.code!=200)std::cerr<<"status failed: incidents="<<incidents<<" stage="<<unsigned(d.responseHealth.lastFailureStage)<<" peak="<<d.responseHealth.lastDocumentPeakBytes<<"\n";
 assert(d.web.code==200&&!d.locked);
 JsonDocument result;assert(!deserializeJson(result,d.web.output.c_str()));
 assert(result["gsm"].is<JsonObject>()&&result["gps"].is<JsonObject>()&&result["power"].is<JsonObject>()&&result["cameras"].size()==2);
 assert(result["emergency"]["incidents"].size()==incidents);
 assert(d.responseHealth.lastBodyBytes==d.web.output.length());
 assert(d.web.output.length()+512<=24*1024);
 std::cout<<"PASS full admin status: incidents="<<incidents<<" peakDocument="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<d.web.output.length()<<" hostCombined="<<d.responseHealth.lastDocumentPeakBytes+d.web.output.length()<<"\n";
}
void cancellationChecks(){
 heapBudget=65536;
 DashboardServer q;liveHealth=&q.responseHealth;q.coordinator.begin();
 q.store.data.contacts[0].phone="+639170000001";q.store.data.contacts[0].name="First";
 q.store.data.contacts[1].phone="+639170000002";q.store.data.contacts[1].name="Second";
 CrashProtocol::Frame f;f.boot=55;f.sequence=1;f.type=CrashProtocol::Type::AutomaticCrash;
 assert(q.coordinator.accept(f));q.coordinator.tick();
 auto cancel=[&](const char* json){JsonDocument request;assert(!deserializeJson(request,json));q.locked=true;q.cancelMessageSelection(request.as<JsonObjectConst>());JsonDocument response;assert(!deserializeJson(response,q.web.output.c_str()));return response;};
 auto result=cancel("{\"boot\":55,\"sequence\":1,\"recipient\":0}");assert(q.web.code==200&&result["cancelled"]==1);
 assert(q.coordinator.journal.entries[0].recipients[0].outcome==Incidents::Cancelled);
 assert(q.coordinator.journal.entries[0].recipients[1].outcome==Incidents::Waiting);
 assert(q.coordinator.journal.entries[0].camera[0]==0&&q.coordinator.journal.entries[0].camera[1]==0);
 auto& active=q.store.data.jobs[1];assert(active.eventBoot==55&&q.coordinator.submissionGuard(active));
 active.state="sending";q.sim800l.activeJob=1;q.sim800l.phase=SIM800LService::Phase::Result;q.sim800l.waiting=true;
 assert(!q.sim800l.canCancel(active.id));
 assert(StateStore::enqueueSms(q.store.data,"+639170000003","Manual text","addition"));
 result=cancel("{\"all\":true}");assert(q.web.code==200&&result["cancelled"]==1&&result["alreadySending"]==1);
 assert(active.state=="sending"&&q.sim800l.activeJob==1&&q.sim800l.phase==SIM800LService::Phase::Result);
 result=cancel("{\"boot\":55,\"sequence\":1,\"recipient\":1}");assert(q.web.code==409&&active.state=="sending");
 result=cancel("{\"boot\":0,\"sequence\":0,\"recipient\":0}");assert(q.web.code==400);
 result=cancel("{\"all\":1}");assert(q.web.code==400);
 result=cancel("{\"id\":true}");assert(q.web.code==400);
 assert(StateStore::enqueueSms(q.store.data,"+639170000004","Another text","addition"));
 const auto last=q.store.data.nextJob-1;q.store.failWrites=true;
 const auto payload=String("{\"id\":")+String(last)+"}";result=cancel(payload.c_str());assert(q.web.code==503&&result["cancelled"]==0);
 q.store.failWrites=false;result=cancel(payload.c_str());assert(q.web.code==200&&result["cancelled"]==1);
 DashboardServer partial;liveHealth=&partial.responseHealth;partial.coordinator.begin();
 partial.store.data.contacts[0].phone="+639170000001";f.boot=66;assert(partial.coordinator.accept(f));partial.coordinator.tick();
 assert(StateStore::enqueueSms(partial.store.data,"+639170000005","Manual","addition"));partial.store.failWrites=true;
 JsonDocument all;all["all"]=true;partial.cancelMessageSelection(all.as<JsonObjectConst>());
 JsonDocument outcome;assert(!deserializeJson(outcome,partial.web.output.c_str()));
 assert(partial.web.code==200&&outcome["cancelled"]==1&&outcome["notCancelled"]==1&&outcome["ok"]==false);
 assert(partial.coordinator.journal.entries[0].recipients[0].outcome==Incidents::Cancelled);
 assert(partial.store.data.jobs[1].state=="queued");
 DashboardServer before;liveHealth=&before.responseHealth;
 assert(StateStore::enqueueSms(before.store.data,"+639170000001","Never submit this","addition"));
 auto& job=before.store.data.jobs[0];job.state="sending";before.sim800l.activeJob=0;before.sim800l.phase=SIM800LService::Phase::Prompt;before.sim800l.waiting=true;
 assert(before.sim800l.canCancel(job.id));const int submissions=::modem.submissions;
 JsonDocument request;request["id"]=job.id;before.cancelMessageSelection(request.as<JsonObjectConst>());
 assert(before.web.code==200&&job.state=="cancelled"&&before.sim800l.activeJob==-1);
 assert(before.sim800l.phase==SIM800LService::Phase::SyncQuiet&&before.sim800l.txBytes==2);
 ::modem.inject("> ");before.sim800l.tick();assert(::modem.submissions==submissions);
 std::cout<<"PASS actual cancellation handler: per-recipient and all-pending, untouched camera work, submitted-SMS refusal, strict selectors, storage failure/retry, pre-body ESC and late-prompt isolation\n";
}
int main(){
 DashboardServer d;liveHealth=&d.responseHealth;
 for(auto& c:d.cameras){c.heartbeat(clockMs,30000);c.telemetry["currentFrames"]=30;}
 check(d,0);
 auto& modem=d.sim800l;modem.observeSim("READY");modem.registration=1;modem.registrationObserved=true;modem.registrationAt=clockMs;
 modem.signalObserved=true;modem.signalAt=clockMs;modem.csq=20;modem.smsConfigured=true;modem.goodReplySeen=true;modem.lastGood=clockMs;modem.phase=SIM800LService::Phase::Idle;
 modem.operatorName=std::string(40,'o');modem.operatorCode="51502";modem.simCarrierName=std::string(40,'c');modem.simCarrierCode="51502";
 modem.subscriberNumber="+639170000001";modem.subscriberNumberStatus="Available";modem.iccid="8963021234567890123";modem.iccidStatus="Read successfully";
 // Distinct maximum-length diagnostics prevent string deduplication masking cost.
 String* diagnostics[]={&modem.lastError,&modem.lastCommand,&modem.lastErrorCommand,&modem.lastSmsError,&modem.lastSmsErrorCommand,&modem.lastPowerEvent,&modem.lastModemLine,&modem.serviceCenter,&modem.lastDeliveryReport,&modem.lastRegistrationResponse,&modem.recoveryStatus};
 for(unsigned i=0;i<sizeof(diagnostics)/sizeof(*diagnostics);++i)*diagnostics[i]=std::string(159,char('A'+i));
 d.sensor.seen=true;d.sensor.lastSeen=clockMs;d.sensor.heartbeat.flags=CrashProtocol::Armed|CrashProtocol::ImuReady|CrashProtocol::PiezoReady|CrashProtocol::Durable;
 d.sensor.heartbeat.boot=123;
 d.neo6m.begin();
 ::modem.inject("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");d.neo6m.tick();
 for(unsigned i=0;i<5;++i){auto& job=d.store.data.jobs[i];job.id=i+1;job.kind="crash";job.phone="+639170000001";job.state="accepted";job.detail=std::string(159,char('a'+i));}
 for(auto& c:d.store.data.contacts)c.phone="+639170000001";
 check(d,0);
 for(unsigned i=0;i<Incidents::LIMIT;++i){
  auto& e=d.coordinator.journal.entries[i];e.boot=123;e.sequence=i+1;e.count=5;e.ageKnown=true;e.ageAtReceipt=100;
  memset(e.message,'m'+i,160);e.message[160]=0;
  auto& t=d.coordinator.timings[i];t.boot=e.boot;t.sequence=e.sequence;t.automaticFrame=true;
  auto& v=e.evidence;v.captured=1;v.utcMs=1793188862000ULL;v.timeSource=2;v.timeEstimated=1;
  v.motion=1;v.accelerationMg=2800;v.rotationDps=156;v.gps=4;v.latitudeE7=145995000;v.longitudeE7=1209842000;
  v.modem=2;v.mobileService=4;v.battery=2;v.batteryPercent=60;v.voltageMv=11200;v.piezo=2;v.impactKnown=1;v.impactBanks=1;v.camera[0]=2;v.camera[1]=1;
  for(unsigned n=0;n<5;++n){auto& r=e.recipients[n];strcpy(r.phone,"+639170000001");memset(r.name,char('A'+i*5+n),40);r.name[40]=0;}
  check(d,i+1);
 }
 const auto responses=d.responseHealth.responses;
 for(unsigned i=0;i<100;++i) {d.sendStatus();assert(d.web.code==200);}
 assert(d.responseHealth.responses==responses+100&&d.responseHealth.documentFailures==0);
 // Printable quotes/backslashes exercise JSON escaping without inventing
 // invalid sensor text or altering the fixed journal/message schemas.
 for(unsigned i=0;i<sizeof(diagnostics)/sizeof(*diagnostics);++i)*diagnostics[i]=std::string(1,char('A'+i))+std::string(158,i%2?'"':'\\');
 check(d,4);
 // A bounded available working budget reproduces the old full-response failure.
 heapBudget=16384;d.sendStatus();assert(d.web.code==503);
 JsonDocument full;heapBudget=65536;d.sendStatus();assert(!deserializeJson(full,d.web.output.c_str()));
 heapBudget=16384;
 for(const char* view:{"overview","gsm","gps","power","network","system","tools"}){
  d.web.view=view;d.sendStatus();assert(d.web.code==200);
  JsonDocument scoped;assert(!deserializeJson(scoped,d.web.output.c_str()));
  const size_t working=d.responseHealth.lastDocumentPeakBytes+d.web.output.length();
  assert(working<=heapBudget);
  for(const char* section:{"gsm","gps","power","network","sensing","cameras"}){
   if(!scoped[section].isNull()){
    if((std::string(view)=="overview"&&std::string(section)=="gsm")||std::string(view)=="tools"){
     for(auto field:scoped[section].as<JsonObjectConst>())assert(field.value()==full[section][field.key().c_str()]);
    }else assert(scoped[section]==full[section]);
   }
  }
  assert(scoped["emergency"]["incidents"].isNull());
  std::cout<<"PASS scoped "<<view<<": document="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<d.web.output.length()<<" combined="<<working<<"\n";
 }
 // Pairing and SMS controls do not need incident, sensor or camera diagnostics.
 heapBudget=8192;d.web.view="tools";d.sendStatus();assert(d.web.code==200);
 JsonDocument tools;assert(!deserializeJson(tools,d.web.output.c_str()));
 assert(tools["sms"]==full["sms"]&&tools["power"]["ledTestSupported"]==true);
 for(const char* section:{"emergency","http","radio","gps","network","sensing","cameras"})assert(tools[section].isNull());
 assert(d.responseHealth.lastDocumentPeakBytes+d.web.output.length()<=8192);
 std::cout<<"PASS tools fits 8 KiB combined working budget with full incident history: document="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<d.web.output.length()<<" combined="<<d.responseHealth.lastDocumentPeakBytes+d.web.output.length()<<"\n";
 heapBudget=24576;d.web.view="ride";d.sendStatus();assert(d.web.code==200);
 JsonDocument ride;assert(!deserializeJson(ride,d.web.output.c_str()));
 for(const char* section:{"gsm","gps","power","network","sensing","cameras"})assert(ride[section]==full[section]);
 assert(ride["sms"].isNull()&&ride["emergency"].isNull()&&ride["http"].isNull());
 std::cout<<"PASS ride preserves all recorded modules: document="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<d.web.output.length()<<" combined="<<d.responseHealth.lastDocumentPeakBytes+d.web.output.length()<<"\n";
 d.web.view="unknown";d.sendStatus();assert(d.web.code==400);
 for(unsigned i=0;i<Config::MAX_SMS_JOBS;++i){auto& job=d.store.data.jobs[i];job.id=i+1;job.kind="crash";job.phone="+639170000001";job.state="accepted";job.detail=std::string(159,char('a'+i));}
 heapBudget=65536;d.web.view="";d.sendRiderStatus();assert(d.web.code==200);
 JsonDocument riderFull;assert(!deserializeJson(riderFull,d.web.output.c_str()));
 const size_t fullRider=d.web.output.length();
 std::cout<<"PASS full rider history baseline: document="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<fullRider<<" combined="<<d.responseHealth.lastDocumentPeakBytes+fullRider<<"\n";
 heapBudget=16384;
 for(const char* view:{"overview","equipment","activity"}){
  d.web.view=view;d.sendRiderStatus();assert(d.web.code==200);
  JsonDocument scoped;assert(!deserializeJson(scoped,d.web.output.c_str()));
  for(const char* section:{"monitoring","location","battery","cameras","contacts","activity"})
   if(!scoped[section].isNull()&&!(std::string(view)=="activity"&&std::string(section)=="monitoring"))assert(scoped[section]==riderFull[section]);
  if(std::string(view)=="overview")assert(scoped["cameras"].isNull()&&scoped["activity"].isNull());
  if(std::string(view)=="activity")assert(scoped["location"].isNull()&&scoped["cellular"].isNull());
  assert(d.web.output.length()<fullRider);
  std::cout<<"PASS rider "<<view<<": document="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<d.web.output.length()<<" combined="<<d.responseHealth.lastDocumentPeakBytes+d.web.output.length()<<"\n";
 }
 for(unsigned i=0;i<5;++i){d.store.data.contacts[i].name=std::string(40,char('U'+i));d.store.data.contacts[i].phone=String("+63917000000")+String(i+1);}
 for(unsigned i=0;i<Config::MAX_SMS_JOBS;++i){auto& job=d.store.data.jobs[i];job.state="queued";job.phone=d.store.data.contacts[i%5].phone;}

 d.web.view="activity";d.sendRiderStatus();
 if(d.web.code!=200)std::cerr<<"Worst queue response failed: document="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<d.web.output.length()<<"\n";
 assert(d.web.code==200);
 std::cout<<"PASS rider 4x5 alert recipients plus 20 pending manual texts: document="<<d.responseHealth.lastDocumentPeakBytes<<" body="<<d.web.output.length()<<" combined="<<d.responseHealth.lastDocumentPeakBytes+d.web.output.length()<<"\n";
 d.web.view="unknown";d.sendRiderStatus();assert(d.web.code==400);
 d.web.view="";
 heapBudget=1;d.sendStatus();assert(d.web.code==503&&!d.locked&&d.responseHealth.outputFailures>0&&d.responseHealth.lastFailureStage==2);
 heapBudget=65536;check(d,4);
 assert(modem.txBytes==0&&modem.commandsSent==0&&::modem.submissions==0);
 std::cout<<"PASS actual status/JSON handlers and service writers: startup, populated modem, 4 incidents x5 recipients, all timings, repeated reads, constrained output allocation and recovery; no physical I/O\n";
 cancellationChecks();
}
