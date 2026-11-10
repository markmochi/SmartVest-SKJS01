#pragma once
#include "RuntimeTelemetry.h"
#include "../storage/StateStore.h"
#include "../gsm/SIM800LService.h"
#include "../gps/NEO6MService.h"
#include "../link/SensorLink.h"
#include "../power/BatteryService.h"
class SerialReporter {
 public:
  void begin();
  void tick(const StateStore& store, const SIM800LService& gsm, const NEO6MService& gps,
            const BatteryService& power, const SensorLink& sensor, const EmergencyCoordinator& emergency,
            const RuntimeTelemetry& runtime);
 private:
  uint32_t lastCheck = 0, previousHealth = UINT32_MAX;
  char previousIp[20] = {};
};
