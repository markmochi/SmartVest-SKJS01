#include <Arduino.h>
#include <ArduinoJson.h>
#include "CameraAuth.h"
#include "CameraTelemetry.h"
#include "CameraPresence.h"
#include "DiagnosticPaging.h"
#include "ResponseMemory.h"
#include "Config.h"
#include <cassert>
#include <map>
#include <set>
#include <iostream>
uint32_t clockMs=1000;
constexpr int HTTP_POST=1;
ResponseMemory::Health* liveHealth=nullptr;
size_t heapBudget=32768;
bool allowReserve(size_t bytes){return !liveHealth||bytes+liveHealth->lastDocumentPeakBytes<=heapBudget;}
struct FakeESP{uint32_t getFreeHeap(){return uint32_t(heapBudget);}uint32_t getMaxAllocHeap(){return uint32_t(heapBudget);}} ESP;
inline void convertToJson(const String& value,JsonVariant out){out.set(JsonString(value.c_str(),false));}
const char* cameraKey(int){return "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";}
const char* cameraRole(int){return "front";}
struct SIM800LService{
#include "service_types.inc"
  TraceSnapshot traceData;
};
struct DashboardServer{
 static JsonDocument makeJson(){return JsonDocument();}
  struct Web{
    std::map<std::string,String> query;int status=0;String output;
    bool hasArg(const char* key){return query.count(key)!=0;}
    String arg(const char* key){return hasArg(key)?query.at(key):String();}
    int method(){return 0;}String uri(){return "/fixture";}
    void sendHeader(const char*,const char*){}
    void sendOwned(int code,const char*,String&& text){status=code;output=std::move(text);}
    void send(int code,const char*,const String& text){status=code;output=text;}
  }web;
  struct Camera:CameraPresence{
    bool recording=true,sdReady=true,lockSupported=true;
    uint32_t heartbeats=1,commands=1,acknowledgements=1;
    String firmware="camera-1.0";JsonDocument telemetry;CameraMonitor::Cursor cursor;
  }cameras[2];
  SIM800LService sim800l;CameraMonitor::Journal cameraLogs;
  ResponseMemory::Health responseHealth;uint32_t diagnosticEpoch=77;
  int cameraResponseRole=-1;CameraAuth::Exchange cameraExchange;
  bool locked=false,authorized=true,mutateOnRelease=false;
  bool* authorize(){locked=true;if(!authorized){sendError(401,"Sign in");return nullptr;}return &authorized;}
  void releaseState(){
    locked=false;
    if(mutateOnRelease){for(auto& e:cameraLogs.rows)strcpy(e.detail,"MUTATED");for(auto& e:sim800l.traceData.entries)strcpy(e.text,"MUTATED");strcpy(sim800l.traceData.lastFailure.reason,"MUTATED");mutateOnRelease=false;}
  }
  void addSecurityHeaders(){assert(!locked);}
  void writeDownload(JsonObject out,bool*){out["state"]=0;}
  void writeClock(JsonObject out){out["source"]="unsynced";out["utcMs"]=nullptr;out["offsetMinutes"]=480;}
  void sendJson(JsonDocument&,int=200);void sendError(int,const String&);void sendError(int,const char*);
  void sendCameraMonitor();void sendModemTrace();
};
namespace Firmware{constexpr char VERSION[]="1.0";}
bool cameraFrameFreshAtReport(const JsonDocument& doc){return doc["currentFrames"].as<uint32_t>()>0;}
#include "handlers.inc"
JsonDocument result(DashboardServer& d){JsonDocument out;assert(!deserializeJson(out,d.web.output.c_str()));return out;}
void cameraEvent(DashboardServer& d,unsigned n){
 JsonDocument e;e["seq"]=n;e["atMs"]=n*1000;e["boot"]=1;e["incident"]=1;e["type"]="clip_saved";
 e["clip"]=std::string(80,'c')+std::to_string(n); // journal bounds the source field to 80 characters
 e["detail"]=std::string(95,'d');d.cameraLogs.add(0,1,1,clockMs,e.as<JsonObjectConst>());
}
int main(){
 DashboardServer d;liveHealth=&d.responseHealth;
 for(auto& c:d.cameras){c.heartbeat(clockMs,30000);auto t=c.telemetry.to<JsonObject>();t["schema"]=1;
  for(const char* k:{"stream","session","uptimeMs","capturedFrames","currentFrames","currentBytes","frameAgeMs","finalizedThisBoot","deletedThisBoot","normalRetained","lockedRetained","activeLocks","freeBytesAtBoundary","lastFrames","lastDurationMs","lastMaxGapMs","disconnects","lastReason","httpErrors","logOverwritten"})t[k]=uint64_t(123456789);
  t["currentClip"]=std::string(80,'c');t["lastClip"]=std::string(80,'l');t["fault"]=std::string(95,'f');t["clockSource"]="gps_holdover";t["rssi"]=-60;
 }
 for(unsigned i=1;i<=48;i++)cameraEvent(d,i);
 std::set<unsigned> ids;uint32_t before=0;
 for(unsigned page=0;page<6;page++){
  d.web.query.clear();if(before)d.web.query["before"]=String(before);d.web.query["epoch"]="77";
  d.sendCameraMonitor();assert(d.web.status==200&&!d.locked);auto r=result(d);assert(r["logs"].size()==8);
  for(auto e:r["logs"].as<JsonArrayConst>())ids.insert(e["serial"].as<unsigned>());
  before=r["page"]["nextBefore"];assert(r["page"]["more"].as<bool>()==(page<5));
 }
 assert(ids.size()==48&&*ids.begin()==1&&*ids.rbegin()==48);
 std::cout<<"PASS camera handler: all 48 retained records through six <=8-entry pages; peak document="<<d.responseHealth.lastDocumentPeakBytes<<", body="<<d.responseHealth.lastBodyBytes<<", combined budget="<<heapBudget<<"\n";
 // Concurrent arrival cannot move an exclusive cursor backward into duplicate rows.
 cameraEvent(d,49);d.web.query={{"before","41"},{"epoch","77"}};d.sendCameraMonitor();auto r=result(d);assert(r["logs"][0]["serial"]==40);
 d.web.query={{"after","1"},{"epoch","77"}};cameraEvent(d,50);d.sendCameraMonitor();r=result(d);assert(r["page"]["lost"]==true);
 d.web.query={{"after","49"},{"epoch","88"}};d.sendCameraMonitor();r=result(d);assert(r["page"]["reset"]==true&&r["logs"][0]["serial"]==50);
 for(auto& e:d.cameraLogs.rows){memset(e.detail,1,95);e.detail[95]=0;}
 d.web.query.clear();d.sendCameraMonitor();r=result(d);assert(d.web.status==200&&r["logs"][0]["detail"].as<std::string>()==std::string(95,char(1)));
 for(auto& e:d.cameraLogs.rows){memset(e.detail,'d',95);e.detail[95]=0;}
 d.web.query={{"view","summary"}};d.sendCameraMonitor();r=result(d);assert(r["logs"].size()==0&&r["cameras"].size()==2&&r["page"]["omitted"]==true&&!r["page"]["more"].as<bool>());
 const auto summaryPeak=d.responseHealth.lastDocumentPeakBytes,summaryBody=d.responseHealth.lastBodyBytes;
 d.web.query={{"view","events"}};d.sendCameraMonitor();r=result(d);
 assert(d.web.status==200&&r["cameras"].size()==0&&r["logs"].size()==8&&r["page"]["more"]==true);
 std::cout<<"PASS isolated camera reads: summary document="<<summaryPeak<<", body="<<summaryBody<<"; events document="<<d.responseHealth.lastDocumentPeakBytes<<", body="<<d.responseHealth.lastBodyBytes<<"\n";
 d.web.query={{"before","-1"}};d.sendCameraMonitor();assert(d.web.status==400);
 d.web.query={{"before","4294967296"}};d.sendCameraMonitor();assert(d.web.status==400);
 d.web.query.clear();d.mutateOnRelease=true;d.sendCameraMonitor();r=result(d);assert(r["logs"][0]["detail"].as<std::string>()==std::string(95,'d'));
 d.authorized=false;d.sendCameraMonitor();assert(d.web.status==401);d.authorized=true;
 auto& t=d.sim800l.traceData;t.count=48;t.next=0;t.total=70;
 for(size_t i=0;i<48;i++){auto& e=t.entries[i];e.at=uint32_t(i);e.jobId=4;e.direction='<';memset(e.text,'t',159);e.text[159]=0;}
 t.lastFailure.present=true;t.lastFailure.revision=9;t.lastFailure.count=16;
 strcpy(t.lastFailure.stage,"result");strcpy(t.lastFailure.command,"AT+CMGS=\"+639170000001\"");memset(t.lastFailure.reason,'r',159);t.lastFailure.reason[159]=0;
 for(size_t i=0;i<16;i++)t.lastFailure.entries[i]=t.entries[i];
 ids.clear();before=0;
 for(unsigned page=0;page<6;page++){
  d.locked=true;d.web.query.clear();if(before)d.web.query["before"]=String(before);
  d.sendModemTrace();assert(d.web.status==200&&!d.locked);r=result(d);assert(r["entries"].size()==8&&r["lastFailure"]["entries"].size()==0);
  for(auto e:r["entries"].as<JsonArrayConst>())ids.insert(e["serial"].as<unsigned>());before=r["page"]["nextBefore"];
 }
 assert(ids.size()==48&&*ids.begin()==23&&*ids.rbegin()==70);
 assert(d.responseHealth.endpoints[2].documentPeak>0&&d.responseHealth.endpoints[3].documentPeak>0);
 {const auto peak=d.responseHealth.endpoints[2].documentPeak;ResponseMemory::Allocator status(d.responseHealth,ResponseMemory::Endpoint::Status);JsonDocument info(&status);d.responseHealth.write(info.to<JsonObject>());assert(info["endpoints"]["cameras"]["documentPeakBytes"]==peak&&info["endpoints"]["cameras"]["responses"].as<unsigned>()>0);}
 std::cout<<"PASS trace handler: full 48-record ring without allocating TraceSnapshot; peak document="<<d.responseHealth.lastDocumentPeakBytes<<", body="<<d.responseHealth.lastBodyBytes<<"\n";
 d.web.query={{"section","failure"},{"revision","9"}};d.locked=true;d.sendModemTrace();r=result(d);assert(r["lastFailure"]["entries"].size()==8&&r["page"]["more"]==true);
 before=r["page"]["nextBefore"];d.web.query["before"]=String(before);d.locked=true;d.sendModemTrace();r=result(d);assert(r["lastFailure"]["entries"].size()==8&&r["page"]["more"]==false);
 t.lastFailure.revision++;d.locked=true;d.sendModemTrace();r=result(d);assert(r["page"]["reset"]==true);
 d.web.query.clear();d.locked=true;d.mutateOnRelease=true;d.sendModemTrace();r=result(d);assert(r["entries"][0]["text"].as<std::string>()==std::string(159,'t')&&r["lastFailure"]["reason"].as<std::string>()==std::string(159,'r'));
 heapBudget=1;d.locked=true;d.sendModemTrace();assert(d.web.status==503&&d.responseHealth.outputFailures>0&&d.responseHealth.lastFailureStage==2);
 heapBudget=32768;d.locked=true;d.sendModemTrace();assert(d.web.status==200);
 {ResponseMemory::Allocator allocator(d.responseHealth);JsonDocument doc(&allocator);for(unsigned i=0;i<4000;i++)doc["values"].add(std::string(80,'x')+std::to_string(i));assert(doc.overflowed());d.locked=true;d.sendJson(doc);assert(d.web.status==503&&d.responseHealth.documentFailures>0&&d.responseHealth.lastFailedAllocationBytes>0);}
 d.locked=true;d.sendModemTrace();assert(d.web.status==200);
 DiagnosticPaging::Query q;q.after=UINT32_MAX;DiagnosticPaging::Window wrapped(1,1,q,77);assert(wrapped.reset&&wrapped.count==1);
 std::cout<<"PASS summary/auth/strict cursors, insertion/eviction/reboot/rollover, failure-revision isolation, copied strings after mutex release, constrained allocation and recovery\n";
}
