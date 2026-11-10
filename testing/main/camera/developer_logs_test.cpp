#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/ClipNames.h"
#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/CameraLog.h"
#include "../../../Main_and_Cameras/SmartVest_SJKS01_Main/src/gps/UtcClock.h"
#include "../../../Main_and_Cameras/SmartVest_SJKS01_Main/src/web/CameraTelemetry.h"
#include <cassert>
#include <cstring>
#include <string>
int main(){
  using namespace UtcClock;
  assert(fromDate(2026,10,2,2,33,34,0)==1790908414000ULL);
  assert(fromDate(2024,2,29,0,0,0,0)==1709164800000ULL);
  assert(fromDate(2026,2,29,0,0,0,0)==0);assert(fromDate(2026,13,1,0,0,0,0)==0);
  Anchor clock;assert(clock.get(0)==0);clock.set(1790908414000ULL,100,2);
  assert(clock.get(1100)==1790908415000ULL);assert(clock.get(99)==0);assert(clock.get(100+HOLD_MS)==0);
  char name[81];ClipNames::make(name,sizeof(name),"front",12,3,1790908414000ULL,480);
  assert(!strcmp(name,"/clips/front_2026-10-02_10-33-34"));assert(ClipNames::validStem(name));
  ClipNames::make(name,sizeof(name),"rear",12,3,0,480);assert(!strcmp(name,"/clips/rear_time-unknown_session-12_clip-3"));
  assert(ClipNames::validStem("/clips/c_1_2"));assert(!ClipNames::validStem("/clips/front_../../x"));assert(!ClipNames::validStem("/else/front_1"));assert(!ClipNames::validStem("/clips/front_x.avi"));
  CameraLogs::Buffer<3> pending;for(int i=0;i<4;++i)pending.add(i,"clip_saved","/clips/c_1_1","saved");
  CameraLogs::Entry entries[3];assert(pending.copy(entries,3)==3);assert(entries[0].seq==2&&entries[2].seq==4&&pending.overwritten==1);
  pending.acknowledge(1);assert(pending.count==3);pending.acknowledge(3);assert(pending.count==1);
  assert(pending.copy(entries,3)==1&&entries[0].seq==4);pending.acknowledge(4);assert(pending.count==0);
  CameraMonitor::Cursor cursor;assert(cursor.accept(1,1));assert(!cursor.accept(1,1));assert(cursor.accept(1,3));assert(cursor.missing==1);assert(cursor.accept(2,1));assert(cursor.missing==0);
  JsonDocument d;auto t=d["telemetry"].to<JsonObject>();t["schema"]=1;t["stream"]=1;
  for(const char* key:{"session","uptimeMs","capturedFrames","currentFrames","currentBytes","frameAgeMs","finalizedThisBoot","deletedThisBoot","normalRetained","lockedRetained","activeLocks","freeBytesAtBoundary","lastFrames","lastDurationMs","lastMaxGapMs","disconnects","lastReason","httpErrors","logOverwritten"})t[key]=uint64_t(123);
  t["rssi"]=-50;t["currentClip"]="/clips/front_2026-10-02_10-33-34";t["lastClip"]="";t["fault"]="";t["clockSource"]="device";
  auto events=d["events"].to<JsonArray>();auto e=events.add<JsonObject>();e["seq"]=1;e["atMs"]=uint64_t(123);e["type"]="clip_saved";e["clip"]="/clips/c_1_1";e["detail"]="saved";e["boot"]=0;e["incident"]=0;
  assert(CameraMonitor::validate(t,events));t["rssi"]=10;assert(!CameraMonitor::validate(t,events));t["rssi"]=-50;
  e["detail"]=std::string(96,'x');assert(!CameraMonitor::validate(t,events));e["detail"]="saved";
  e["seq"]=0;assert(!CameraMonitor::validate(t,events));e["seq"]=1;
  t["session"]=-1;assert(!CameraMonitor::validate(t,events));t["session"]=123;
  CameraMonitor::Journal journal;for(int i=0;i<60;++i)journal.add(0,1,12,i,e);
  assert(journal.count==48 && journal.overwritten==12 && journal.newest(0).received==59 && journal.newest(47).received==12);
  // Longest allowed telemetry strings and six longest log records fit the unchanged 4KB API limit.
  t["currentClip"]=std::string(80,'a');t["lastClip"]=std::string(80,'a');t["fault"]=std::string(95,'a');t["clockSource"]=std::string(23,'a');
  events.clear();for(int i=1;i<=6;++i){auto x=events.add<JsonObject>();x["seq"]=i;x["atMs"]=uint64_t(18446744073709551615ULL);x["type"]=std::string(23,'a');x["clip"]=std::string(80,'a');x["detail"]=std::string(95,'a');x["boot"]=4294967295U;x["incident"]=4294967295U;}
  d["firmware"]="SJKS-camera-0.5.0";d["recording"]=true;d["sdReady"]=true;d["lockSupported"]=true;d["frames"]=4294967295U;d["uptimeMs"]=4294967295U;d["rssi"]=-50;d["disconnects"]=4294967295U;
  assert(CameraMonitor::validate(t,events));assert(measureJson(d)<4096);
}
