#pragma once
#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32)
#error "This pin profile targets classic ESP32-WROOM-32, not ESP32-S2/S3/C3."
#endif
#if defined(BOARD_HAS_PSRAM)
#error "This WROOM-32 profile uses GPIO16/17 for UART. Select PSRAM Disabled."
#endif

#ifndef SV_UART_MAPPING_CONFIRMED
#define SV_UART_MAPPING_CONFIRMED 1
#endif
#ifndef SV_IMU_MAPPING_CONFIRMED
#define SV_IMU_MAPPING_CONFIRMED 1
#endif
#ifndef SV_PIEZO_MAPPING_CONFIRMED
#define SV_PIEZO_MAPPING_CONFIRMED 1
#endif
#ifndef SV_PIEZO_CIRCUIT_VERIFIED
#define SV_PIEZO_CIRCUIT_VERIFIED 1
#endif
#ifndef SV_ENABLE_SYSTEM_TEST
#define SV_ENABLE_SYSTEM_TEST 0
#endif

namespace SensorConfig {
constexpr bool UART_MAPPING_CONFIRMED = SV_UART_MAPPING_CONFIRMED;
constexpr bool IMU_MAPPING_CONFIRMED = SV_IMU_MAPPING_CONFIRMED;
constexpr bool ENABLE_SYSTEM_UART_TEST = SV_ENABLE_SYSTEM_TEST;
#ifndef SV_ARM_CRASH_DETECTION
#define SV_ARM_CRASH_DETECTION 1
#endif
// A current capability lease and valid calibration are required to arm.
constexpr bool ARM_CRASH_DETECTION = SV_ARM_CRASH_DETECTION;
constexpr bool PIEZO_IMPACT_DETECTION_ENABLED = true;
constexpr bool PIEZO_MAPPING_CONFIRMED = SV_PIEZO_MAPPING_CONFIRMED;
constexpr bool PIEZO_CIRCUIT_VERIFIED = SV_PIEZO_CIRCUIT_VERIFIED;
// Reserved ADC1 inputs. ONLY protected, conditioned group outputs.
constexpr int FRONT_PIEZO_PIN = 34, REAR_PIEZO_PIN = 35;
constexpr uint32_t PIEZO_SAMPLE_MS = 1, PIEZO_STALE_MS = 100;
constexpr uint32_t PIEZO_INIT_RETRY_MS = 5000;
constexpr uint16_t PIEZO_GOOD_PAIRS = 10;
constexpr uint32_t DEBUG_BAUD = 115200;
// LOCAL sensing pins: main GPIO27 -> sensing RX16; sensing TX17 -> main GPIO26.
// Main GPIO16/17 belong to SIM800L on that separate board.
constexpr int MAIN_RX_PIN = 16, MAIN_TX_PIN = 17;
constexpr uint32_t LINK_BAUD = 115200;
// Dedicated UART1 for Nano battery telemetry. TX33 reserved, left unconnected.
constexpr int BATTERY_RX_PIN=32, BATTERY_TX_PIN=33;
constexpr uint32_t BATTERY_BAUD=19200;
constexpr uint32_t HEARTBEAT_MS = 1000, EVENT_RETRY_MS = 1000;
constexpr size_t MAX_PENDING_EVENTS = 4;
constexpr int SPI_SCK_PIN = 18, SPI_MISO_PIN = 19, SPI_MOSI_PIN = 23, SPI_CS_PIN = 21;
constexpr uint32_t SPI_HZ = 100000;
constexpr uint16_t IMU_CHIP_ID = 0x0043;
constexpr uint32_t SAMPLE_MS = 5, IMU_STALE_MS = 100, IMU_RETRY_MS = 5000;
constexpr uint32_t SAMPLE_GAP_US = 7500;
constexpr uint16_t GYRO_CAL_SAMPLES = 240;
constexpr uint32_t CAL_MAX_GAP_MS = 25, CAL_TIMEOUT_MS = 2000;
// Classic WROOM only: no flash, strap or input-only pins for SPI outputs/UART.
constexpr bool allowedPin(int p) {
  return p == 4 || (p >= 13 && p <= 14) || (p >= 16 && p <= 19) ||
         (p >= 21 && p <= 23) || (p >= 25 && p <= 27) || p == 32 || p == 33;
}
constexpr int activePins[] = {MAIN_RX_PIN,MAIN_TX_PIN,SPI_SCK_PIN,SPI_MISO_PIN,SPI_MOSI_PIN,SPI_CS_PIN,FRONT_PIEZO_PIN,REAR_PIEZO_PIN,BATTERY_RX_PIN,BATTERY_TX_PIN};
constexpr bool uniquePins() { for(unsigned i=0;i<sizeof(activePins)/sizeof(activePins[0]);++i)for(unsigned j=i+1;j<sizeof(activePins)/sizeof(activePins[0]);++j)if(activePins[i]==activePins[j])return false;return true; }
static_assert(allowedPin(BATTERY_RX_PIN)&&allowedPin(BATTERY_TX_PIN),"Unsupported battery UART GPIO");
static_assert(BATTERY_BAUD==19200,"Nano battery UART requires 19200 baud");
static_assert(uniquePins(), "Sensing SPI/UART/piezo pin collision");
static_assert(allowedPin(MAIN_RX_PIN) && allowedPin(MAIN_TX_PIN) && allowedPin(SPI_SCK_PIN) && allowedPin(SPI_MISO_PIN) && allowedPin(SPI_MOSI_PIN) && allowedPin(SPI_CS_PIN), "Unsupported WROOM GPIO");
constexpr bool exposedAdc1Pin(int p) { return p==32 || p==33 || p==34 || p==35 || p==36 || p==39; }
static_assert(exposedAdc1Pin(FRONT_PIEZO_PIN) && exposedAdc1Pin(REAR_PIEZO_PIN), "Protected ADC1 inputs required");
static_assert(!PIEZO_MAPPING_CONFIRMED || PIEZO_CIRCUIT_VERIFIED,
              "Verify protected group circuits before enabling analog inputs");

static_assert(LINK_BAUD == 115200,
              "Paired Main firmware requires UART 115200 baud");
}  // namespace SensorConfig
