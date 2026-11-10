#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include "HttpMemoryBudget.h"

namespace ResponseMemory {
class Allocator;
enum class Endpoint:uint8_t{Unknown=0,Status=1,Imu=2,Cameras=3,Trace=4,Rider=5};
// Diagnostic accounting contains no request bodies, recipients, credentials or coordinates.
struct Health {
  struct Metric{size_t documentPeak=0,failedAllocation=0,bodyBytes=0;uint32_t freeBefore=0,largestBefore=0;uint8_t stage=0;uint32_t responses=0;};
  Metric endpoints[5];Allocator* active=nullptr;
  uint32_t responses=0,documentFailures=0,outputFailures=0,lengthFailures=0;
  size_t lastBodyBytes=0,maxBodyBytes=0,lastDocumentPeakBytes=0,lastFailedAllocationBytes=0;
  uint32_t freeBeforeEncode=0,largestBeforeEncode=0;
  uint8_t lastFailureStage=0; // 0 none; 1 document; 2 output reserve; 3 serialization length
  void write(JsonObject out)const{
    out["responses"]=responses;out["documentFailures"]=documentFailures;out["outputFailures"]=outputFailures;
    out["lengthFailures"]=lengthFailures;out["lastBodyBytes"]=lastBodyBytes;out["maxBodyBytes"]=maxBodyBytes;
    out["lastDocumentPeakBytes"]=lastDocumentPeakBytes;out["lastFailedAllocationBytes"]=lastFailedAllocationBytes;
    out["freeBeforeEncode"]=freeBeforeEncode;out["largestBeforeEncode"]=largestBeforeEncode;out["lastFailureStage"]=lastFailureStage;
    auto routes=out["endpoints"].to<JsonObject>();
    const char* names[]={"status","imu","cameras","trace","rider"};
    for(size_t i=0;i<5;++i){const auto& m=endpoints[i];auto row=routes[names[i]].to<JsonObject>();
      row["documentPeakBytes"]=m.documentPeak;row["failedAllocationBytes"]=m.failedAllocation;row["bodyBytes"]=m.bodyBytes;
      row["freeBeforeEncode"]=m.freeBefore;row["largestBeforeEncode"]=m.largestBefore;row["failureStage"]=m.stage;
      row["responses"]=m.responses;
    }
  }
};
// Use only on the large read-only diagnostics. The tree owns all copied strings and
// is freed before network output. A prefix accounts reallocations without a second heap map.
class Allocator final:public ArduinoJson::Allocator {
 public:
  explicit Allocator(Health& h,Endpoint e=Endpoint::Unknown):health(h),endpoint(e),previous(h.active){health.active=this;health.lastDocumentPeakBytes=0;health.lastFailedAllocationBytes=0;}
  ~Allocator(){if(health.active==this)health.active=previous;}
  void finish(size_t body,uint8_t stage,uint32_t freeHeap,uint32_t largest){
    if(endpoint==Endpoint::Unknown)return;
    auto& m=health.endpoints[uint8_t(endpoint)-1];const uint32_t count=m.responses+1;m={peak,health.lastFailedAllocationBytes,body,freeHeap,largest,stage,count};
  }
  void* allocate(size_t n)override{
    if(n>LIMIT-used||!HttpMemoryBudget::available(sizeof(Header)+n)){health.lastFailedAllocationBytes=n;return nullptr;}
    auto* h=static_cast<Header*>(malloc(sizeof(Header)+n));
    if(!h){health.lastFailedAllocationBytes=n;return nullptr;}
    h->size=n;used+=n;record();return h+1;
  }
  void deallocate(void* p)override{if(!p)return;auto* h=static_cast<Header*>(p)-1;used-=h->size;free(h);}
  void* reallocate(void* p,size_t n)override{
    if(!p)return allocate(n);
    if(!n){deallocate(p);return nullptr;}
    auto* old=static_cast<Header*>(p)-1;const size_t previous=old->size;
    if(n>LIMIT-(used-previous)||(n>previous&&!HttpMemoryBudget::available(sizeof(Header)+n))){health.lastFailedAllocationBytes=n;return nullptr;}
    auto* h=static_cast<Header*>(realloc(old,sizeof(Header)+n));
    if(!h){health.lastFailedAllocationBytes=n;return nullptr;}
    h->size=n;used=used-previous+n;record();return h+1;
  }
  static constexpr size_t LIMIT=24*1024;
 private:
  struct alignas(max_align_t) Header{size_t size;};
  Health& health;Endpoint endpoint;Allocator* previous;size_t used=0,peak=0;
  void record(){if(used>peak)peak=used;health.lastDocumentPeakBytes=peak;}
};
}
