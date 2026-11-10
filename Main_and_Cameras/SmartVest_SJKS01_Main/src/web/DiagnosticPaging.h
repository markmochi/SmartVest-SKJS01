#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Request-local windows over bounded RAM rings. IDs are newest=total, oldest=total-count+1.
// before is exclusive; after keeps incremental polls from returning already saved entries.
namespace DiagnosticPaging {
constexpr size_t PAGE_SIZE=8;
inline bool number(const char* text,uint32_t& value){
  if(!text||!*text)return false;
  uint64_t n=0;
  for(const char* p=text;*p;++p){if(*p<'0'||*p>'9')return false;n=n*10+uint8_t(*p-'0');if(n>UINT32_MAX)return false;}
  value=uint32_t(n);return true;
}
struct Query {
  uint32_t before=0,after=0,epoch=0,revision=0;
  bool hasEpoch=false,hasRevision=false;
  template<class Web> bool read(Web& web){
    for(const char* key:{"before","after","epoch","revision"}){
      if(!web.hasArg(key))continue;
      uint32_t value=0;if(!number(web.arg(key).c_str(),value))return false;
      if(!strcmp(key,"before"))before=value;
      else if(!strcmp(key,"after"))after=value;
      else if(!strcmp(key,"epoch")){epoch=value;hasEpoch=true;}
      else{revision=value;hasRevision=true;}
    }
    return true;
  }
};
struct Window {
  uint32_t total=0,oldest=0,after=0,nextBefore=0;
  size_t retained=0,offset=0,count=0;
  bool more=false,reset=false,lost=false;
  Window(uint32_t t,size_t available,const Query& q,uint32_t epoch,bool changed=false):total(t),retained(available){
    if(retained>total)retained=total;
    oldest=retained?total-uint32_t(retained)+1:0;
    reset=changed||(q.hasEpoch&&q.epoch!=epoch)||q.after>total||(q.before&&uint64_t(q.before)>uint64_t(total)+1);
    after=reset?0:q.after;
    const uint32_t before=reset?0:q.before;
    lost=!reset&&((after&&oldest&&after<oldest-1)||(before&&oldest&&before<=oldest));
    if(before&&before<=total)offset=size_t(total-before)+1;
    while(offset+count<retained&&count<PAGE_SIZE){
      const uint32_t serial=total-uint32_t(offset+count);
      if(serial<=after)break;
      ++count;
    }
    if(count){nextBefore=total-uint32_t(offset+count-1);more=offset+count<retained&&nextBefore>after+uint64_t(1);}
  }
  void write(JsonObject out,uint32_t epoch)const{
    out["epoch"]=epoch;out["total"]=total;out["retained"]=retained;out["oldest"]=oldest;
    out["after"]=after;out["nextBefore"]=more?nextBefore:0;out["more"]=more;out["reset"]=reset;out["lost"]=lost;
  }
};
}
