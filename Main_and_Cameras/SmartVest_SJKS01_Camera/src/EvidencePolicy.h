#pragma once
#include <stdint.h>
namespace EvidencePolicy {
struct Window { uint64_t start, end, trigger; bool known, prePossible; };
inline Window window(uint64_t now, uint64_t age, bool known,
                     uint64_t pre, uint64_t post, uint64_t margin) {
  known = known && age <= now;
  const uint64_t trigger = known ? now - age : now;
  const bool enough = known && trigger >= pre + margin;
  return {trigger >= pre + margin ? trigger - pre - margin : 0,
          trigger + post + margin, trigger, known, enough};
}
inline bool overlaps(uint64_t start, uint64_t end, const Window& w) {
  return start <= w.end && end >= w.start;
}
inline bool canProtect(const Window& w, uint64_t first, uint64_t last,
                       bool continuous, bool filesVerified) {
  return w.known && w.prePossible && first <= w.start && last >= w.end &&
         continuous && filesVerified;
}
}
