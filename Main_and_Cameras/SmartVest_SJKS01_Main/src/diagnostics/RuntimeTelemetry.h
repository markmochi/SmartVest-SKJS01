#pragma once
#include <cstdint>
#include "../network/ApState.h"
struct CameraTelemetry {
  bool online = false, recording = false, sdReady = false;
  uint32_t heartbeats = 0, commands = 0, acknowledgements = 0, ageMs = UINT32_MAX;
};
struct RuntimeTelemetry {
  bool webReady = false, networkOnline = false, mdns = false, connecting = false;
  bool hotspot = false;
  char ip[20] = {};
  uint32_t attempts = 0, retryInMs = 0;
  uint32_t capturedAt=0,httpRequests=0,httpExpired=0,httpRejected=0,maxHttpMs=0;
  uint32_t httpWriteAborts=0,httpFlashCompleted=0,maxFlashMs=0;
  uint32_t httpRequestMemoryFailures=0,httpResponseMemoryFailures=0;
  uint32_t httpDocumentFailures=0,httpOutputFailures=0,httpLengthFailures=0;
  uint32_t httpQueuedBytes=0,httpMaxQueuedBytes=0;
  uint8_t httpSending=0;
  uint32_t apStarts=0,apStops=0,clientJoins=0,clientLeaves=0;
  uint8_t clients=0;
  ApState radio;
  CameraTelemetry cameras[2];
};
