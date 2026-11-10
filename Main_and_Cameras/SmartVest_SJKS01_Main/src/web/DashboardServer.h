#pragma once

#include "../network/NetworkService.h"
#include "ReadyWebServer.h"
#include "CameraTelemetry.h"
#include "CameraPresence.h"
#include "CameraFiles.h"
#include "DownloadLease.h"
#include "DiagnosticPaging.h"
#include "ResponseMemory.h"
#include "../security/CameraAuth.h"
#include "../gps/UtcClock.h"
#include "../diagnostics/RuntimeTelemetry.h"

#include "../gps/NEO6MService.h"
#include "../gsm/SIM800LService.h"
#include "../power/BatteryService.h"
#include "../security/Security.h"
#include "../security/LoginVerifier.h"
#include "../storage/StateStore.h"
#include "../link/SensorLink.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Owns local network, authenticated dashboard APIs and camera reports.
class DashboardServer {
 public:
  DashboardServer(StateStore& stateStore, SIM800LService& sim800lService,
                  NEO6MService& neo6mService, BatteryService& batteryService)
      : store(stateStore),
        sim800l(sim800lService),
        neo6m(neo6mService),
        battery(batteryService) {}

  bool begin();
  bool startNetwork() { return networkStarted || (networkStarted = network.begin()); }
  void tick();
  void refreshIncidentContext(); // Application owns the state mutex.
  RuntimeTelemetry runtimeTelemetry() const { return runtime; }
  void attachRuntime(SemaphoreHandle_t mutex, SensorLink& link,
                     EmergencyCoordinator& coordinator) {
    stateMutex = mutex;
    sensorLink = &link;
    emergency = &coordinator;
  }

 private:
  struct CameraStatus : CameraPresence {
    bool recording = false;
    bool sdReady = false;
    bool lockSupported = false;
    String firmware;
    IPAddress address;
    JsonDocument telemetry;CameraMonitor::Cursor cursor;
    uint32_t heartbeats = 0, commands = 0, acknowledgements = 0;
  };

  StateStore& store;
  SIM800LService& sim800l;
  NEO6MService& neo6m;
  BatteryService& battery;

  RuntimeTelemetry runtime;
  uint32_t lastRuntimeSnapshot = 0;
  NetworkService network;
  bool networkStarted = false;
  uint32_t networkRetryAt=0;
  ReadyWebServer web{80};
  Security security;
  LoginVerifier login;
  void beginLogin();
  void finishLogin();
  CameraStatus cameras[2];
  CameraFiles cameraFiles[2];
  DownloadLease download;
  void writeDownload(JsonObject out,Security::Session* session);
  void observeDownload(int camera);
  void serveCameraFiles(bool write);
  void receiveCameraFiles();
  void offerCameraFiles(int camera,JsonObject out);
  CameraMonitor::Journal cameraLogs;
  ResponseMemory::Health responseHealth;
  uint32_t diagnosticEpoch=0;
  UtcClock::Anchor namingClock;
  void writeClock(JsonObject out);
  void sendCameraMonitor();
  SemaphoreHandle_t stateMutex = nullptr;
  SensorLink* sensorLink = nullptr;
  EmergencyCoordinator* emergency = nullptr;
  bool stateLocked = false;
  void acquireState();
  void releaseState();
  int cameraIdentity();
  void cameraSession();
  CameraAuth::Peer cameraPeers[2];
  CameraAuth::Exchange cameraExchange{};
  int cameraResponseRole=-1;
  uint32_t testBoot = 0, testSequence = 0;

  uint32_t lastManualMessage = 0;
  bool manualMessageSent = false;

  void addSecurityHeaders(bool immutable = false);
  static __attribute__((noinline)) JsonDocument makeJson();
  void bindRoute(const char* path,HTTPMethod method,void(DashboardServer::*handler)(),
                 Access::Role role=Access::Role::Developer,bool gated=true);
  void sendSession(Security::Session* session, bool defaultPassword);
  void sendJson(JsonDocument& document, int statusCode = 200);
  void sendError(int statusCode, const String& message);
  void sendError(int statusCode, const char* message);

  bool isCanonicalHost();
  bool isValidOrigin();
  Security::Session* authorize(bool write = false,
                               Access::Role required = Access::Role::Developer);
  bool readJsonBody(JsonDocument& document);
  void redirectToDashboard();
  bool sendConnectivityCheck();
  void sendAndroidCaptivePage();
  void sendPortalPage();
  void sendAdminPage();
  void registerRiderRoutes();
  bool sendWebAsset(const char* path);
  void sendRiderStatus();
  void sendRiderAccount(bool administrator = false);
  void resetData();
  void completeRiderSetup();
  void changeRiderAccount(bool administrator = false);

  void sendStatus();
  void sendRiderPose();
  void commandRiderCalibration();
  void sendImuLab();
  void commandImuLab();
  void commandPiezo();
  void sendModemTrace();
  void sendSettings();
  void saveSettings();
  void sendJobs();
  void queueContactCheck();
  void queueIntroduction();
  void queueManualMessage(const String& kind);
  void handleJobAction();
  void writeMessageQueue(JsonObject output, bool includeContacts = true);
  void cancelMessages();
  void cancelMessageSelection(JsonObjectConst selection);
  void changePassword();
  void sendIntegrations();
  void cameraHeartbeat();
};
