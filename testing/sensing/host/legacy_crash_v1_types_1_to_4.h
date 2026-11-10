// Original receiver codec from before-source.zip; namespace renamed for compatibility tests only.
#pragma once
#include <cstddef>
#include <cstdint>

// Wire format V1: 32 bytes, little endian, CRC-16/CCITT-FALSE. No heap or
// strings. Canonical source; verification/sync_protocol.py copies this into
// both sketches.
namespace LegacyCrashProtocol {
constexpr size_t FRAME_SIZE = 32;
constexpr size_t SENSOR_OUTBOX_CAPACITY = 4;
enum class Type : uint8_t { Heartbeat = 1, Crash = 2, Ack = 3, Test = 4 };
enum Flag : uint8_t { ImuReady = 1, PiezoReady = 2, Armed = 4, Durable = 8 };
struct Frame {
  Type type = Type::Heartbeat;
  uint8_t flags = 0;
  uint32_t boot = 0, sequence = 0, uptimeMs = 0;
  uint16_t accelerationMg = 0, rotationDps = 0;
  uint16_t piezoFront = 0, piezoRear = 0, pending = 0;
};
inline uint16_t crc16(const uint8_t* data, size_t size) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < size; ++i) {
    crc ^= uint16_t(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}
inline void put16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}
inline void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint16_t get16(const uint8_t* p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t get32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i);
  return v;
}
inline void encode(const Frame& f, uint8_t* out) {
  for (size_t i = 0; i < FRAME_SIZE; ++i) out[i] = 0;
  out[0] = 'S';
  out[1] = 'V';
  out[2] = 1;
  out[3] = uint8_t(f.type);
  out[4] = f.flags;
  out[5] = FRAME_SIZE;
  put32(out + 8, f.boot);
  put32(out + 12, f.sequence);
  put32(out + 16, f.uptimeMs);
  put16(out + 20, f.accelerationMg);
  put16(out + 22, f.rotationDps);
  put16(out + 24, f.piezoFront);
  put16(out + 26, f.piezoRear);
  put16(out + 28, f.pending);
  put16(out + 30, crc16(out, 30));
}
inline bool decode(const uint8_t* in, Frame& f) {
  if (in[0] != 'S' || in[1] != 'V' || in[2] != 1 || in[5] != FRAME_SIZE ||
      in[6] || in[7] || in[3] < 1 || in[3] > 4 || (in[4] & ~uint8_t(15)) ||
      get16(in + 30) != crc16(in, 30))
    return false;
  f.type = Type(in[3]);
  f.flags = in[4];
  f.boot = get32(in + 8);
  f.sequence = get32(in + 12);
  f.uptimeMs = get32(in + 16);
  f.accelerationMg = get16(in + 20);
  f.rotationDps = get16(in + 22);
  f.piezoFront = get16(in + 24);
  f.piezoRear = get16(in + 26);
  f.pending = get16(in + 28);
  return f.boot != 0;
}
class Parser {
 public:
  uint32_t rejected = 0;
  bool feed(uint8_t byte, uint32_t now, Frame& out) {
    if (used && uint32_t(now - lastByte) >= 100) used = 0;
    lastByte = now;
    bytes[used++] = byte;
    while (used && (bytes[0] != 'S' || (used > 1 && bytes[1] != 'V'))) shift();
    if (used < FRAME_SIZE) return false;
    if (decode(bytes, out)) {
      used = 0;
      return true;
    }
    ++rejected;
    shift();
    while (used && (bytes[0] != 'S' || (used > 1 && bytes[1] != 'V'))) shift();
    return false;
  }

 private:
  uint8_t bytes[FRAME_SIZE] = {};
  size_t used = 0;
  uint32_t lastByte = 0;
  void shift() {
    for (size_t i = 1; i < used; ++i) bytes[i - 1] = bytes[i];
    --used;
  }
};
}  // namespace LegacyCrashProtocol
