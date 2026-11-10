#pragma once
#include "../../Config.h"
#include "BatteryIndicator.h"
#include "CrashLedSignal.h"
class BatteryLed {
 public:
  void begin() {
    if (!Config::ENABLE_BATTERY_LED) return;
    const int pins[] = {Config::LED_RED_PIN, Config::LED_GREEN_PIN, Config::LED_BLUE_PIN};
    for (int pin : pins) { digitalWrite(pin, level(false)); pinMode(pin, OUTPUT); }
    greenModulated = sigmaDeltaAttach(Config::LED_GREEN_PIN, 312500);
    if (greenModulated) sigmaDeltaWrite(Config::LED_GREEN_PIN, Config::LED_COMMON_ANODE ? 255 : 0);
    else { pinMode(Config::LED_GREEN_PIN, OUTPUT); digitalWrite(Config::LED_GREEN_PIN, level(false)); }
    // Power-on indication precedes sensor/modem initialization.
    for (unsigned flash = 0; flash < 6; ++flash) {
      write(true, 255, true); delay(250);
      write(false, 0, false); delay(250);
    }
    startedAt = millis();
  }
  void tick(BatteryIndicator::State state, bool blue = false, bool manualTest = false,
            CrashLedSignal* crash = nullptr) {
    if (!Config::ENABLE_BATTERY_LED) return;
    const uint32_t now = millis();
    if (crash && crash->tick(now) != CrashLedSignal::Pattern::Off) {
      testFinished = true;
      const bool on = crash->lit(now);
      write(on, on ? 64 : 0, false);
      return;
    }
    if (manualTest) testFinished = true;
    if (Config::BATTERY_LED_BOOT_TEST && !testFinished) {
      const uint32_t age = now - startedAt;
      if (age < 6000) {
        write(age < 2000, age >= 2000 && age < 4000 ? 255 : 0, age >= 4000);
        return;
      }
      testFinished = true;
    }
    const auto value = BatteryIndicator::output(state, now,
        Config::BATTERY_CRITICAL_BLINK_MS, Config::BATTERY_CHARGING_BLINK_MS);
    write(value.red, value.green ? 255 : 0, blue);
  }
  bool colorReady() const { return greenModulated; }
 private:
  uint32_t startedAt = 0;
  bool testFinished = false;
  bool greenModulated = false;
  int lastRed = -1, lastGreen = -1, lastBlue = -1;
  void write(bool red, uint8_t green, bool blue) {
    if (lastRed != red) { digitalWrite(Config::LED_RED_PIN, level(red)); lastRed = red; }
    if (lastGreen != green) {
      if (greenModulated) sigmaDeltaWrite(Config::LED_GREEN_PIN, Config::LED_COMMON_ANODE ? 255 - green : green);
      else digitalWrite(Config::LED_GREEN_PIN, level(green != 0));
      lastGreen = green;
    }
    if (lastBlue != blue) { digitalWrite(Config::LED_BLUE_PIN, level(blue)); lastBlue = blue; }
  }
  static int level(bool on) { return (on != Config::LED_COMMON_ANODE) ? HIGH : LOW; }
};
