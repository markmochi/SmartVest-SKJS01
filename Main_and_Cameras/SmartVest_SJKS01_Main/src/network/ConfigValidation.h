#pragma once
// Compile-time validation only. Settings are all in the root Config.h.
namespace ConfigValidation {
static_assert(Config::RIDER_ACCOUNT_RESET_REVISION == 0 || Config::ALLOW_DEFAULT_RIDER_LOGIN,
              "Rider account reset requires ALLOW_DEFAULT_RIDER_LOGIN");
constexpr bool configStringEqual(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
constexpr bool validHostname(const char* s) {
  const size_t n = sizeof(Config::LOCAL_HOSTNAME) - 1;
  if (!n || n > 32 || s[0] == '-' || s[n-1] == '-') return false;
  for (size_t i=0; i<n; ++i)
    if (!((s[i]>='a' && s[i]<='z') || (s[i]>='0' && s[i]<='9') || s[i]=='-'))
      return false;
  return true;
}
static_assert(!Config::NETWORK_USES_STATION || (sizeof(Config::HOME_WIFI_SSID)>1 && sizeof(Config::HOME_WIFI_SSID)<=33),
              "Set HOME_WIFI_SSID (1..32 bytes) in Config.h");
static_assert(!Config::NETWORK_USES_STATION || (sizeof(Config::HOME_WIFI_PASSWORD)>=9 && sizeof(Config::HOME_WIFI_PASSWORD)<=64),
              "Set HOME_WIFI_PASSWORD (8..63 bytes); open/WEP networks are not supported");
static_assert(!Config::NETWORK_USES_AP || (sizeof(Config::WIFI_NAME)>1 && sizeof(Config::WIFI_NAME)<=33 &&
              sizeof(Config::WIFI_PASSWORD)>=9 && sizeof(Config::WIFI_PASSWORD)<=64),
              "Vest hotspot needs SSID 1..32 bytes and password 8..63 bytes");
static_assert(Config::WIFI_CHANNEL >= 1 && Config::WIFI_CHANNEL <= 11, "WIFI_CHANNEL must be 1..11");
static_assert(Config::MAX_WIFI_USERS >= 1 && Config::MAX_WIFI_USERS <= 10, "MAX_WIFI_USERS must be 1..10");
static_assert(validHostname(Config::LOCAL_HOSTNAME), "LOCAL_HOSTNAME: 1..32 lowercase letters/digits/hyphens");
static_assert(Config::WIFI_CONNECT_TIMEOUT_MS >= 1000 && Config::WIFI_CONNECT_TIMEOUT_MS <= 120000,
              "WIFI_CONNECT_TIMEOUT_MS must be 1000..120000");
static_assert(Config::WIFI_RETRY_INITIAL_MS >= 1000 && Config::WIFI_RETRY_MAX_MS >= Config::WIFI_RETRY_INITIAL_MS &&
              Config::WIFI_RETRY_MAX_MS <= 600000 && Config::WIFI_MDNS_RETRY_MS >= 1000 && Config::WIFI_MDNS_RETRY_MS <= 600000,
              "Invalid Wi-Fi retry timing");

static_assert(!Config::USE_HOME_WIFI ||
                  (sizeof(Config::DEVELOPER_PASSWORD) >= 13 && sizeof(Config::DEVELOPER_PASSWORD) <= 65 &&
                   !configStringEqual(Config::DEVELOPER_PASSWORD, "smartvest-skjs") &&
                   !configStringEqual(Config::DEVELOPER_PASSWORD, "REPLACE_WITH_YOUR_UNIQUE_PASSWORD")),
              "Home mode: set your own DEVELOPER_PASSWORD (12..64 characters) in Config.h");
static_assert(!Config::ENABLE_USB_DEBUG || !Config::NEO6M_AUTO_BAUD,
              "Serial Monitor shares GPS UART1: disable NEO6M_AUTO_BAUD or USB debug");
static_assert(!Config::ENABLE_USB_DEBUG || !Config::ENABLE_NEO6M || Config::DEBUG_BAUD == Config::NEO6M_BAUD,
              "DEBUG_BAUD must equal NEO6M_BAUD when GPS and Serial Monitor are enabled");
static_assert(!Config::ENABLE_USB_DEBUG || Config::DEBUG_TX_PIN == 1,
              "USB Serial Monitor uses GPIO1 on the ESP32-WROOM development board");
static_assert(Config::RIDER_MIN_PASSWORD_LENGTH >= 8 && Config::RIDER_MIN_PASSWORD_LENGTH <= 64,
              "RIDER_MIN_PASSWORD_LENGTH must be 8..64");
static_assert(!Config::ENABLE_USB_DEBUG ||
              (Config::SENSOR_RX_PIN != 1 && Config::SENSOR_TX_PIN != 1 &&
               Config::SIM800L_RX_PIN != 1 && Config::SIM800L_TX_PIN != 1 &&
               Config::NEO6M_RX_PIN != 1 && Config::I2C_SDA_PIN != 1 && Config::I2C_SCL_PIN != 1),
              "GPIO1 is reserved for USB debug output");
constexpr bool inputPin(int pin) {
  return pin >= 0 && pin <= 39 && !(pin >= 6 && pin <= 11) && pin != 20 && pin != 24 && !(pin >= 28 && pin <= 31);
}
constexpr bool outputPin(int pin) { return inputPin(pin) && pin < 34; }
// Reserve the complete documented map even when a peripheral is disabled.
// GPIO1/3 belong to the board USB bridge; GPS TX5 and RGB are reservations.
// This prevents a disabled feature from silently lending its pins to UART.
constexpr int reservedPinMap[] = {
  Config::SIM800L_RX_PIN, Config::SIM800L_TX_PIN,
  Config::NEO6M_RX_PIN, Config::NEO6M_TX_PIN,
  Config::I2C_SDA_PIN, Config::I2C_SCL_PIN,
  Config::SENSOR_RX_PIN, Config::SENSOR_TX_PIN,
  Config::LED_RED_PIN, Config::LED_GREEN_PIN, Config::LED_BLUE_PIN,
  Config::DEBUG_TX_PIN, 3
};
constexpr bool uniqueReservedPins() {
  for (size_t i = 0; i < sizeof(reservedPinMap)/sizeof(reservedPinMap[0]); ++i)
    for (size_t j = i + 1; j < sizeof(reservedPinMap)/sizeof(reservedPinMap[0]); ++j)
      if (reservedPinMap[i] == reservedPinMap[j]) return false;
  return true;
}
constexpr bool profileIoPin(int p) {
  // Exclude USB, flash, unavailable module pins and strap pins from active IO.
  return p == 4 || p == 13 || p == 14 || (p >= 16 && p <= 19) ||
         (p >= 21 && p <= 23) || (p >= 25 && p <= 27) || p == 32 || p == 33;
}
static_assert(uniqueReservedPins(),
              "GPIO collision in main active/reserved map: GSM/GPS/sensing/I2C/RGB/USB");
static_assert(profileIoPin(Config::SIM800L_RX_PIN) && profileIoPin(Config::SIM800L_TX_PIN) &&
              profileIoPin(Config::NEO6M_RX_PIN) &&
              profileIoPin(Config::SENSOR_RX_PIN) && profileIoPin(Config::SENSOR_TX_PIN) &&
              profileIoPin(Config::I2C_SDA_PIN) && profileIoPin(Config::I2C_SCL_PIN) &&
              profileIoPin(Config::LED_RED_PIN) && profileIoPin(Config::LED_GREEN_PIN) &&
              profileIoPin(Config::LED_BLUE_PIN),
              "Main pin profile: review USB/flash/strap/input-only pin restrictions");
static_assert(Config::NEO6M_TX_PIN == 5 && Config::DEBUG_TX_PIN == 1,
              "Pin profile reserves GPS TX5 (undriven) and USB debug TX1");
static_assert(Config::SENSOR_BAUD == 115200,
              "Paired Sensing firmware requires UART 115200 baud");
static_assert(!Config::ENABLE_SIM800L || (inputPin(Config::SIM800L_RX_PIN) && outputPin(Config::SIM800L_TX_PIN) && Config::SIM800L_RX_PIN != Config::SIM800L_TX_PIN),
              "SIM800L needs distinct valid RX/TX GPIO pins on ESP32-WROOM");
static_assert(!Config::ENABLE_SENSOR_LINK || (inputPin(Config::SENSOR_RX_PIN) && outputPin(Config::SENSOR_TX_PIN) && Config::SENSOR_RX_PIN != Config::SENSOR_TX_PIN),
              "Sensing link needs distinct valid RX/TX GPIO pins on ESP32-WROOM");
static_assert(!Config::ENABLE_NEO6M || inputPin(Config::NEO6M_RX_PIN), "GPS RX must be a valid ESP32-WROOM GPIO input");
constexpr bool pinsOverlap(int a, int b, int c, int d) { return a == c || a == d || b == c || b == d; }
static_assert(!(Config::ENABLE_SENSOR_LINK && Config::ENABLE_SIM800L) || !pinsOverlap(Config::SENSOR_RX_PIN, Config::SENSOR_TX_PIN, Config::SIM800L_RX_PIN, Config::SIM800L_TX_PIN),
              "Sensing and GSM cannot share GPIO pins");
static_assert(!(Config::ENABLE_NEO6M && Config::ENABLE_SENSOR_LINK) || (Config::NEO6M_RX_PIN != Config::SENSOR_RX_PIN && Config::NEO6M_RX_PIN != Config::SENSOR_TX_PIN),
              "GPS and sensing cannot share GPIO pins");
static_assert(!(Config::ENABLE_NEO6M && Config::ENABLE_SIM800L) || (Config::NEO6M_RX_PIN != Config::SIM800L_RX_PIN && Config::NEO6M_RX_PIN != Config::SIM800L_TX_PIN),
              "GPS and GSM cannot share GPIO pins");
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static_assert(!Config::ENABLE_SENSOR_LINK,
              "Set Arduino Tools > Core Debug Level to None: UART0 carries the sensing protocol");
#endif
} // namespace ConfigValidation

static_assert(Config::SIM800L_SIM_POLL_MS >= 1000 && Config::SIM800L_SIM_STALE_MS >= Config::SIM800L_SIM_POLL_MS * 2,
              "SIM poll must be >=1s and SIM expiry must allow two polls");

static_assert(Config::BATTERY_CRITICAL_BLINK_MS > 0 && Config::BATTERY_CHARGING_BLINK_MS > 0,
              "Battery LED blink half-periods must be positive");
