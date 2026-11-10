#pragma once
#include "WiFi.h"
constexpr int ESP_OK=0,ESP_FAIL=-1;
inline int esp_wifi_get_max_tx_power(int8_t* power){if(!WiFi.radioReadOK)return ESP_FAIL;*power=78;return ESP_OK;}
inline int esp_wifi_set_protocol(int,uint8_t protocol){if(!WiFi.protocolOK)return ESP_FAIL;WiFi.protocol=protocol;return ESP_OK;}
inline int esp_wifi_get_config(int,wifi_config_t* config){if(!WiFi.radioReadOK)return ESP_FAIL;*config=WiFi.radioConfig;return ESP_OK;}
inline int esp_wifi_get_protocol(int,uint8_t* protocol){if(!WiFi.radioReadOK)return ESP_FAIL;*protocol=WiFi.protocol;return ESP_OK;}
