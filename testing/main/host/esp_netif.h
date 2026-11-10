#pragma once
#include "esp_wifi.h"
enum esp_netif_dhcp_status_t {ESP_NETIF_DHCP_INIT,ESP_NETIF_DHCP_STARTED,ESP_NETIF_DHCP_STOPPED};
inline int esp_netif_dhcps_get_status(void* netif,esp_netif_dhcp_status_t* status){if(!netif||!WiFi.dhcpReadOK)return ESP_FAIL;*status=WiFi.dhcpRunning?ESP_NETIF_DHCP_STARTED:ESP_NETIF_DHCP_STOPPED;return ESP_OK;}
