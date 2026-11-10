#pragma once

#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include "../diagnostics/UartHealth.h"
#include <TinyGPSPlus.h>

/*
  NEO-6M GPS module
  ----------------
  Reads GPS data and keeps the most recent valid position available for the
  dashboard and crash-message location snapshot.
*/
class NEO6MService {
 public:
  UartHealth uartHealth;
  bool latestUtcMs(uint64_t& utcMs);
  void begin();
  void tick();
  void writeJson(JsonObject output);
  HardwareSerial& consolePort() { return uart; }
  struct Telemetry {
    bool receiving, decoding, locationValid, fresh;
    uint32_t bytes, sentences, checksumFailures, baud, byteAge, fixAge;
  };
  Telemetry telemetry() const;
  bool weakGeometry();

  bool latestLocation(double& latitude,
                      double& longitude,
                      uint32_t& ageMs,
                      bool& fresh);

 private:
  HardwareSerial uart{1};
  TinyGPSPlus gps;
  TinyGPSCustom satellitesInView{gps, "GPGSV", 3};
  uint64_t clockUtcMs=0;
  uint32_t clockReceivedAt=0;
  uint32_t lastByteAt = 0;
  uint32_t lastSentenceAt = 0;
  uint32_t rateSince = 0;
  uint32_t validSentences = 0;
  uint32_t rxBytes = 0;
  uint32_t baud = 9600;
  size_t rateIndex = 0;
};
