#pragma once
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include "../diagnostics/UartHealth.h"

#include "../emergency/EmergencyCoordinator.h"
#include "CrashProtocol.h"
#include "ImuLabProtocol.h"
#include "PiezoProtocol.h"
#include "../power/BatteryProtocol.h"
class SensorLink {
 public:
  UartHealth uartHealth;
  explicit SensorLink(EmergencyCoordinator& c) : coordinator(c) {}
  void begin();
  void tick();
  void writeJson(JsonObject output);
  bool armedAndHealthy() const;
  struct Telemetry {
    bool online, armed, imu, piezo, durable;
    uint32_t bytes, frames, heartbeats, acknowledgements, rejected, transmitted, deferred, rxAge, heartbeatAge;
    uint16_t pending;
  };
  Telemetry telemetry() const;
  void writeLabJson(JsonObject output);
  void writeRiderPoseJson(JsonObject output);
  ImuLabProtocol::Result riderCalibrationReady(ImuLabProtocol::Op op) const;
  uint32_t requestLab(ImuLabProtocol::Op op, uint16_t argument = 0,
                      uint16_t count = 0, uint32_t revision = 0);
  bool labOnline() const;
  bool piezoOnline() const;
  void writePiezoJson(JsonObject output);
  ImuLabProtocol::Result piezoCommandReady(ImuLabProtocol::Op op) const;
  const BatteryProtocol::Tracker& batterySource() const {return battery;}

 private:
  EmergencyCoordinator& coordinator;
  HardwareSerial uart{0};
  ImuLabProtocol::Parser parser;
  BatteryProtocol::Tracker battery;
  ImuLabProtocol::Live labLive;
  PiezoProtocol::Status piezoLive;
  uint32_t piezoAt=0,piezoSequence=0;
  bool piezoSeen=false;
  ImuLabProtocol::Metadata labMeta;
  ImuLabProtocol::Frame labCommand;
  uint32_t labSession = 0, helloAt = 0;
  uint32_t labBoot = 0, labSeenAt = 0, commandAt = 0, commandSentAt = 0, commandSequence = 0;
  bool labSeen = false, metaSeen = false, commandPending = false, commandSent = false;
  uint32_t labLiveAccepted=0, labMessagesReceived=0, labHelloSent=0;
  uint8_t commandState = 0, commandResult = 0, commandAttempts = 0;
  void reportIncidentImpact();
  void receiveLab(const ImuLabProtocol::Frame& frame);
  void pumpLabCommand();
  uint32_t lastSeen = 0, accepted = 0, rejected = 0;
  bool seen = false;
  uint32_t rxBytes = 0, txBytes = 0, frames = 0, heartbeatFrames = 0, ackDeferred = 0, lastByteAt = 0;
  CrashProtocol::Frame heartbeat;
};
