#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include "CrashProtocol.h"

// BMI323 diagnostic lab V2 shares the UART with unchanged incident V1. Explicit little-endian
// serialization; each frame has CRC16. Removed recorder slots remain reserved for compatibility.
namespace ImuLabProtocol {
constexpr size_t MAX_FRAME = 224, MAX_DATA = MAX_FRAME - 22;
// Hello capability: an older Main sends token zero and cannot activate tests.
constexpr uint32_t AUTOMATIC_CRASH_HELLO_TOKEN = 0x42544d49u;
constexpr uint16_t LEGACY_SAMPLE_LIMIT = 2048;
constexpr uint8_t SAMPLE_BYTES = 48;
enum class Type : uint8_t { Live = 1, Metadata, Samples, Response, Command, Hello, Battery, Piezo };
enum class Op : uint8_t { Retry = 1, Gyro, Upright, Forward, Neutral, ClearBody, Start, Stop, ReadSamples, ResetCandidate, PiezoBaseline, PiezoThresholds, PiezoNormal, PiezoTaps, PiezoStop };
enum class Result : uint8_t { Accepted, Busy, Offline, Prerequisite, Range, WrongBoot, Unknown, TrialChanged, Storage };
enum class State : uint8_t { Empty, Countdown, Recording, Complete, Interrupted };
enum class Finish : uint8_t { None, Duration, UserStop, SensorLost, BufferFull };
enum SampleFlag : uint16_t { AccelClip = 1, GyroClip = 2, AttitudeValid = 4, TimingGap = 8, PiezoValid = 16, GyroValid = 32, TemperatureValid = 64, TemperatureFresh = 128, SensorDeltaValid = 256 };
enum CalibrationFlag : uint8_t { GyroCalibrated = 1, BodyCalibrated = 2, NeutralSet = 4, UprightSet = 8 };
struct Frame {
  Type type = Type::Live;
  uint32_t token = 0, boot = 0, session = 0;
  uint8_t length = 0;
  uint8_t data[MAX_DATA] = {};
};
struct Sample {
  uint32_t timeUs = 0, sequence = 0;
  int16_t accel[3] = {}, gyro[3] = {}, temperature = 0;
  int16_t sideCdeg = 0, forwardCdeg = 0;
  uint16_t front = 0, rear = 0, flags = 0;
  uint32_t sensorTime = 0, deltaUs = 0, candidateId = 0;
  uint16_t detectorFlags = 0;
  uint8_t continuity = 0, reserved = 0;
};
static_assert(sizeof(Sample) == SAMPLE_BYTES, "Review live sample wire layout");
struct Metadata {
  uint32_t id = 0, startUs = 0, elapsedUs = 0, crc = 0;
  uint16_t count = 0, seconds = 0;
  uint8_t flags = 0;
  uint16_t who = 0;
  uint32_t revision = 0;
  Finish finish = Finish::None;
  float gravity = 1, lean = 0, bias[3] = {}, right[3] = {1,0,0};
  float forward[3] = {0,1,0}, up[3] = {0,0,1}, neutral[2] = {};
  uint32_t errors = 0, gaps = 0, maxGapUs = 0, clipped = 0, overruns = 0;
  char label[32] = {}, firmware[16] = "1.0";
};
struct Live {
  bool healthy = false, automaticDetectionEnabled = false, automaticReady = false;
  uint8_t error = 0, phase = 0, reg = 0, calibrationFlags = 0;
  uint16_t who = 0, observed = 0, expected = 0;
  uint32_t samples = 0, failures = 0, overruns = 0, intervalUs = 0;
  float rateHz = 0, side = 0, forward = 0, trust = 0, bias[3] = {};
  Sample sample;
  uint8_t calibrationOp = 0, calibrationResult = 0;
  uint16_t calibrationCount = 0;
  State state = State::Empty;
  Finish finish = Finish::None;
  uint32_t trialId = 0, elapsedUs = 0;
  uint16_t trialCount = 0, trialSeconds = 0;
  uint32_t gaps = 0, clipped = 0, maxGapUs = 0;
  uint32_t candidateCount = 0, rotationHoldUs = 0, tiltHoldUs = 0, revision = 0;
  uint32_t rejected = 0, duplicates = 0, notReady = 0, cycleUs = 0;
  uint8_t detectorState = 0, continuity = 0;
  float postureChange = 0, activity = 0;
  uint32_t eventStartUs=0,decisionUs=0,eventRotationUs=0,eventTiltUs=0;float eventPeakG=0,eventPeakDps=0,eventChange=0;
};
inline void put16(uint8_t* p, uint16_t v) { CrashProtocol::put16(p,v); }
inline void put32(uint8_t* p, uint32_t v) { CrashProtocol::put32(p,v); }
inline uint16_t get16(const uint8_t* p) { return CrashProtocol::get16(p); }
inline uint32_t get32(const uint8_t* p) { return CrashProtocol::get32(p); }
inline void putFloat(uint8_t* p, float v) { uint32_t bits; std::memcpy(&bits,&v,4); put32(p,bits); }
inline float getFloat(const uint8_t* p) { uint32_t bits=get32(p); float v; std::memcpy(&v,&bits,4); return v; }
inline void encodeSample(const Sample& s,uint8_t* p) {
  put32(p,s.timeUs); put32(p+4,s.sequence);
  for(size_t i=0;i<3;++i) { put16(p+8+i*2,uint16_t(s.accel[i])); put16(p+14+i*2,uint16_t(s.gyro[i])); }
  put16(p+20,uint16_t(s.temperature)); put16(p+22,uint16_t(s.sideCdeg)); put16(p+24,uint16_t(s.forwardCdeg));
  put16(p+26,s.front); put16(p+28,s.rear); put16(p+30,s.flags);
  put32(p+32,s.sensorTime);put32(p+36,s.deltaUs);put32(p+40,s.candidateId);
  put16(p+44,s.detectorFlags);p[46]=s.continuity;p[47]=0;
}
inline Sample decodeSample(const uint8_t* p) {
  Sample s; s.timeUs=get32(p); s.sequence=get32(p+4);
  for(size_t i=0;i<3;++i) { s.accel[i]=int16_t(get16(p+8+i*2)); s.gyro[i]=int16_t(get16(p+14+i*2)); }
  s.temperature=int16_t(get16(p+20)); s.sideCdeg=int16_t(get16(p+22)); s.forwardCdeg=int16_t(get16(p+24));
  s.front=get16(p+26);s.rear=get16(p+28);s.flags=get16(p+30);s.sensorTime=get32(p+32);s.deltaUs=get32(p+36);s.candidateId=get32(p+40);s.detectorFlags=get16(p+44);s.continuity=p[46];s.reserved=p[47];return s;
}
constexpr uint8_t META_BYTES = 168;
inline void encodeMetadata(const Metadata& m,Frame& f) {
  f.type=Type::Metadata; f.length=META_BYTES; auto* p=f.data;
  put32(p,m.id);put16(p+4,m.count);put16(p+6,m.seconds);put32(p+8,m.startUs);put32(p+12,m.elapsedUs);put32(p+16,m.crc);
  p[20]=m.flags;put16(p+21,m.who);p[23]=uint8_t(m.finish);
  putFloat(p+24,m.gravity);putFloat(p+28,m.lean);
  for(size_t i=0;i<3;++i) {putFloat(p+32+i*4,m.bias[i]);putFloat(p+44+i*4,m.right[i]);putFloat(p+56+i*4,m.forward[i]);putFloat(p+68+i*4,m.up[i]);}
  putFloat(p+80,m.neutral[0]);putFloat(p+84,m.neutral[1]);
  put32(p+88,m.errors);put32(p+92,m.gaps);put32(p+96,m.maxGapUs);put32(p+100,m.clipped);put32(p+104,m.overruns);
  std::memcpy(p+108,m.label,32);std::memcpy(p+140,m.firmware,16);
  put16(p+156,0x70B9);put16(p+158,0x70C9);p[160]=18;p[161]=19;p[162]=23;p[163]=21;put32(p+164,m.revision);
}
inline bool decodeMetadata(const Frame& f,Metadata& m) {
  if(f.length!=META_BYTES || get16(f.data+156)!=0x70B9 || get16(f.data+158)!=0x70C9 || f.data[160]!=18 || f.data[161]!=19 || f.data[162]!=23 || f.data[163]!=21)return false;
  const auto* p=f.data;
  m.id=get32(p);m.count=get16(p+4);m.seconds=get16(p+6);m.startUs=get32(p+8);m.elapsedUs=get32(p+12);m.crc=get32(p+16);
  m.revision=get32(p+164);m.flags=p[20];m.who=get16(p+21);m.finish=Finish(p[23]);m.gravity=getFloat(p+24);m.lean=getFloat(p+28);
  for(size_t i=0;i<3;++i){m.bias[i]=getFloat(p+32+i*4);m.right[i]=getFloat(p+44+i*4);m.forward[i]=getFloat(p+56+i*4);m.up[i]=getFloat(p+68+i*4);}
  m.neutral[0]=getFloat(p+80);m.neutral[1]=getFloat(p+84);m.errors=get32(p+88);m.gaps=get32(p+92);m.maxGapUs=get32(p+96);m.clipped=get32(p+100);m.overruns=get32(p+104);
  std::memcpy(m.label,p+108,32);m.label[31]=0;std::memcpy(m.firmware,p+140,16);m.firmware[15]=0;
  for(unsigned offset=24;offset<=84;offset+=4)if(!std::isfinite(getFloat(p+offset)))return false;
  if(m.flags>15 || m.gravity<=.75f || m.gravity>=1.25f)return false;
  return m.count<=LEGACY_SAMPLE_LIMIT && uint8_t(m.finish)<=uint8_t(Finish::BufferFull);
}
constexpr uint8_t LIVE_BYTES = 202;
inline void encodeLive(const Live& v,Frame& f) {
  f.type=Type::Live;f.length=LIVE_BYTES;auto* p=f.data;
  p[0]=v.healthy;p[1]=v.error;p[2]=v.phase;p[3]=v.calibrationFlags;
  put16(p+4,v.who);p[6]=v.reg;p[7]=0;put16(p+8,v.observed);put16(p+10,v.expected);
  put32(p+12,v.samples);put32(p+16,v.failures);put32(p+20,v.overruns);put32(p+24,v.intervalUs);putFloat(p+28,v.rateHz);
  encodeSample(v.sample,p+32);putFloat(p+80,v.side);putFloat(p+84,v.forward);putFloat(p+88,v.trust);
  for(size_t i=0;i<3;++i)putFloat(p+92+i*4,v.bias[i]);
  p[104]=v.calibrationOp;p[105]=v.calibrationResult;put16(p+106,v.calibrationCount);p[108]=uint8_t(v.state);p[109]=uint8_t(v.finish);
  put32(p+110,v.trialId);put16(p+114,v.trialCount);put16(p+116,v.trialSeconds);put32(p+118,v.elapsedUs);
  put32(p+122,v.gaps);put32(p+126,v.clipped);put32(p+130,v.candidateCount);p[134]=v.detectorState;p[135]=v.continuity;
  put32(p+136,v.rotationHoldUs);put32(p+140,v.tiltHoldUs);putFloat(p+144,v.postureChange);putFloat(p+148,v.activity);
  put32(p+152,v.revision);put32(p+156,v.rejected);put32(p+160,v.duplicates);put32(p+164,v.notReady);put32(p+168,v.cycleUs);p[172]=v.automaticDetectionEnabled;p[173]=v.automaticReady;
  put32(p+174,v.eventStartUs);put32(p+178,v.decisionUs);put32(p+182,v.eventRotationUs);put32(p+186,v.eventTiltUs);putFloat(p+190,v.eventPeakG);putFloat(p+194,v.eventPeakDps);putFloat(p+198,v.eventChange);
}
inline bool decodeLive(const Frame& f,Live& v) {
  if(f.length!=LIVE_BYTES || f.data[7] || f.data[172]>1 || f.data[173]>1)return false;
  const auto* p=f.data;v.healthy=p[0];v.error=p[1];v.phase=p[2];v.calibrationFlags=p[3];
  v.who=get16(p+4);v.reg=p[6];v.observed=get16(p+8);v.expected=get16(p+10);
  v.samples=get32(p+12);v.failures=get32(p+16);v.overruns=get32(p+20);v.intervalUs=get32(p+24);v.rateHz=getFloat(p+28);
  v.sample=decodeSample(p+32);v.side=getFloat(p+80);v.forward=getFloat(p+84);v.trust=getFloat(p+88);
  for(size_t i=0;i<3;++i)v.bias[i]=getFloat(p+92+i*4);
  v.calibrationOp=p[104];v.calibrationResult=p[105];v.calibrationCount=get16(p+106);v.state=State(p[108]);v.finish=Finish(p[109]);
  v.trialId=get32(p+110);v.trialCount=get16(p+114);v.trialSeconds=get16(p+116);v.elapsedUs=get32(p+118);
  v.gaps=get32(p+122);v.clipped=get32(p+126);v.candidateCount=get32(p+130);v.detectorState=p[134];v.continuity=p[135];
  v.rotationHoldUs=get32(p+136);v.tiltHoldUs=get32(p+140);v.postureChange=getFloat(p+144);v.activity=getFloat(p+148);
  v.revision=get32(p+152);v.rejected=get32(p+156);v.duplicates=get32(p+160);v.notReady=get32(p+164);v.cycleUs=get32(p+168);
  v.automaticDetectionEnabled=p[172];v.automaticReady=p[173];v.eventStartUs=get32(p+174);v.decisionUs=get32(p+178);v.eventRotationUs=get32(p+182);v.eventTiltUs=get32(p+186);v.eventPeakG=getFloat(p+190);v.eventPeakDps=getFloat(p+194);v.eventChange=getFloat(p+198);
  for(float value:{v.rateHz,v.side,v.forward,v.trust,v.bias[0],v.bias[1],v.bias[2],v.postureChange,v.activity,v.eventPeakG,v.eventPeakDps,v.eventChange})if(!std::isfinite(value))return false;
  if(p[0]>1||v.calibrationFlags>15||v.detectorState>5||v.continuity>7||v.sample.flags>511||v.sample.detectorFlags>1023||v.sample.sensorTime>0x3fffffff||v.sample.continuity>7)return false;
  return uint8_t(v.state)<=uint8_t(State::Interrupted) && v.trialCount<=LEGACY_SAMPLE_LIMIT && !v.sample.reserved;
}
inline size_t encode(const Frame& f,uint8_t* p) {
  if(f.length>MAX_DATA)return 0;
  const size_t size=f.length+22;
  std::memset(p,0,size);p[0]='S';p[1]='L';p[2]=2;p[3]=uint8_t(f.type);p[4]=uint8_t(size);
  put32(p+8,f.token);put32(p+12,f.boot);put32(p+16,f.session);std::memcpy(p+20,f.data,f.length);
  put16(p+size-2,CrashProtocol::crc16(p,size-2));return size;
}
inline bool decode(const uint8_t* p,size_t size,Frame& f) {
  if(size<22 || size>MAX_FRAME || p[0]!='S' || p[1]!='L' || p[2]!=2 || p[4]!=size || p[5] || p[6] || p[7] || p[3]<1 || p[3]>8 || get16(p+size-2)!=CrashProtocol::crc16(p,size-2))return false;
  f.type=Type(p[3]);f.token=get32(p+8);f.boot=get32(p+12);f.session=get32(p+16);f.length=uint8_t(size-22);std::memcpy(f.data,p+20,f.length);return f.boot!=0 && f.session!=0;
}
// One parser owns frame boundaries. Binary lab payload cannot be independently
// misread as an incident by a second parser listening to the same stream.
class Parser {
 public:
  uint32_t rejected=0;
  uint8_t feed(uint8_t byte,uint32_t now,CrashProtocol::Frame& incident,Frame& lab) {
    if(used && uint32_t(now-lastByte)>=100)used=0;
    lastByte=now;bytes[used++]=byte;
    align();
    if(used<8)return 0;
    const size_t size=bytes[1]=='V'?CrashProtocol::FRAME_SIZE:bytes[4];
    const bool legacy=bytes[1]=='V';
    // Discard a bounded lab envelope in its entirety, including unknown versions.
    // Never rescan arbitrary lab payload for an embedded incident.
    const bool lengthOk=legacy || (size>=18 && size<=MAX_FRAME);
    if(!lengthOk){++rejected;used=0;return 0;}
    const bool header=!bytes[6] && !bytes[7] &&
      (legacy ? bytes[2]==1 && bytes[3]>=1 && bytes[3]<=5 && !(bytes[4]&~uint8_t(15)) && bytes[5]==CrashProtocol::FRAME_SIZE
              : bytes[2]==2 && bytes[3]>=1 && bytes[3]<=8 && bytes[5]==0 && size>=22);
    if(used<size)return 0;
    if(header && (legacy?CrashProtocol::decode(bytes,incident):decode(bytes,size,lab))){used=0;return legacy?1:2;}
    // A corrupt, well-framed lab packet is discarded as a whole. Never promote
    // its binary payload to an incident. Incident/command retry handles loss.
    ++rejected;used=0;return 0;
  }
 private:
  uint8_t bytes[MAX_FRAME]={};size_t used=0;uint32_t lastByte=0;
  void shift(){std::memmove(bytes,bytes+1,--used);}
  void align(){while(used && (bytes[0]!='S' || (used>1 && bytes[1]!='V' && bytes[1]!='L')))shift();}
};
} // namespace ImuLabProtocol
