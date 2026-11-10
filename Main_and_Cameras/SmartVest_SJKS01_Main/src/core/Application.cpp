#include "Application.h"
#include "../power/BatteryLed.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "../../Config.h"
#include "../web/DashboardServer.h"
#include "../diagnostics/Console.h"
#include "../diagnostics/SerialReporter.h"

namespace {
SerialReporter reporter;
StateStore store;
SIM800LService gsm(store);
NEO6MService gps;
BatteryService power;
BatteryLed batteryLed;
EmergencyCoordinator emergency(store, gps);
SensorLink sensor(emergency);
DashboardServer dashboard(store, gsm, gps, power);
SemaphoreHandle_t stateMutex = nullptr;
bool hardwareReady = false, webReady = false;
void uiTask(void*) {
  for (;;) {
    if (webReady) dashboard.tick();
    vTaskDelay(pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);
  }
}
bool guard(void* context, const SmsJob& job) {
  return static_cast<EmergencyCoordinator*>(context)->submissionGuard(job);
}
}  // namespace
void Application::begin() {
  // Keep the hotspot unavailable until HTTP, saved state and its servicing
  // task are ready. A phone must not join during the startup-only delay.
  batteryLed.begin();
  if (Config::ENABLE_SENSOR_LINK) esp_log_level_set("*", ESP_LOG_NONE);
  if (Config::ENABLE_SENSOR_LINK) sensor.begin();
  gps.begin();
  Console::begin(gps.consolePort());
  if (Config::ENABLE_BATTERY_LED && !batteryLed.colorReady())
    Console::line("[LED] Color mix unavailable; orange degraded.");
  reporter.begin();
  stateMutex = xSemaphoreCreateMutex();
  if (!stateMutex) { Console::line("[MAIN] Startup failed: cannot allocate state mutex."); return; }
  if (!store.begin()) Console::line("[STORAGE] Saved settings could not be loaded; rider login stays unavailable.");
  emergency.begin();
  gsm.setSubmissionGuard(guard, &emergency);
  if (Config::ENABLE_SIM800L) gsm.begin();
  power.attachBattery(&sensor.batterySource());

  dashboard.attachRuntime(stateMutex, sensor, emergency);
  webReady = dashboard.begin();
  if (!webReady) Console::line("[WEB] Dashboard did not start. Other enabled modules continue running.");
  hardwareReady = true;
  // Lowest priority shares time with core0's idle task even if a socket read
  // spins, so a slow HTTP client cannot starve the idle watchdog indefinitely.
  if (webReady && xTaskCreatePinnedToCore(uiTask, "vest_dashboard", 16384,
                                        nullptr, 0, nullptr, 0) != pdPASS)
    { webReady = false; Console::line("[WEB] Could not allocate the dashboard task."); }
}
void Application::tick() {
  if (!hardwareReady) {
    Console::pump();
    delay(10);
    return;
  }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  dashboard.refreshIncidentContext();
  sensor.tick();
  power.setRemoteLink(sensor.telemetry().online);
  if (Config::ENABLE_NEO6M) gps.tick();
  if (Config::ENABLE_SIM800L) gsm.tick();
  emergency.tick();
  power.updateIndicator();
  if (Config::ENABLE_SIM800L) gsm.tick();
  emergency.tick();  // Observe final modem outcomes before history slots can be
                     // reused by UI.
  batteryLed.tick(power.ledOutputState(), power.ledTestBlue(), power.ledTestActive(), &emergency.crashLight);
  reporter.tick(store, gsm, gps, power, sensor, emergency, dashboard.runtimeTelemetry());
  xSemaphoreGive(stateMutex);
  Console::pump();
  delay(1);
}
