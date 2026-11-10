#pragma once
#include <cstring>
#include <initializer_list>

#include "../link/CrashProtocol.h"

// Fixed, explicitly encoded journal. One NVS blob commits the entire incident
// snapshot atomically; no compiler padding or pointers are persisted.
namespace Incidents {
constexpr size_t LIMIT = 4, CONTACTS = 5, TOMBSTONES = 16;
enum Outcome : uint8_t {
  Waiting,
  Sending,
  Accepted,
  Unknown,
  Failed,
  Cancelled,
  Delivered,
  Undelivered
};
struct Recipient {
  char phone[14] = {};
  char name[41] = {};
  uint8_t outcome = Waiting;
  uint32_t jobId = 0;
};
// Frozen at acceptance. Zero means unknown in migrated legacy records.
// Presence: 1 not detected, 2 detected. GPS: 3 current fix, 4 last known fix.
struct Evidence {
  uint64_t utcMs = 0;
  int32_t latitudeE7 = 0, longitudeE7 = 0;
  uint32_t locationAgeMs = 0;
  uint16_t accelerationMg = 0, rotationDps = 0, voltageMv = 0;
  uint8_t captured = 0, timeSource = 0, timeEstimated = 0, motion = 0;
  uint8_t gps = 0, modem = 0, mobileService = 0, camera[2] = {};
  uint8_t battery = 0, piezo = 0, impactKnown = 0, impactBanks = 0;
  int8_t batteryPercent = -1;
};
struct Incident {
  uint32_t boot = 0, sequence = 0, detectedUptime = 0;
  uint32_t sensorUptime = 0, ageAtReceipt = 0;
  bool ageKnown = false;
  char message[161] = {};
  uint8_t count = 0,
          camera[2] = {};  // 0 pending, 1 received, 2 protected, 3 failed
  bool test = false;
  Evidence evidence;
  Recipient recipients[CONTACTS];
};
struct Journal {
  Incident entries[LIMIT];
  uint32_t retiredBoot[TOMBSTONES] = {}, retiredSequence[TOMBSTONES] = {};
  uint8_t retiredNext = 0;
};
constexpr size_t RECORD_SIZE = 21 + 161 + 4 + CONTACTS * 19;
constexpr size_t LEGACY_ENCODED_SIZE = 5 + LIMIT * RECORD_SIZE + TOMBSTONES * 8 + 2;
constexpr size_t EVIDENCE_BYTES = 42 + CONTACTS * 41;
constexpr size_t ENCODED_SIZE = LEGACY_ENCODED_SIZE + LIMIT * EVIDENCE_BYTES;
inline void encode(const Journal& j, uint8_t* out) {
  std::memset(out, 0, ENCODED_SIZE);
  out[0] = 'I';
  out[1] = 'J';
  out[2] = 2;
  out[3] = LIMIT;
  out[4] = j.retiredNext;
  size_t n = 5;
  for (const auto& e : j.entries) {
    CrashProtocol::put32(out + n, e.boot);
    n += 4;
    CrashProtocol::put32(out + n, e.sequence);
    n += 4;
    CrashProtocol::put32(out + n, e.detectedUptime);
    n += 4;
    CrashProtocol::put32(out + n, e.sensorUptime);
    n += 4;
    CrashProtocol::put32(out + n, e.ageAtReceipt);
    n += 4;
    out[n++] = e.ageKnown;
    std::memcpy(out + n, e.message, 161);
    n += 161;
    out[n++] = e.count;
    out[n++] = e.camera[0];
    out[n++] = e.camera[1];
    out[n++] = e.test;
    for (const auto& r : e.recipients) {
      std::memcpy(out + n, r.phone, 14);
      n += 14;
      out[n++] = r.outcome;
      CrashProtocol::put32(out + n, r.jobId);
      n += 4;
    }
  }
  for (size_t i = 0; i < TOMBSTONES; ++i) {
    CrashProtocol::put32(out + n, j.retiredBoot[i]);
    n += 4;
    CrashProtocol::put32(out + n, j.retiredSequence[i]);
    n += 4;
  }
  for (const auto& e : j.entries) {
    const auto& v=e.evidence;
    CrashProtocol::put32(out+n,uint32_t(v.utcMs));n+=4;
    CrashProtocol::put32(out+n,uint32_t(v.utcMs>>32));n+=4;
    CrashProtocol::put32(out+n,uint32_t(v.latitudeE7));n+=4;
    CrashProtocol::put32(out+n,uint32_t(v.longitudeE7));n+=4;
    CrashProtocol::put32(out+n,v.locationAgeMs);n+=4;
    for(uint16_t value:{v.accelerationMg,v.rotationDps,v.voltageMv}){CrashProtocol::put16(out+n,value);n+=2;}
    for(uint8_t value:{v.captured,v.timeSource,v.timeEstimated,v.motion,v.gps,v.modem,v.mobileService,v.camera[0],v.camera[1],v.battery,v.piezo,v.impactKnown,v.impactBanks,uint8_t(v.batteryPercent)})out[n++]=value;
    out[n++]=0;out[n++]=0;
    for(const auto& r:e.recipients){std::memcpy(out+n,r.name,41);n+=41;}
  }
  CrashProtocol::put16(out + n, CrashProtocol::crc16(out, n));
}
inline bool decode(const uint8_t* in, Journal& output, size_t size=ENCODED_SIZE) {
  const bool legacy=size==LEGACY_ENCODED_SIZE;
  if ((!legacy && size!=ENCODED_SIZE) || in[0] != 'I' || in[1] != 'J' || in[2] != (legacy?1:2) || in[3] != LIMIT ||
      in[4] >= TOMBSTONES ||
      CrashProtocol::get16(in + size - 2) !=
          CrashProtocol::crc16(in, size - 2))
    return false;
  Journal j;
  j.retiredNext = in[4];
  size_t n = 5;
  for (auto& e : j.entries) {
    e.boot = CrashProtocol::get32(in + n);
    n += 4;
    e.sequence = CrashProtocol::get32(in + n);
    n += 4;
    e.detectedUptime = CrashProtocol::get32(in + n);
    n += 4;
    e.sensorUptime = CrashProtocol::get32(in + n);
    n += 4;
    e.ageAtReceipt = CrashProtocol::get32(in + n);
    n += 4;
    const uint8_t known = in[n++];
    if (known > 1) return false;
    e.ageKnown = known;
    std::memcpy(e.message, in + n, 161);
    n += 161;
    e.count = in[n++];
    e.camera[0] = in[n++];
    e.camera[1] = in[n++];
    const uint8_t test = in[n++];
    e.test = test;
    if (e.count > CONTACTS || e.camera[0] > 3 || e.camera[1] > 3 || test > 1 ||
        e.message[160] || (e.boot && (!e.sequence || !e.message[0])))
      return false;
    for (auto& r : e.recipients) {
      std::memcpy(r.phone, in + n, 14);
      n += 14;
      r.outcome = in[n++];
      r.jobId = CrashProtocol::get32(in + n);
      n += 4;
      if (r.phone[13] || r.outcome > Undelivered) return false;
    }
    for (size_t i = 0; i < e.count; ++i) {
      const char* p = e.recipients[i].phone;
      if (std::strlen(p) != 13 || std::strncmp(p, "+639", 4)) return false;
      for (size_t k = 1; k < 13; ++k)
        if (p[k] < '0' || p[k] > '9') return false;
    }
  }
  for (size_t i = 0; i < TOMBSTONES; ++i) {
    j.retiredBoot[i] = CrashProtocol::get32(in + n);
    n += 4;
    j.retiredSequence[i] = CrashProtocol::get32(in + n);
    n += 4;
  }
  if(!legacy)for(auto& e:j.entries){
    auto& v=e.evidence;
    v.utcMs=uint64_t(CrashProtocol::get32(in+n))|(uint64_t(CrashProtocol::get32(in+n+4))<<32);n+=8;
    v.latitudeE7=int32_t(CrashProtocol::get32(in+n));n+=4;
    v.longitudeE7=int32_t(CrashProtocol::get32(in+n));n+=4;
    v.locationAgeMs=CrashProtocol::get32(in+n);n+=4;
    v.accelerationMg=CrashProtocol::get16(in+n);n+=2;v.rotationDps=CrashProtocol::get16(in+n);n+=2;v.voltageMv=CrashProtocol::get16(in+n);n+=2;
    uint8_t* fields[]={&v.captured,&v.timeSource,&v.timeEstimated,&v.motion,&v.gps,&v.modem,&v.mobileService,&v.camera[0],&v.camera[1],&v.battery,&v.piezo,&v.impactKnown,&v.impactBanks};
    for(auto* field:fields)*field=in[n++];
    v.batteryPercent=int8_t(in[n++]);
    if(in[n++]||in[n++])return false;
    if(v.captured>1||v.timeSource>2||v.timeEstimated>1||v.motion>1||v.gps>4||v.modem>2||v.mobileService>4||v.camera[0]>2||v.camera[1]>2||v.battery>2||v.piezo>2||v.impactKnown>1||v.impactBanks>3||v.batteryPercent< -1||v.batteryPercent>100)return false;
    if(v.utcMs && (v.utcMs<1577836800000ULL||v.utcMs>=4102444800000ULL))return false;
    if(v.latitudeE7< -900000000||v.latitudeE7>900000000||int64_t(v.longitudeE7)< -1800000000LL||int64_t(v.longitudeE7)>1800000000LL)return false;
    for(auto& r:e.recipients){std::memcpy(r.name,in+n,41);n+=41;if(r.name[40])return false;}
  }
  output = j;
  return true;
}
inline bool terminal(const Incident& e) {
  for (size_t i = 0; i < e.count; ++i)
    if (e.recipients[i].outcome == Waiting ||
        e.recipients[i].outcome == Sending)
      return false;
  return true;
}
}  // namespace Incidents
