#include "BatteryService.h"
#include "../../Config.h"

namespace {
__attribute__((noinline)) void writeFlag(JsonObject output, const char* name, bool value) {
  output[name] = value;
}
void writeCharging(JsonObject output, uint8_t state) {
  if (state) output["charging"] = state == BatteryProtocol::Charging;
  else output["charging"] = nullptr;
  writeFlag(output, "chargingEstimated", false);
}
// Share the numeric/null writer across raw measurements without duplicating
// JSON member lookup and conversion code in the flash-constrained Main image.
__attribute__((noinline)) void writeMeasurement(JsonObject output, const char* name,
                                               bool valid, float value) {
  auto field = output[name];
  if (valid) field.set(value);
  else field.set(nullptr);
}
}

int BatteryService::batteryPercent() const {
  if (!Config::BATTERY_MONITORS_PACK || !remoteLink || !remote ||
      !remote->fresh(millis())) return -1;
  return retainedPercent;
}

uint8_t BatteryService::chargingState(bool valid) const {
  // Accept measured states from compatible senders; never infer charge from load current or voltage.
  return valid && remote && BatteryProtocol::measuredCharge(remote->data.charge)
             ? remote->data.charge : uint8_t(BatteryProtocol::ChargeUnknown);
}

BatteryService::Telemetry BatteryService::telemetry() const {
  if (!remote) return {false, false, 0, 0, 0, UINT32_MAX, 0, 0, 0};
  const auto& r = remote->data;
  const uint32_t now = millis();
  const bool responding = remoteLink && remote->seen &&
      uint32_t(now - remote->receivedAt) < BatteryProtocol::STALE_MS &&
      bool(r.flags & BatteryProtocol::Responding);
  return {responding, remoteLink && remote->fresh(now),
          remote->accepted - remote->invalid, remote->invalid, 0, remote->age(now),
          r.busMV / 1000.0f + r.shuntUV / 1000000.0f,
          float(r.currentMA), float(r.powerMW)};
}

void BatteryService::writeIndicatorJson(JsonObject output) {
  writeFlag(output, "ledTestSupported", true);
  writeFlag(output, "ledTestActive", ledTest.active(millis()));
  output["ledTestMode"] = ledTest.name(millis());
  output["ledTestRemainingMs"] = ledTest.remaining(millis());
  output["indicatorState"] = BatteryIndicator::name(indicator.state);
  writeFlag(output, "indicatorEnabled", Config::ENABLE_BATTERY_LED);
  writeFlag(output, "indicatorThresholdsVerified", Config::BATTERY_MONITORS_PACK);
}

void BatteryService::writeJson(JsonObject output) {
  const auto t = telemetry();
  writeIndicatorJson(output);
  const bool currentKnown = t.valid && remote && BatteryProtocol::measuredCharge(remote->data.charge);
  writeFlag(output, "netBatteryCurrentVerified", currentKnown);
  writeCharging(output, chargingState(t.valid));
  output["netCurrentMA"] = nullptr;
  if (currentKnown) output["netCurrentMA"] = int(remote->data.netCurrentMA);
  writeFlag(output, "fullIsEstimate", true);
  writeFlag(output, "enabled", true);
  output["sensorModel"] = "INA226";
  writeFlag(output, "viaSensing", true);
  writeFlag(output, "nanoConnected", remoteLink && remote && remote->seen && uint32_t(millis()-remote->receivedAt)<BatteryProtocol::STALE_MS);
  writeFlag(output, "responding", t.responding);
  writeFlag(output, "sampleValid", t.valid);
  output["state"] = t.valid ? "measured" : t.responding ? "awaiting_sample" : "offline";
  writeMeasurement(output, "sourceV", t.valid, t.voltage);
  writeMeasurement(output, "busV", t.valid, t.valid ? remote->data.busMV / 1000.0f : 0);
  writeMeasurement(output, "currentMA", t.valid, t.current);
  writeMeasurement(output, "powerMW", t.valid, t.power);
  output["batteryPercent"] = nullptr;
  output["sampleAgeMs"] = nullptr;
  writeFlag(output, "lowBatteryVoltage", t.valid && t.voltage < Config::LOW_BATTERY_V);
  if (t.age != UINT32_MAX) output["sampleAgeMs"] = t.age;
  const int percent = batteryPercent();
  if (percent >= 0) output["batteryPercent"] = percent;
  writeFlag(output, "batteryEstimated", percent >= 0);
  writeFlag(output, "batteryCalibrated", false);
  output["estimateMethod"] = "Main voltage estimate";
  writeFlag(output, "monitorsBatteryPack", Config::BATTERY_MONITORS_PACK);
  output["sdaPin"] = nullptr;
  output["sclPin"] = nullptr;
  output["nanoTxPin"] = 1;
  output["sensingRxPin"] = 32;
  output["nanoBaud"] = 19200;
  output["nanoI2cError"] = remote && remote->seen ? remote->data.error : 0;
  output["receivedPackets"] = remote ? remote->accepted : 0;
  output["nanoSequence"] = remote && remote->seen ? remote->data.sequence : 0;
  writeMeasurement(output, "shuntMV", t.valid, t.valid ? remote->data.shuntUV / 1000.0f : 0);
  output["address"] = 0x40;
  output["samples"] = t.samples;
  output["failures"] = t.failures;
  output["retries"] = nullptr;
}

void BatteryService::writeRiderBattery(JsonObject output) const {
  const bool available = retainedPercent >= 0;
  const bool current = batteryPercent() >= 0;
  const bool low = available && retainedPercent <= Config::BATTERY_LOW_PERCENT;
  output["percent"] = nullptr;
  output["voltageV"] = nullptr;
  output["ageMs"] = nullptr;
  if (available) {
    output["percent"] = retainedPercent;
    output["voltageV"] = retainedVoltage;
    output["ageMs"] = retainedAge + uint32_t(millis() - retainedTick);
  }
  writeFlag(output, "current", current && available);
  writeCharging(output, chargingState(current));
  writeFlag(output, "estimated", available);
  writeFlag(output, "low", low);
  output["help"] = !available ? "Waiting for your battery level." : !current ?
      "Showing your last battery level. Waiting for an update." : low ?
      "Battery is low. Charge your vest before riding." : "Estimated battery level.";
}

void BatteryService::updateIndicator() {
  ledTest.tick(millis());
  const auto t = telemetry();
  // Retain display data in RAM; live validity still controls the LED.
  const uint32_t now = millis();
  if (retainedPercent >= 0) retainedAge += uint32_t(now - retainedTick);
  retainedTick = now;
  // Main owns the estimate. Older Nano percentages remain wire-compatible
  // but are ignored. Advance the filter once per accepted physical sample.
  const bool valid = Config::BATTERY_MONITORS_PACK && t.valid;
  if (valid && (retainedPercent < 0 || remote->accepted != estimatedAccepted)) {
    estimatedAccepted = remote->accepted;
    retainedPercent = display.update(BatteryDisplay::sourceMillivolts(
        remote->data.busMV, remote->data.shuntUV), now - t.age);
    retainedVoltage = t.voltage;
    retainedAge = t.age;
  }
  if (!valid) display.invalidate();
  indicator.updatePercent(valid, batteryPercent(), Config::BATTERY_CRITICAL_PERCENT, Config::BATTERY_LOW_PERCENT);
}
