#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
namespace CameraMonitor {
inline bool text(JsonVariantConst v,size_t max){return v.is<const char*>() && strlen(v.as<const char*>())<=max;}
inline bool validate(JsonObjectConst t,JsonArrayConst events){
  if(t["schema"]!=1 || !t["stream"].is<uint32_t>() || t["stream"].as<uint32_t>()==0 || events.size()>6)return false;
  for(const char* key:{"session","uptimeMs","capturedFrames","currentFrames","currentBytes","frameAgeMs","finalizedThisBoot","deletedThisBoot","normalRetained","lockedRetained","activeLocks","freeBytesAtBoundary","lastFrames","lastDurationMs","lastMaxGapMs","disconnects","lastReason","httpErrors","logOverwritten"})if(!t[key].is<uint64_t>())return false;
  if(!t["rssi"].is<int>() || t["rssi"].as<int>()>0 || t["rssi"].as<int>()< -127)return false;
  if(!text(t["currentClip"],80)||!text(t["lastClip"],80)||!text(t["fault"],95)||!text(t["clockSource"],23))return false;
  if(t["downloadsSupported"]==1){
    if(!t["downloadId"].is<uint32_t>()||!t["downloadState"].is<unsigned>()||t["downloadState"].as<unsigned>()>4||!t["downloadSent"].is<uint64_t>()||!t["downloadSize"].is<uint64_t>()||t["downloadSent"].as<uint64_t>()>t["downloadSize"].as<uint64_t>()||t["downloadSize"].as<uint64_t>()>134217728)return false;
  }
  uint32_t previous=0;
  for(JsonObjectConst e:events){
    if(!e["seq"].is<uint32_t>()||e["seq"].as<uint32_t>()<=previous||!e["atMs"].is<uint64_t>()||!e["boot"].is<uint32_t>()||!e["incident"].is<uint32_t>()||!text(e["type"],23)||!text(e["clip"],80)||!text(e["detail"],95))return false;
    previous=e["seq"];
  }
  return true;
}
inline void copyStatus(JsonObject out,JsonObjectConst in){
  for(const char* key:{"schema","stream","session","uptimeMs","capturedFrames","currentClip","currentFrames","currentBytes","frameAgeMs","finalizedThisBoot","deletedThisBoot","normalRetained","lockedRetained","activeLocks","freeBytesAtBoundary","lastClip","lastFrames","lastDurationMs","lastMaxGapMs","fault","rssi","disconnects","lastReason","httpErrors","logOverwritten","clockSource","filesSupported","downloadsSupported","downloadId","downloadState","downloadSent","downloadSize","totalBytes","latestUtcMs"})out[key]=in[key];
}
struct Record {int role=0;uint32_t stream=0,seq=0,received=0,boot=0,incident=0;uint64_t session=0,at=0;char type[24]{},clip[81]{},detail[96]{};};
class Journal {
 public:
  static constexpr size_t CAPACITY=48;
  Record rows[CAPACITY]{};size_t head=0,count=0;uint32_t overwritten=0,total=0;
  void add(int role,uint32_t stream,uint64_t session,uint32_t received,JsonObjectConst e){
    if(++total==0){head=count=0;overwritten=0;total=1;} // Cursor reset on counter rollover.
    if(count==CAPACITY){head=(head+1)%CAPACITY;--count;++overwritten;}
    auto& r=rows[(head+count)%CAPACITY];r=Record{};r.role=role;r.stream=stream;r.session=session;r.received=received;
    r.seq=e["seq"];r.at=e["atMs"];r.boot=e["boot"];r.incident=e["incident"];
    snprintf(r.type,sizeof(r.type),"%s",e["type"]|"");snprintf(r.clip,sizeof(r.clip),"%s",e["clip"]|"");snprintf(r.detail,sizeof(r.detail),"%s",e["detail"]|"");++count;
  }
  const Record& newest(size_t i)const{return rows[(head+count-1-i)%CAPACITY];}
};
struct Cursor {uint32_t stream=0,seq=0,missing=0;
  bool accept(uint32_t incoming,uint32_t next){
    if(stream!=incoming){stream=incoming;seq=0;missing=0;}
    if(next<=seq)return false;
    if(next>seq+1)missing+=next-seq-1;
    seq=next;return true;
  }
};
}
