#include "NetworkService.h"
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <cstring>
#include "../../Config.h"
#include "NetworkPolicy.h"

const char* NetworkService::modeName() const {
  return Config::USE_HOME_WIFI ? "home_wifi_only" : "vest_hotspot";
}
bool NetworkService::stationConnected() const {
  return Config::NETWORK_USES_STATION && WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0,0,0,0);
}
String NetworkService::networkName() const {
  return Config::NETWORK_USES_STATION ? String(Config::HOME_WIFI_SSID) : String(Config::WIFI_NAME);
}
uint8_t NetworkService::hotspotClients() const {
  return apActive() ? WiFi.softAPgetStationNum() : 0;
}
String NetworkService::dashboardUrl() const {
  if (stationConnected()) return String("http://") + WiFi.localIP().toString() + "/";
  if (apActive()) return String("http://") + WiFi.softAPIP().toString() + "/";
  return "";
}
bool NetworkService::allowedHost(const String& host) const {
  const String ap = apStarted ? WiFi.softAPIP().toString() : String();
  const String sta = stationConnected() ? WiFi.localIP().toString() : String();
  return NetworkPolicy::allowedHost(host.c_str(), Config::LOCAL_HOSTNAME,
                                   ap.c_str(), sta.c_str());
}
bool NetworkService::validOrigin(const String& origin, const String& host) const {
  return allowedHost(host) && NetworkPolicy::sameOrigin(origin.c_str(), host.c_str());
}
void NetworkService::refreshMdns(bool force) {
  if (!Config::ENABLE_MDNS) return;
  if (force && mdnsStarted) { MDNS.end(); mdnsStarted = false; }
  if (!apActive() && !stationConnected()) return;
  if (mdnsStarted) return;
  if (!force && uint32_t(millis()-mdnsAttemptAt) < Config::WIFI_MDNS_RETRY_MS) return;
  mdnsAttemptAt = millis();
  mdnsStarted = MDNS.begin(Config::LOCAL_HOSTNAME);
  if (mdnsStarted) MDNS.addService("http", "tcp", 80);
}
void NetworkService::startStationAttempt() {
  ++connectionAttempts;
  attemptAt = millis();
  attemptActive = true;
  WiFi.begin(Config::HOME_WIFI_SSID, Config::HOME_WIFI_PASSWORD);
}
bool NetworkService::begin() {
#if defined(ARDUINO_ARCH_ESP32)
  if(!eventsRegistered)eventsRegistered=WiFi.onEvent([this](WiFiEvent_t event,WiFiEventInfo_t info) {
    // Event task: atomic counters only, no HTTP, Serial or state mutex.
    if(event==ARDUINO_EVENT_WIFI_AP_START){apRadioUp.store(true);lastStart.store(millis());++starts;}
    else if(event==ARDUINO_EVENT_WIFI_AP_STOP){apRadioUp.store(false);lastStop.store(millis());++stops;}
    else if(event==ARDUINO_EVENT_WIFI_AP_STACONNECTED)++joins;
    else if(event==ARDUINO_EVENT_WIFI_AP_STADISCONNECTED){
      const uint16_t reason=info.wifi_ap_stadisconnected.reason;leaveReason.store(reason);++leaves;
      if(reason==2||reason==15||reason==16||reason==202||reason==203||reason==204)++authFaults;
    }
  })!=0;
  if(!eventsRegistered)return false;
#endif
  WiFi.persistent(false); // Home credentials stay in the uploaded firmware.
  WiFi.mode(WIFI_OFF);
  if (!WiFi.setHostname(Config::LOCAL_HOSTNAME)) { return false; }
  if (Config::NETWORK_USES_STATION && !WiFi.mode(WIFI_STA)) return false;
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false); // This service controls bounded retry/backoff.
  if (Config::NETWORK_USES_STATION) WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);

  if (Config::NETWORK_USES_AP) {
    apRetryDelay = Config::WIFI_RETRY_INITIAL_MS;
    if (!startHotspot()) {
      // Keep HTTP/task initialization alive so a transient boot failure can
      // recover. A failed AP must not permanently disable the dashboard task.
      apFault = true;apFaultAt = apRetryAt = millis();
    }
  }
  retryDelay = Config::WIFI_RETRY_INITIAL_MS;
  if (Config::NETWORK_USES_STATION) startStationAttempt();
  // Station association/DHCP happens asynchronously. No waiting loop here.
  return true;
}
void NetworkService::startDns() {
  dnsRetryAt = millis();
  if (!(Config::HOTSPOT_LOCAL_DEVICE_MODE || Config::ENABLE_CAPTIVE_PORTAL)) return;
  dnsStarted = dns.start(53, WiFi.softAPIP());
  localModeStarted = dnsStarted && Config::HOTSPOT_LOCAL_DEVICE_MODE;
  // Use legacy HTTP probes for sign-in. Arduino's DHCP option 114 helper
  // advertises an HTML root instead of a CAPPORT JSON API and stops/restarts
  // DHCP after AP startup. Avoid that extra failure path during association.
}
bool NetworkService::startHotspot() {
  if (dnsStarted) dns.stop();
  dnsStarted = localModeStarted = false;
  if (mdnsStarted) { MDNS.end();mdnsStarted = false; }
  apStarted = false;apRadioUp.store(false);
  const uint32_t recoveries=radio.recoveryAttempts;
  radio=ApState{};radio.recoveryAttempts=recoveries;
  // Startup, readback faults or the bounded service-silence watchdog reach here.
  // Ordinary phone leave/no-Internet/idle events alone never restart the AP.
  const IPAddress ip(192,168,4,1), mask(255,255,255,0), first(192,168,4,2);
  if (!WiFi.mode(WIFI_OFF) || !WiFi.mode(WIFI_AP) ||
      !WiFi.softAPConfig(ip, ip, mask, first, ip) ||
      !WiFi.softAP(Config::WIFI_NAME, Config::WIFI_PASSWORD,
                   Config::WIFI_CHANNEL, false, Config::MAX_WIFI_USERS) ||
      !WiFi.AP.bandwidth(WIFI_BW_HT20)) {
    lastError = "Hotspot startup failed; retrying automatically.";
    return false;
  }
  radio.error=esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
      (Config::WIFI_AP_ALLOW_11N ? WIFI_PROTOCOL_11N : 0));
  if(radio.error!=ESP_OK){lastError="Hotspot protocol setup failed; retrying automatically.";return false;}
  WiFi.setSleep(false);
  apStarted = true;apRadioUp.store(true);
  service.restarted(millis());
  inspectRadio();radioCheckAt=millis();
  if(!radio.configured || !radio.dhcp){
    apRadioUp.store(false);lastError="Hotspot readback or DHCP failed; retrying automatically.";return false;
  }
  startDns();refreshMdns(true);
  lastError = dnsStarted || !Config::ENABLE_CAPTIVE_PORTAL ? "" : "Hotspot DNS unavailable; retrying.";
  return true;
}
ApState NetworkService::radioState() const {
  ApState result=radio;
  result.lastStartMs=lastStart.load();result.lastStopMs=lastStop.load();
  result.disconnectReason=leaveReason.load();result.authFailures=authFaults.load();
  result.serviceRecoveries=service.recoveries;return result;
}
void NetworkService::inspectRadio() {
  radio.txPowerQuarterDbm=-128;
  int8_t power=0;
  if(esp_wifi_get_max_tx_power(&power)==ESP_OK)radio.txPowerQuarterDbm=power;
  wifi_config_t config{};
  uint8_t protocol=0;esp_netif_dhcp_status_t dhcp=ESP_NETIF_DHCP_INIT;
  int32_t error=esp_wifi_get_config(WIFI_IF_AP,&config);
  if(error==ESP_OK)error=esp_wifi_get_protocol(WIFI_IF_AP,&protocol);
  if(error==ESP_OK)error=esp_netif_dhcps_get_status(WiFi.AP.netif(),&dhcp);
  radio.error=error;radio.readable=error==ESP_OK;
  radio.channel=config.ap.channel;radio.protocol=protocol;radio.limit=config.ap.max_connection;
  radio.visible=radio.readable && !config.ap.ssid_hidden;
  radio.dhcp=radio.readable && dhcp==ESP_NETIF_DHCP_STARTED;
  const uint8_t expected=WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
      (Config::WIFI_AP_ALLOW_11N ? WIFI_PROTOCOL_11N : 0);
  const size_t length=sizeof(Config::WIFI_NAME)-1;
  radio.configured=radio.visible && config.ap.ssid_len==length &&
      !std::memcmp(config.ap.ssid,Config::WIFI_NAME,length) &&
      !std::memcmp(config.ap.password,Config::WIFI_PASSWORD,sizeof(Config::WIFI_PASSWORD)) &&
      config.ap.channel==Config::WIFI_CHANNEL && config.ap.max_connection==Config::MAX_WIFI_USERS &&
      config.ap.authmode==WIFI_AUTH_WPA2_PSK && config.ap.pairwise_cipher==WIFI_CIPHER_TYPE_CCMP &&
      protocol==expected;
}
void NetworkService::maintainHotspot(uint32_t now) {
  const bool started = apStarted && WiFi.getMode() == WIFI_AP &&
      WiFi.AP.started() && WiFi.softAPIP() == IPAddress(192,168,4,1);
  if(!started)radio.readable=radio.visible=radio.configured=radio.dhcp=false;
  if(started && uint32_t(now-radioCheckAt)>=1000){inspectRadio();radioCheckAt=now;}
  const bool healthy=started && radio.configured && radio.dhcp;
  if (healthy) {
    if(service.recover(now,authFaults.load())){
      ++radio.recoveryAttempts;
      if(!startHotspot()){apFault=true;apFaultAt=apRetryAt=now;}
      return;
    }
    apRadioUp.store(true);apFault = false;
    apRetryDelay = Config::WIFI_RETRY_INITIAL_MS;
    if (!dnsStarted && uint32_t(now-dnsRetryAt) >= Config::WIFI_RETRY_INITIAL_MS) {
      startDns();
      if (dnsStarted) lastError = "";
    }
    refreshMdns();return;
  }
  apRadioUp.store(false);
  if (!apFault) { apFault = true;apFaultAt = apRetryAt = now; }
  // Debounce asynchronous START/STOP delivery, then retry at 5..60 seconds.
  // All arithmetic remains valid through millis() rollover.
  if (uint32_t(now-apFaultAt) < 2000 || uint32_t(now-apRetryAt) < apRetryDelay) return;
  apRetryAt = now;
  ++radio.recoveryAttempts;
  if (startHotspot()) { apFault = false;apRetryDelay = Config::WIFI_RETRY_INITIAL_MS; }
  else apRetryDelay = apRetryDelay >= Config::WIFI_RETRY_MAX_MS/2
      ? Config::WIFI_RETRY_MAX_MS : apRetryDelay*2;
}
void NetworkService::tick() {
  if (dnsStarted) dns.processNextRequest();
  if (!Config::NETWORK_USES_STATION) { maintainHotspot(millis()); return; }
  const uint32_t now = millis();
  if (stationConnected()) {
    const String ip = WiFi.localIP().toString();
    if (!wasConnected || ip != lastStationIp) {
      lastStationIp = ip;
      refreshMdns(true);
    }
    lastError = "";
    wasConnected = true;
    attemptActive = false;
    retryDelay = Config::WIFI_RETRY_INITIAL_MS;
  } else {
    if (wasConnected) {
      lastError = "Home Wi-Fi connection lost; reconnecting.";
      wasConnected = false;
      lastStationIp = "";
      retryAt = now;
      attemptActive = false;
      if (mdnsStarted) { MDNS.end(); mdnsStarted = false; }
      refreshMdns(true);
    }
    if (attemptActive && uint32_t(now-attemptAt) >= Config::WIFI_CONNECT_TIMEOUT_MS) {
      lastError = "Wi-Fi connection timed out. Check the configured 2.4 GHz network and credentials.";
      WiFi.disconnect(false, false); // Keep selected mode and hotspot, if any.
      attemptActive = false;
      retryAt = now;
    } else if (!attemptActive && uint32_t(now-retryAt) >= retryDelay) {
      startStationAttempt();
      retryDelay = retryDelay >= Config::WIFI_RETRY_MAX_MS/2
          ? Config::WIFI_RETRY_MAX_MS : retryDelay*2;
    }
  }
  refreshMdns();
}

String NetworkService::localIp() const {
  if (stationConnected()) return WiFi.localIP().toString();
  if (apActive()) return WiFi.softAPIP().toString();
  return "";
}
int NetworkService::signalDbm() const { return stationConnected() ? WiFi.RSSI() : 0; }
uint32_t NetworkService::retryInMs() const {
  if (!Config::NETWORK_USES_STATION || stationConnected() || attemptActive) return 0;
  const uint32_t age = uint32_t(millis()-retryAt);
  return age < retryDelay ? retryDelay-age : 0;
}
