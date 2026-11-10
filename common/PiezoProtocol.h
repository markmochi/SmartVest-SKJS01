#pragma once
#include "ImuLabProtocol.h"
namespace PiezoProtocol {
constexpr uint16_t MIN_THRESHOLD=50, MAX_THRESHOLD=800, DEFAULT_THRESHOLD=100, SUPPORT_THRESHOLD=800;
inline bool validThreshold(uint16_t value){return value>=MIN_THRESHOLD&&value<=MAX_THRESHOLD;}
enum BankFlag : uint8_t { Ready=1, Capturing=2, Clipped=4, Armed=8, Drift=16, Old=32 };
enum StatusFlag : uint8_t { Enabled=1, Saved=2, SettingsFault=4, Healthy=8 };
enum TrialState : uint8_t { Empty, Recording, Complete, Interrupted };
struct Bank {
  uint16_t raw=0,baseline=0,rise=0,lastPeak=0;
  uint32_t hits=0,lastAt=0;
  uint8_t flags=0;
  // Retain strong evidence separately so a later small hit cannot replace it.
  uint16_t supportPeak=0;
  uint32_t supportAt=0,supportHit=0;
};
struct Trial {
  uint32_t id=0,epoch=0;
  uint8_t state=Empty,seconds=15;
  uint16_t elapsedMs=0,maxRise[2]={},minPeak[2]={},hits[2]={},gaps=0,clipped=0,floor[2]={},baseline[2]={};
};
struct Status {
  uint32_t sequence=0,uptime=0,revision=1,epoch=0,samples=0,failures=0,maxGapUs=0;
  uint8_t flags=0;
  uint16_t threshold[2]={DEFAULT_THRESHOLD,DEFAULT_THRESHOLD},sampleAgeMs=65535;
  Bank bank[2];Trial normal,taps;
  uint32_t candidate=0,candidateAt=0,matchedHits[2]={};
  uint8_t matchedBanks=0;
};
constexpr uint8_t BYTES=180;
static_assert(BYTES<=ImuLabProtocol::MAX_DATA,"Piezo status must fit the diagnostic frame");
inline void encode(const Status& s,ImuLabProtocol::Frame& f) {
  namespace L=ImuLabProtocol;f.type=L::Type::Piezo;f.length=BYTES;
  std::memset(f.data,0,BYTES);auto* p=f.data;*p++=2;*p++=s.flags;
  for(uint32_t v:{s.sequence,s.uptime,s.revision,s.epoch,s.samples,s.failures,s.maxGapUs}){L::put32(p,v);p+=4;}
  for(auto v:s.threshold){L::put16(p,v);p+=2;}L::put16(p,s.sampleAgeMs);p+=2;
  for(const auto& b:s.bank){for(uint16_t v:{b.raw,b.baseline,b.rise,b.lastPeak}){L::put16(p,v);p+=2;}L::put32(p,b.hits);p+=4;L::put32(p,b.lastAt);p+=4;*p++=b.flags;}
  for(const auto* t:{&s.normal,&s.taps}){
    L::put32(p,t->id);p+=4;L::put32(p,t->epoch);p+=4;*p++=t->state;*p++=t->seconds;
    for(uint16_t v:{t->elapsedMs,t->maxRise[0],t->maxRise[1],t->minPeak[0],t->minPeak[1],t->hits[0],t->hits[1],t->gaps,t->clipped,t->floor[0],t->floor[1],t->baseline[0],t->baseline[1]}){L::put16(p,v);p+=2;}
  }
  L::put32(p,s.candidate);p+=4;L::put32(p,s.candidateAt);p+=4;*p++=s.matchedBanks;
  for(auto v:s.matchedHits){L::put32(p,v);p+=4;}
  *p++=0;
  for(const auto& b:s.bank){L::put16(p,b.supportPeak);p+=2;L::put32(p,b.supportAt);p+=4;L::put32(p,b.supportHit);p+=4;}
}
inline bool decode(const ImuLabProtocol::Frame& f,Status& s) {
  namespace L=ImuLabProtocol;
  if(f.type!=L::Type::Piezo||f.length!=BYTES||f.data[0]!=2||f.data[159]||f.data[1]>15)return false;
  const auto* p=f.data+2;s.flags=f.data[1];
  for(auto* v:{&s.sequence,&s.uptime,&s.revision,&s.epoch,&s.samples,&s.failures,&s.maxGapUs}){*v=L::get32(p);p+=4;}
  for(auto& v:s.threshold){v=L::get16(p);p+=2;if(!validThreshold(v))return false;}
  s.sampleAgeMs=L::get16(p);p+=2;
  for(auto& b:s.bank){for(auto* v:{&b.raw,&b.baseline,&b.rise,&b.lastPeak}){*v=L::get16(p);p+=2;if(*v>4095)return false;}b.hits=L::get32(p);p+=4;b.lastAt=L::get32(p);p+=4;b.flags=*p++;if(b.flags>63)return false;}
  for(auto* t:{&s.normal,&s.taps}){
    t->id=L::get32(p);p+=4;t->epoch=L::get32(p);p+=4;t->state=*p++;t->seconds=*p++;
    for(auto* v:{&t->elapsedMs,&t->maxRise[0],&t->maxRise[1],&t->minPeak[0],&t->minPeak[1],&t->hits[0],&t->hits[1],&t->gaps,&t->clipped,&t->floor[0],&t->floor[1],&t->baseline[0],&t->baseline[1]}){*v=L::get16(p);p+=2;}
    if(t->state>Interrupted||t->seconds!=15||t->elapsedMs>15000)return false;
    for(unsigned i=0;i<2;++i)if(t->maxRise[i]>4095||t->minPeak[i]>4095||t->floor[i]>4095||t->baseline[i]>4095)return false;
  }
  s.candidate=L::get32(p);p+=4;s.candidateAt=L::get32(p);p+=4;s.matchedBanks=*p++;
  for(auto& v:s.matchedHits){v=L::get32(p);p+=4;}
  ++p;
  for(auto& b:s.bank){b.supportPeak=L::get16(p);p+=2;b.supportAt=L::get32(p);p+=4;b.supportHit=L::get32(p);p+=4;
    if(b.supportPeak>4094||(b.supportPeak&&b.supportPeak<SUPPORT_THRESHOLD)||bool(b.supportPeak)!=bool(b.supportHit)||b.supportHit>b.hits)return false;
  }
  return s.sequence && s.revision && s.matchedBanks<4;
}
}
