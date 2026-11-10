#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "BatteryIndicator.h"
#include "BatteryLedTest.h"
#include "BatteryProtocol.h"
#include "BatteryDisplay.h"

class BatteryService {
 public:
  void attachBattery(const BatteryProtocol::Tracker* source) { remote = source; }
  void setRemoteLink(bool current) { remoteLink = current; }
  void writeJson(JsonObject output);
  void writeIndicatorJson(JsonObject output);
  void writeRiderBattery(JsonObject output) const;
  int batteryPercent() const;
  uint8_t chargingState(bool valid) const;
  BatteryIndicator::State indicatorState() const { return indicator.state; }
  void updateIndicator();
  bool startLedTest(const char* mode) { return ledTest.start(mode, millis()); }
  BatteryIndicator::State ledOutputState() const {
    return ledTest.active(millis()) ? ledTest.state() : indicator.state;
  }
  bool ledTestActive() const { return ledTest.active(millis()); }
  bool ledTestBlue() const { return ledTest.active(millis()) && ledTest.blueOn(); }
  struct Telemetry {
    bool responding, valid;
    uint32_t samples, failures, retries, age;
    float voltage, current, power;
  };
  Telemetry telemetry() const;

 private:
  BatteryIndicator::Policy indicator;
  BatteryLedTest ledTest;
  BatteryDisplay display;
  uint32_t estimatedAccepted = 0;
  const BatteryProtocol::Tracker* remote = nullptr;
  bool remoteLink = false;
  int retainedPercent = -1;
  float retainedVoltage = 0;
  uint64_t retainedAge = 0;
  uint32_t retainedTick = 0;
};
