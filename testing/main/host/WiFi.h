#pragma once
#include "Arduino.h"
#include <functional>
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };
constexpr int WL_DISCONNECTED=6, WL_CONNECTED=3, WIFI_AUTH_WPA2_PSK=3;
enum WiFiEvent_t {ARDUINO_EVENT_WIFI_AP_START,ARDUINO_EVENT_WIFI_AP_STOP,ARDUINO_EVENT_WIFI_AP_STACONNECTED,ARDUINO_EVENT_WIFI_AP_STADISCONNECTED};
struct WiFiEventInfo_t {struct {uint16_t reason=0;} wifi_ap_stadisconnected;};
constexpr int WIFI_BW_HT20=20;
constexpr uint8_t WIFI_PROTOCOL_11B=1,WIFI_PROTOCOL_11G=2,WIFI_PROTOCOL_11N=4;
constexpr int WIFI_IF_AP=1,WIFI_CIPHER_TYPE_CCMP=4;
struct wifi_config_t {struct {uint8_t ssid[32]{},password[64]{};uint8_t channel=6,ssid_len=0,ssid_hidden=0,max_connection=6;int authmode=WIFI_AUTH_WPA2_PSK,pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;} ap;};
struct MockWiFi {
 std::function<void(WiFiEvent_t,WiFiEventInfo_t)> handler;
 unsigned eventRegistrations=0;bool eventsOK=true;
 unsigned onEvent(decltype(handler) h){++eventRegistrations;if(!eventsOK)return 0;handler=h;return eventRegistrations;}
 void event(WiFiEvent_t e,uint16_t reason=0){WiFiEventInfo_t info;info.wifi_ap_stadisconnected.reason=reason;if(handler)handler(e,info);}
 wifi_mode_t getMode()const{return selected;}
 wifi_mode_t selected=WIFI_OFF; int state=WL_DISCONNECTED;
 bool ap=false, modeOK=true, apOK=true, autoReconnect=true, persistentEnabled=true;
 bool radioReadOK=true,protocolOK=true,dhcpReadOK=true,dhcpRunning=true;
 uint8_t protocol=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N;
 wifi_config_t radioConfig;
 int minSecurity=0, begins=0, disconnects=0, apCreates=0, modeCalls=0, clients=2; String hostname;
 IPAddress staIp, apIp{192,168,4,1};
 struct APType {bool up=false;void* netif(){return up?this:nullptr;}int bandwidthValue=0;bool started()const{return up;}bool bandwidth(int b){bandwidthValue=b;return true;}int captiveStarts=0;bool enableDhcpCaptivePortal(){++captiveStarts;return true;} } AP;
 void persistent(bool enabled){persistentEnabled=enabled;}
 bool mode(wifi_mode_t m){++modeCalls;selected=m;ap=false;AP.up=m==WIFI_AP;if(m==WIFI_OFF){state=WL_DISCONNECTED;event(ARDUINO_EVENT_WIFI_AP_STOP);}else if(m==WIFI_AP)event(ARDUINO_EVENT_WIFI_AP_START);return modeOK;}
 bool setHostname(const char*s){hostname=s; return selected==WIFI_OFF;}
 void setSleep(bool){} void setAutoReconnect(bool enabled){autoReconnect=enabled;}
 void setMinSecurity(int value){minSecurity=value;}
 bool softAPConfig(IPAddress,IPAddress,IPAddress,IPAddress,IPAddress){dhcpRunning=true;return apOK;}
 bool softAP(const char* ssid,const char* pass,uint8_t channel,bool hidden,uint8_t limit){++apCreates;ap=apOK;radioConfig={};radioConfig.ap.ssid_len=uint8_t(std::strlen(ssid));std::memcpy(radioConfig.ap.ssid,ssid,std::strlen(ssid));std::memcpy(radioConfig.ap.password,pass,std::strlen(pass));radioConfig.ap.channel=channel;radioConfig.ap.ssid_hidden=hidden;radioConfig.ap.max_connection=limit;return apOK;}
 void begin(const char*,const char*){++begins; state=WL_DISCONNECTED;}
 void disconnect(bool,bool){++disconnects;state=WL_DISCONNECTED;staIp=IPAddress();}
 int status()const{return state;} IPAddress localIP()const{return staIp;}
 int RSSI()const{return -55;}
  unsigned stationListQueries=0;
  IPAddress softAPIP()const{return apIp;} uint8_t softAPgetStationNum(){++stationListQueries;return clients;}
};
inline MockWiFi WiFi;
