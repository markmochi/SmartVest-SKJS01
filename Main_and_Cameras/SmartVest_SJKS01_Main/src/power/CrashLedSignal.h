#pragma once
#include <cstdint>

class CrashLedSignal {
 public:
  enum class Pattern : uint8_t { Off, Blink, Steady };
  static constexpr uint32_t DURATION_MS = 5000;
  void detected(uint32_t boot, uint32_t sequence, uint32_t now) {
    if (!boot || !sequence || (boot == ownerBoot && sequence == ownerSequence)) return;
    ownerBoot = boot; ownerSequence = sequence;
    succeeded = false; started = now; current = Pattern::Blink;
  }
  void sent(uint32_t boot, uint32_t sequence, uint32_t now) {
    if (!ownerBoot || boot != ownerBoot || sequence != ownerSequence || succeeded) return;
    succeeded = true; started = now; current = Pattern::Steady;
  }
  Pattern tick(uint32_t now) {
    if (current != Pattern::Off && uint32_t(now - started) >= DURATION_MS) current = Pattern::Off;
    return current;
  }
  bool lit(uint32_t now) const {
    return current == Pattern::Steady ||
        (current == Pattern::Blink && (uint32_t(now - started) / 250) % 2 == 0);
  }
 private:
  uint32_t ownerBoot = 0, ownerSequence = 0, started = 0;
  Pattern current = Pattern::Off;
  bool succeeded = false;
};
