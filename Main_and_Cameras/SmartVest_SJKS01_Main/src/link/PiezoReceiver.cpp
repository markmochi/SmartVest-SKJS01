#include "SensorLink.h"
#include "../../Config.h"
namespace {
bool supportMatches(const PiezoProtocol::Status& p,unsigned i){
  const auto& b=p.bank[i];const int32_t offset=int32_t(b.supportAt-p.candidateAt);
  return p.candidate&&b.supportHit&&b.supportHit==p.matchedHits[i]&&
    b.supportPeak>=PiezoProtocol::SUPPORT_THRESHOLD&&offset>=-4500&&offset<=1500&&
    !(b.flags&(PiezoProtocol::Clipped|PiezoProtocol::Drift))&&bool(p.matchedBanks&(1u<<i));
}
}
bool SensorLink::piezoOnline() const {
  return labOnline()&&piezoSeen&&uint32_t(millis()-piezoAt)<1000;
}
ImuLabProtocol::Result SensorLink::piezoCommandReady(ImuLabProtocol::Op op) const {
  namespace L=ImuLabProtocol;
  if(!piezoOnline())return L::Result::Offline;
  if(commandPending)return L::Result::Busy;
  if(op==L::Op::PiezoStop)return L::Result::Accepted;
  if(labLive.calibrationResult==1||labLive.state==L::State::Countdown||labLive.state==L::State::Recording||piezoLive.normal.state==PiezoProtocol::Recording||piezoLive.taps.state==PiezoProtocol::Recording)return L::Result::Busy;
  if(!(piezoLive.flags&PiezoProtocol::Healthy)||piezoLive.sampleAgeMs+uint32_t(millis()-piezoAt)>=1000)return L::Result::Offline;
  return L::Result::Accepted;
}
void SensorLink::writePiezoJson(JsonObject out){
  namespace P=PiezoProtocol;const bool online=piezoOnline();
  const uint32_t age=piezoSeen?uint32_t(millis()-piezoAt):UINT32_MAX;
  const bool fresh=online&&(piezoLive.flags&P::Healthy)&&piezoLive.sampleAgeMs+uint64_t(age)<1000;
  out["supported"]=piezoSeen;out["online"]=online;out["fresh"]=fresh;out["ageMs"]=age;
  out["boot"]=labBoot;out["sequence"]=piezoLive.sequence;out["revision"]=piezoLive.revision;out["baselineEpoch"]=piezoLive.epoch;
  out["enabled"]=online&&bool(piezoLive.flags&P::Enabled);out["saved"]=bool(piezoLive.flags&P::Saved);out["settingsFault"]=bool(piezoLive.flags&P::SettingsFault);
  out["supportThreshold"]=P::SUPPORT_THRESHOLD;
  out["sampleAgeMs"]=piezoLive.sampleAgeMs;out["samples"]=piezoLive.samples;out["readFailures"]=piezoLive.failures;out["maxGapUs"]=piezoLive.maxGapUs;
  for(unsigned i=0;i<2;++i){
    const auto& b=piezoLive.bank[i];auto o=out[i?"rear":"front"].to<JsonObject>();
    o["threshold"]=piezoLive.threshold[i];o["ready"]=fresh&&bool(b.flags&P::Ready);o["clipped"]=fresh&&bool(b.flags&P::Clipped);o["drift"]=fresh&&bool(b.flags&P::Drift);
    if(fresh){o["raw"]=b.raw;o["baseline"]=b.baseline;o["rise"]=b.rise;}else{o["raw"]=nullptr;o["baseline"]=nullptr;o["rise"]=nullptr;}
    o["hits"]=b.hits;o["lastPeak"]=b.lastPeak;
    o["supportPeak"]=b.supportPeak;
    o["supportHeadroom"]=fresh&&uint32_t(b.baseline)+P::SUPPORT_THRESHOLD<4095;
    if(b.supportHit)o["supportAgeMs"]=uint64_t(uint32_t(piezoLive.uptime-b.supportAt))+age;else o["supportAgeMs"]=nullptr;
    if(b.lastPeak&&!(b.flags&P::Old))o["lastAgeMs"]=uint64_t(uint32_t(piezoLive.uptime-b.lastAt))+age;else o["lastAgeMs"]=nullptr;
    o["imuMatched"]=fresh&&bool(piezoLive.flags&P::Enabled)&&!(piezoLive.flags&P::SettingsFault)&&supportMatches(piezoLive,i);
  }
  for(unsigned i=0;i<2;++i){const auto& t=i?piezoLive.taps:piezoLive.normal;auto o=out[i?"taps":"normal"].to<JsonObject>();
    o["id"]=t.id;o["epoch"]=t.epoch;o["state"]=t.state;o["elapsedMs"]=t.elapsedMs;o["gaps"]=t.gaps;o["clipped"]=t.clipped;
    for(unsigned b=0;b<2;++b){auto v=o[b?"rear":"front"].to<JsonObject>();v["maxRise"]=t.maxRise[b];v["weakestTap"]=t.minPeak[b];v["taps"]=t.hits[b];v["captureFloor"]=t.floor[b];v["baseline"]=t.baseline[b];}
  }
  auto c=out["command"].to<JsonObject>();c["token"]=labCommand.token;c["state"]=commandState;c["result"]=commandResult;
}

void SensorLink::reportIncidentImpact(){
  namespace P=PiezoProtocol;
  if(!piezoOnline()||!piezoLive.candidate||!(piezoLive.flags&P::Enabled)||
     !(piezoLive.flags&P::Healthy)||(piezoLive.flags&P::SettingsFault)||
     piezoLive.sampleAgeMs+uint64_t(uint32_t(millis()-piezoAt))>=1000)return;
  uint8_t banks=0;
  for(unsigned i=0;i<2;++i)if(supportMatches(piezoLive,i))banks|=uint8_t(1u<<i);
  coordinator.observeImpact(labBoot,piezoLive.candidateAt,banks);
}
