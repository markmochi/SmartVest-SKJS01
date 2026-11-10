#pragma once
#include <cmath>
#include <cstdint>

namespace BatteryIndicator {
enum class State { Unknown, Normal, Critical, Low, Charging, Full };
inline const char* name(State s) {
  switch (s) {
    case State::Normal: return "normal";
    case State::Critical: return "critical";
    case State::Low: return "low";
    case State::Charging: return "charging";
    case State::Full: return "full_estimated";
    default: return "unknown";
  }
}
class Policy {
 public:
  State state = State::Unknown;
  // Main already filters its estimate. Above the low threshold the automatic
  // battery light is off, including at 100%; green remains a manual test mode.
  void updatePercent(bool valid, int percent, int critical, int low) {
    state = State::Unknown;
    if (!valid || percent < 0 || percent > 100 || critical < 0 ||
        low <= critical || low >= 100) return;
    state = percent <= critical ? State::Critical :
            percent <= low ? State::Low :
            State::Normal;
  }

};
struct Output { bool red, green; };
inline Output output(State state, uint32_t now, uint32_t criticalMs, uint32_t chargeMs) {
  return {state == State::Low || (state == State::Critical && criticalMs && (now / criticalMs) % 2 == 0),
          state == State::Full || (state == State::Charging && chargeMs && (now / chargeMs) % 2 == 0)};
}
} // namespace BatteryIndicator
