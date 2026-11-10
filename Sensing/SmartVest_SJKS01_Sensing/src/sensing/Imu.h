#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "BMI323SPI.h"
struct ImuSample {
  float accel[3] = {}, gyro[3] = {}, temperatureC = 0;
  int16_t rawAccel[3] = {}, rawGyro[3] = {}, rawTemperature = INT16_MIN;
  uint32_t atMs = 0, atUs = 0, sequence = 0, intervalUs = 0, sensorTime = 0, deltaUs = 0;
  bool saturated = false, accelClipped = false, gyroClipped = false;
  bool temperatureValid = false, temperatureFresh = false, deltaValid = false;
};
class Imu {
 public:
  void begin(); void tick(); void retry(); bool healthy() const;
  enum class Error : uint8_t { None, Start, Identity, Configuration, Rejected, Stale };
  enum class Phase : uint8_t { Retry, Live };
  Error error = Error::None; Phase phase = Phase::Retry;
  bool fresh = false, continuityLost = true;
  uint16_t whoAmI = 0, observed = 0, expected = 0;
  uint8_t faultRegister = 0;
  uint32_t failures = 0, samples = 0, overruns = 0, duplicates = 0, notReady = 0, maxCycleUs = 0;
  ImuSample sample;
  BMI323SPI driver;
 private:
  bool sampled = false;
  uint32_t phaseAt = 0;
};
