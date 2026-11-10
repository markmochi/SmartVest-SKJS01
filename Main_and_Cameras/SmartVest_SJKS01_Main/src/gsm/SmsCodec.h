#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Pure, bounded decoding. An invalid report must never imply delivery.
namespace SmsCodec {
struct Report {
  unsigned reference = 0;
  unsigned status = 0;
  char recipient[32] = {};
};

inline bool validText(const char* text, size_t size) {
  size_t units = 0;
  if (!size) return false;
  for (size_t i = 0; i < size; ++i) {
    const unsigned char c = text[i];
    if ((c < 32 && c != '\r' && c != '\n') || c > 126 || c == '`') return false;
    units += (c == '^' || c == '{' || c == '}' || c == '\\' || c == '[' ||
              c == ']' || c == '~' || c == '|')
                 ? 2
                 : 1;
    if (units > 160) return false;
  }
  return true;
}

inline bool timestamp(const char* s) {
  if (std::strlen(s) != 20 || s[2] != '/' || s[5] != '/' || s[8] != ',' ||
      s[11] != ':' || s[14] != ':' || (s[17] != '+' && s[17] != '-'))
    return false;
  const int positions[] = {0, 3, 6, 9, 12, 15, 18};
  unsigned values[7] = {};
  for (size_t i = 0; i < 7; ++i) {
    const int p = positions[i];
    if (s[p] < '0' || s[p] > '9' || s[p + 1] < '0' || s[p + 1] > '9')
      return false;
    values[i] = unsigned(s[p] - '0') * 10 + unsigned(s[p + 1] - '0');
  }
  return values[1] >= 1 && values[1] <= 12 && values[2] >= 1 &&
         values[2] <= 31 && values[3] <= 23 && values[4] <= 59 &&
         values[5] <= 59 && values[6] <= 79;
}

// Text-mode CMGS can include SCTS in phase 2+. Reject trailing garbage.
inline bool submissionReference(const char* text, int& reference) {
  int value = -1, consumed = 0;
  if (std::sscanf(text, "+CMGS: %d %n", &value, &consumed) != 1 ||
      consumed <= 0 || value < 0 || value > 255)
    return false;
  const char* tail = text + consumed;
  if (*tail) {
    char stamp[21] = {};
    int end = 0;
    if (std::sscanf(tail, ", \"%20[^\"]\" %n", stamp, &end) != 1 || end <= 0 ||
        tail[end] != '\0' || !timestamp(stamp))
      return false;
  }
  reference = value;
  return true;
}

inline bool textReport(const char* text, Report& report) {
  unsigned fo = 0, toa = 0;
  char sent[24] = {}, discharged[24] = {}, extra = 0;
  Report next;
  if (std::sscanf(text,
                  "+CDS: %u,%u,\"%31[^\"]\",%u,\"%23[^\"]\",\"%23[^\"]\",%u %c",
                  &fo, &next.reference, next.recipient, &toa, sent, discharged,
                  &next.status, &extra) != 7)
    return false;
  if (fo > 255 || (fo & 3) != 2 || (fo & 32) || next.reference > 255 ||
      next.status > 127 || (toa != 129 && toa != 145) || !timestamp(sent) ||
      !timestamp(discharged))
    return false;
  report = next;
  return true;
}

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool pduReport(const char* text, size_t size, unsigned tpduLength,
                      Report& report) {
  if (!size || size > 384 || (size & 1)) return false;
  uint8_t bytes[192] = {};
  const size_t count = size / 2;
  for (size_t i = 0; i < count; ++i) {
    const int lo = hexDigit(text[2 * i + 1]), hi = hexDigit(text[2 * i]);
    if (lo < 0 || hi < 0) return false;
    bytes[i] = uint8_t(hi * 16 + lo);
  }
  size_t pos = size_t(bytes[0]) + 1;  // SMSC length includes its address type.
  if (pos >= count || count - pos != tpduLength || count - pos < 4)
    return false;
  const uint8_t fo = bytes[pos++];
  if ((fo & 3) != 2 || (fo & 32))
    return false;  // SMS-SUBMIT status, not a command report.
  Report next;
  next.reference = bytes[pos++];
  const unsigned digits = bytes[pos++];
  const unsigned toa = bytes[pos++];
  if (!digits || digits > 20 || (toa != 129 && toa != 145)) return false;
  const size_t addressBytes = (digits + 1) / 2;
  if (count - pos < addressBytes + 15) return false;
  size_t out = 0;
  if (toa == 145) next.recipient[out++] = '+';
  for (unsigned i = 0; i < digits; ++i) {
    const unsigned d =
        (i & 1) ? bytes[pos + i / 2] >> 4 : bytes[pos + i / 2] & 15;
    if (d > 9) return false;
    next.recipient[out++] = char('0' + d);
  }
  if ((digits & 1) && (bytes[pos + addressBytes - 1] >> 4) != 15) return false;
  pos += addressBytes;
  for (unsigned stamp = 0; stamp < 2; ++stamp) {
    unsigned fields[7] = {};
    for (unsigned i = 0; i < 7; ++i) {
      const uint8_t b = bytes[pos++];
      const unsigned tens = i == 6 ? b & 7 : b & 15;
      const unsigned units = b >> 4;
      if (tens > 9 || units > 9) return false;
      fields[i] = tens * 10 + units;
    }
    if (!fields[1] || fields[1] > 12 || !fields[2] || fields[2] > 31 ||
        fields[3] > 23 || fields[4] > 59 || fields[5] > 59 || fields[6] > 79)
      return false;
  }
  next.status = bytes[pos];
  if (next.status > 127) return false;
  report = next;
  return true;
}
}  // namespace SmsCodec
