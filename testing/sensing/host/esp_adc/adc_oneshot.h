#pragma once
#include "Arduino.h"
#include <array>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_RESPONSE = 0x108, ESP_ERR_TIMEOUT = 0x107;
enum adc_unit_t { ADC_UNIT_1, ADC_UNIT_2 };
enum adc_channel_t { ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
                     ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7 };
enum adc_ulp_mode_t { ADC_ULP_MODE_DISABLE };
enum adc_bitwidth_t { ADC_BITWIDTH_12 = 12 };
enum adc_atten_t { ADC_ATTEN_DB_12 = 3 };
struct adc_oneshot_unit_init_cfg_t { adc_unit_t unit_id; int clk_src; adc_ulp_mode_t ulp_mode; };
struct adc_oneshot_chan_cfg_t { adc_atten_t atten; adc_bitwidth_t bitwidth; };
struct AdcContext {};
using adc_oneshot_unit_handle_t = AdcContext*;
namespace FakeAdc {
inline unsigned allocations = 0, deletions = 0, reads = 0;
inline esp_err_t allocationError = ESP_OK, mappingError = ESP_OK, configError = ESP_OK;
inline std::array<esp_err_t, 8> readError = {};
inline std::array<int, 8> raw = {};
inline void reset() { allocations = deletions = reads = 0; allocationError = mappingError = configError = ESP_OK; readError.fill(ESP_OK); raw.fill(0); }
}
inline esp_err_t adc_oneshot_io_to_channel(int pin, adc_unit_t* unit, adc_channel_t* channel) {
  if (FakeAdc::mappingError) return FakeAdc::mappingError;
  *unit = ADC_UNIT_1;
  switch (pin) {
    case 36: *channel = ADC_CHANNEL_0; break;
    case 39: *channel = ADC_CHANNEL_3; break;
    case 32: *channel = ADC_CHANNEL_4; break;
    case 33: *channel = ADC_CHANNEL_5; break;
    case 34: *channel = ADC_CHANNEL_6; break;
    case 35: *channel = ADC_CHANNEL_7; break;
    default: return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}
inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t* config, adc_oneshot_unit_handle_t* result) {
  ++FakeAdc::allocations;
  if (FakeAdc::allocationError) return FakeAdc::allocationError;
  if (config->unit_id != ADC_UNIT_1) return ESP_ERR_INVALID_ARG;
  *result = new AdcContext; return ESP_OK;
}
inline esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle) { ++FakeAdc::deletions; delete handle; return ESP_OK; }
inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t* config) {
  if (config->bitwidth != ADC_BITWIDTH_12 || config->atten != ADC_ATTEN_DB_12) return ESP_ERR_INVALID_ARG;
  return FakeAdc::configError;
}
inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t channel, int* output) {
  ++FakeAdc::reads; *output = FakeAdc::raw[channel]; return FakeAdc::readError[channel];
}
