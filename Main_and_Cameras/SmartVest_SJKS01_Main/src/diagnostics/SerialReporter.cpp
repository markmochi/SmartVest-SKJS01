#include "SerialReporter.h"
#include "Console.h"
#include "../../Config.h"
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include "../core/Version.h"

namespace {
const char* presence(bool enabled, bool detected) {
  return !enabled ? "DISABLED" : detected ? "DETECTED" : "NOT DETECTED";
}
}
void SerialReporter::begin() {
  Console::line("ESP32 MAIN: STARTING");
  Console::format("[BOOT] resetReason=%u (1=power-on 4=panic 5/6/7=watchdog 9=brownout)",unsigned(esp_reset_reason()));
  if (Config::USE_HOME_WIFI) {
    Console::line("Wi-Fi mode: HOME WI-FI");
    Console::format("Wi-Fi: %s", Config::HOME_WIFI_SSID);
  } else {
    Console::line("Wi-Fi mode: VEST HOTSPOT");
    Console::format("Wi-Fi: %s", Config::WIFI_NAME);
  }
}
void SerialReporter::tick(const StateStore&, const SIM800LService& gsm, const NEO6MService& gps,
                         const BatteryService& power, const SensorLink& sensor, const EmergencyCoordinator&,
                         const RuntimeTelemetry& runtime) {
  if (!Config::ENABLE_USB_DEBUG) return;
  const uint32_t now = millis();
  if (uint32_t(now-lastCheck) < 1000) return;
  lastCheck = now;
  static uint32_t lastHealthReport=0;
  if(uint32_t(now-lastHealthReport)>=30000){
    lastHealthReport=now;
    Console::format("[NET] fw=%s upMs=%lu clientsNow=%u/%u APstarts=%lu APstops=%lu joinEvents=%lu leaveEvents=%lu",
      Firmware::VERSION,(unsigned long)now,unsigned(runtime.clients),unsigned(Config::MAX_WIFI_USERS),(unsigned long)runtime.apStarts,
      (unsigned long)runtime.apStops,(unsigned long)runtime.clientJoins,(unsigned long)runtime.clientLeaves);
    const auto& r=runtime.radio;
    Console::format("[RADIO] ap=%u readable=%u visible=%u config=%u dhcp=%u ch=%u proto=0x%x limit=%u sdk=%ld recoveries=%lu service=%lu authFails=%lu",
      unsigned(runtime.hotspot),unsigned(r.readable),unsigned(r.visible),unsigned(r.configured),unsigned(r.dhcp),
      unsigned(r.channel),unsigned(r.protocol),unsigned(r.limit),(long)r.error,(unsigned long)r.recoveryAttempts,(unsigned long)r.serviceRecoveries,(unsigned long)r.authFailures);
    Console::format("[RADIO EVENTS] lastStartMs=%lu lastStopMs=%lu lastLeaveReason=%u txLimitQdBm=%d",
      (unsigned long)r.lastStartMs,(unsigned long)r.lastStopMs,unsigned(r.disconnectReason),int(r.txPowerQuarterDbm));
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    Console::format("[MEM] byteFree=%lu byteMinimum=%lu byteLargest=%lu",
      (unsigned long)heap_caps_get_free_size(caps),(unsigned long)heap_caps_get_minimum_free_size(caps),
      (unsigned long)heap_caps_get_largest_free_block(caps));
    Console::format("[HTTP] served=%lu expired=%lu rejected=%lu maxMs=%lu snapshotAgeMs=%lu",
      (unsigned long)runtime.httpRequests,(unsigned long)runtime.httpExpired,
      (unsigned long)runtime.httpRejected,(unsigned long)runtime.maxHttpMs,
      (unsigned long)(now-runtime.capturedAt));
    Console::format("[HTTP WRITE] active=%u completed=%lu aborted=%lu maxFlashMs=%lu",
      unsigned(runtime.httpSending),(unsigned long)runtime.httpFlashCompleted,
      (unsigned long)runtime.httpWriteAborts,(unsigned long)runtime.maxFlashMs);
    Console::format("[HTTP MEMORY] requestFails=%lu responseFails=%lu docFails=%lu outputFails=%lu lengthFails=%lu queued=%lu peakQueued=%lu",
      (unsigned long)runtime.httpRequestMemoryFailures,(unsigned long)runtime.httpResponseMemoryFailures,
      (unsigned long)runtime.httpDocumentFailures,(unsigned long)runtime.httpOutputFailures,(unsigned long)runtime.httpLengthFailures,
      (unsigned long)runtime.httpQueuedBytes,(unsigned long)runtime.httpMaxQueuedBytes);
  }
  const auto position = gps.telemetry();
  const auto sensing = sensor.telemetry();
  const auto energy = power.telemetry();
  const uint32_t health = uint32_t(gsm.online()) | uint32_t(gsm.simDetected()) << 1 |
      uint32_t(position.decoding) << 2 | uint32_t(sensing.online) << 3 |
      uint32_t(energy.responding) << 4 | uint32_t(runtime.networkOnline) << 5 |
      uint32_t(runtime.cameras[0].online) << 6 | uint32_t(runtime.cameras[1].online) << 7 |
      uint32_t(sensing.imu) << 8 | uint32_t(sensing.piezo) << 9 |
      uint32_t(runtime.webReady) << 10 | uint32_t(runtime.mdns) << 11;
  const bool first = previousHealth == UINT32_MAX;
  const uint32_t changed = health ^ previousHealth;
  previousHealth = health;
  if (first || (changed & ((1U<<5)|(1U<<10)|(1U<<11))) || std::strcmp(previousIp, runtime.ip) != 0) {
    std::snprintf(previousIp, sizeof(previousIp), "%s", runtime.ip);
    Console::format("Wi-Fi: %s", runtime.networkOnline ? "CONNECTED" : "NOT CONNECTED");
    if (runtime.networkOnline && runtime.webReady && runtime.ip[0]) {
      Console::format("Dashboard: http://%s/", runtime.ip);
      if (runtime.mdns) Console::format("Local name: http://%s.local/", Config::LOCAL_HOSTNAME);
    }
  }
  if (first || (changed & 1U)) Console::format("SIM800L: %s", presence(Config::ENABLE_SIM800L, gsm.online()));
  if (first || (changed & 2U)) Console::format("SIM CARD: %s", presence(Config::ENABLE_SIM800L, gsm.simDetected()));
  if (first || (changed & 4U)) Console::format("NEO6M: %s", presence(Config::ENABLE_NEO6M, position.decoding));
  if (first || (changed & 8U)) Console::format("SENSING BOARD: %s", presence(Config::ENABLE_SENSOR_LINK, sensing.online));
  if (first || (changed & 16U)) Console::format("%s: %s","INA226 via Nano/Sensing",presence(true, energy.responding));
  if (first || (changed & 64U)) Console::format("FRONT CAMERA: %s", presence(true, runtime.cameras[0].online));
  if (first || (changed & 128U)) Console::format("REAR CAMERA: %s", presence(true, runtime.cameras[1].online));
  if (first || (changed & 256U)) Console::format("IMU: %s", presence(Config::ENABLE_SENSOR_LINK, sensing.imu));
  if (first || (changed & 512U)) Console::format("PIEZO: %s", !Config::ENABLE_SENSOR_LINK ? "DISABLED" : sensing.piezo ? "CONFIGURED" : "NOT CONFIGURED");
}
