#pragma once
#include "Arduino.h"
#include <map>
#include <algorithm>
namespace FakeNvs {
inline std::map<std::string, std::vector<uint8_t>> blobs;
inline bool failWrite = false, corruptReadback = false, failBegin = false;
inline unsigned writes = 0;
inline void reset() { blobs.clear(); failWrite = false; corruptReadback = false; failBegin = false; writes = 0; }
}
class Preferences {
 public:
  bool begin(const char* space, bool readOnly) { ns = space; ro = readOnly; return !FakeNvs::failBegin; }
  void end() {}
  size_t getBytesLength(const char* key) { return FakeNvs::blobs[ns + "/" + key].size(); }
  size_t getBytes(const char* key, void* output, size_t n) {
    const auto& blob = FakeNvs::blobs[ns + "/" + key];
    if (blob.size() > n) return 0;
    std::memcpy(output, blob.data(), blob.size());
    if (FakeNvs::corruptReadback && blob.size()) ((uint8_t*)output)[blob.size() - 1] ^= 1;
    return blob.size();
  }
  size_t putBytes(const char* key, const void* p, size_t n) {
    if (ro || FakeNvs::failWrite) return 0;
    ++FakeNvs::writes;
    const auto* bytes = (const uint8_t*)p;
    FakeNvs::blobs[ns + "/" + key] = std::vector<uint8_t>(bytes, bytes + n); return n;
  }
 private: std::string ns; bool ro = false;
};
