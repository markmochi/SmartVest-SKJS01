#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
namespace CameraLogs {
struct Entry {uint32_t seq=0;uint64_t at=0;char type[24]{},clip[81]{},detail[96]{};uint32_t boot=0,incident=0;};
// Caller supplies synchronization. Bounded RAM buffer, never an SD write per log.
template<size_t N> class Buffer {
 public:
  Entry data[N]{};size_t head=0,count=0;uint32_t sequence=0,overwritten=0;
  void add(uint64_t at,const char* type,const char* clip,const char* detail,uint32_t boot=0,uint32_t incident=0){
    if(count==N){head=(head+1)%N;--count;++overwritten;}
    Entry& e=data[(head+count)%N];e=Entry{};e.seq=++sequence;e.at=at;e.boot=boot;e.incident=incident;
    snprintf(e.type,sizeof(e.type),"%s",type?type:"");snprintf(e.clip,sizeof(e.clip),"%s",clip?clip:"");snprintf(e.detail,sizeof(e.detail),"%s",detail?detail:"");++count;
  }
  size_t copy(Entry* out,size_t maximum)const{size_t n=count<maximum?count:maximum;for(size_t i=0;i<n;++i)out[i]=data[(head+i)%N];return n;}
  void acknowledge(uint32_t seq){while(count && data[head].seq<=seq){head=(head+1)%N;--count;}}
};
}
