#pragma once
#include "BatteryIndicator.h"
#include <cstring>
// Volatile, time-limited LED override. Never changes measured battery data.
class BatteryLedTest {
 public:
  static constexpr uint32_t DURATION_MS = 15000;
  bool start(const char* mode, uint32_t now) {
    if (!mode) return false;
    if (!std::strcmp(mode, "stop")) { running = false; return true; }
    BatteryIndicator::State next;
    bool nextBlue = false;
    if (!std::strcmp(mode, "critical")) next = BatteryIndicator::State::Critical;
    else if (!std::strcmp(mode, "low")) next = BatteryIndicator::State::Low;
    else if (!std::strcmp(mode, "charging")) next = BatteryIndicator::State::Charging;
    else if (!std::strcmp(mode, "full")) next = BatteryIndicator::State::Full;
    else if (!std::strcmp(mode, "off")) next = BatteryIndicator::State::Unknown;
    else if (!std::strcmp(mode, "blue")) { next = BatteryIndicator::State::Unknown; nextBlue = true; }
    else return false;
    selected = next; blue = nextBlue; started = now; running = true;
    return true;
  }
  void tick(uint32_t now) { if (running && uint32_t(now - started) >= DURATION_MS) running = false; }
  bool active(uint32_t now) const { return running && uint32_t(now - started) < DURATION_MS; }
  uint32_t remaining(uint32_t now) const { return active(now) ? DURATION_MS - uint32_t(now - started) : 0; }
  BatteryIndicator::State state() const { return selected; }
  bool blueOn() const { return blue; }
  const char* name(uint32_t now) const {
    if (!active(now)) return "none";
    if (blue) return "blue";
    if (selected == BatteryIndicator::State::Full) return "full";
    if (selected == BatteryIndicator::State::Unknown) return "off";
    return BatteryIndicator::name(selected);
  }
 private:
  bool running = false, blue = false;
  uint32_t started = 0;
  BatteryIndicator::State selected = BatteryIndicator::State::Unknown;
};
