#pragma once
#include <Arduino.h>
#include "LocalDnsServer.h"
#include <WiFi.h>
#include <atomic>
#include "ApState.h"
#include "ApServiceWatchdog.h"

// Called ONLY from the dashboard task (begin before that task starts).
// Owns Wi-Fi, DNS and mDNS. Does not own settings, credentials in NVS, or HTTP.
class NetworkService {
 public:
  bool begin();
  void tick();
  bool apActive() const { return apStarted && apRadioUp.load(); }
  bool stationConnected() const;
  bool captivePortalActive() const { return dnsStarted && !localModeStarted && apActive(); }
  bool localDeviceMode() const { return localModeStarted && apActive(); }
  bool hotspotDnsActive() const { return dnsStarted && apActive(); }
  bool allowedHost(const String& host) const;
  bool validOrigin(const String& origin, const String& host) const;
  String dashboardUrl() const;
  String networkName() const;
  const char* modeName() const;
  uint8_t hotspotClients() const;
  bool mdnsActive() const { return mdnsStarted; }
  bool connecting() const { return attemptActive; }
  uint32_t attempts() const { return connectionAttempts; }
  uint32_t retryInMs() const;
  String localIp() const;
  int signalDbm() const;
  const char* error() const { return lastError; }
  uint32_t apStarts() const {return starts.load();}
  uint32_t apStops() const {return stops.load();}
  uint32_t clientJoins() const {return joins.load();}
  uint32_t clientLeaves() const {return leaves.load();}
  ApState radioState() const;
  void observeService(uint32_t requests,bool bothCameras){service.observe(millis(),requests,bothCameras,authFaults.load());}
 private:
  std::atomic<bool> apRadioUp{false};
  std::atomic<uint32_t> starts{0},stops{0},joins{0},leaves{0};
  std::atomic<uint32_t> lastStart{0},lastStop{0};
  std::atomic<uint16_t> leaveReason{0};
  std::atomic<uint32_t> authFaults{0};
  ApServiceWatchdog service;
  ApState radio;
  uint32_t radioCheckAt=0;
  uint32_t connectionAttempts = 0;
  const char* lastError = "";
  LocalDnsServer dns;
  bool apStarted = false, dnsStarted = false, mdnsStarted = false;
  bool eventsRegistered=false;
  bool localModeStarted = false;
  bool apFault = false;
  uint32_t apFaultAt = 0, apRetryAt = 0, apRetryDelay = 0, dnsRetryAt = 0;
  bool attemptActive = false, wasConnected = false;
  uint32_t attemptAt = 0, retryAt = 0, retryDelay = 0, mdnsAttemptAt = 0;
  String lastStationIp;
  void startStationAttempt();
  bool startHotspot();
  void startDns();
  void maintainHotspot(uint32_t now);
  void inspectRadio();
  void refreshMdns(bool force = false);
};
