#include <cassert>
#include <iostream>
#include <limits>
#include "src/network/NetworkService.h"
#include "src/network/NetworkPolicy.h"
#include "Config.h"
#include "ESPmDNS.h"
uint32_t clockMs=0;
int main(){
 NetworkService service;
 assert(service.begin());
 assert(!WiFi.persistentEnabled && !WiFi.autoReconnect);
 assert(WiFi.hostname == Config::LOCAL_HOSTNAME);
 assert(service.apActive() == Config::NETWORK_USES_AP);
 assert(WiFi.ap == Config::NETWORK_USES_AP);
 const bool local=Config::NETWORK_USES_AP && Config::HOTSPOT_LOCAL_DEVICE_MODE;
 const bool portal=Config::NETWORK_USES_AP && !local && Config::ENABLE_CAPTIVE_PORTAL;
 assert(service.localDeviceMode()==local);
 assert(service.captivePortalActive()==portal);
 assert(service.hotspotDnsActive()==(local||portal));
 assert(dnsStarts==((local||portal)?1:0));
 assert(WiFi.AP.captiveStarts==0);
 assert(service.allowedHost("smartvest.local") && service.allowedHost("SMARTVEST.LOCAL:80"));
 assert(!service.allowedHost("smartvest.local.evil.test"));
 assert(!service.allowedHost("evil.test") && !service.allowedHost(""));
 assert(!service.allowedHost("smartvest.local:81"));
 assert(service.allowedHost("192.168.4.1") == Config::NETWORK_USES_AP);
 assert(!service.validOrigin("http://evil.test","smartvest.local"));
 assert(!service.validOrigin("null","smartvest.local"));
 assert(!service.validOrigin("http://smartvest.local/","smartvest.local"));
 assert(service.validOrigin("http://SMARTVEST.LOCAL:80","smartvest.local"));
 assert(service.validOrigin("","smartvest.local"));
 assert(!service.validOrigin("http://smartvest","smartvest.local"));
 if(Config::NETWORK_USES_STATION){
   assert(WiFi.begins==1 && WiFi.selected==(Config::NETWORK_USES_AP?WIFI_AP_STA:WIFI_STA));
   assert(WiFi.minSecurity==WIFI_AUTH_WPA2_PSK);
   assert(!service.stationConnected());
   // Wrong password/router absent: timed attempt, bounded backoff; no AP fallback.
   clockMs=Config::WIFI_CONNECT_TIMEOUT_MS-1;service.tick();assert(WiFi.disconnects==0);
   ++clockMs;service.tick();assert(WiFi.disconnects==1);
   clockMs+=Config::WIFI_RETRY_INITIAL_MS-1;service.tick();assert(WiFi.begins==1);
   ++clockMs;service.tick();assert(WiFi.begins==2);
   assert(service.apActive()==Config::NETWORK_USES_AP && WiFi.ap==Config::NETWORK_USES_AP);
   // DHCP address drives canonical host list. Connected with no IP is not ready.
   WiFi.state=WL_CONNECTED;WiFi.staIp=IPAddress();service.tick();assert(!service.stationConnected());
   WiFi.staIp=IPAddress(192,168,1,50);service.tick();assert(service.stationConnected());
   assert(service.allowedHost("192.168.1.50:80"));
   assert(service.dashboardUrl()=="http://192.168.1.50/");
   assert(service.validOrigin("http://192.168.1.50","192.168.1.50:80"));
   assert(!service.validOrigin("http://192.168.1.50","smartvest.local"));
   int calls=WiFi.begins;clockMs+=100000;service.tick();assert(WiFi.begins==calls);
   // DHCP address change invalidates old numeric host; mDNS restarts.
   int mdns=mdnsBegins;WiFi.staIp=IPAddress(192,168,1,99);service.tick();assert(mdnsBegins>mdns);
   assert(service.allowedHost("192.168.1.99") && !service.allowedHost("192.168.1.50"));
   // Wi-Fi outage invalidates station host immediately, preserving selected mode.
   WiFi.state=WL_DISCONNECTED;service.tick();assert(!service.allowedHost("192.168.1.99"));
   assert(service.apActive()==Config::NETWORK_USES_AP);
   clockMs+=Config::WIFI_RETRY_INITIAL_MS;service.tick();assert(WiFi.begins==calls+1);
   // Retry timing survives millis wraparound.
   WiFi.state=WL_CONNECTED;WiFi.staIp=IPAddress(192,168,1,2);service.tick();
   clockMs=std::numeric_limits<uint32_t>::max()-10;WiFi.state=WL_DISCONNECTED;service.tick();
   calls=WiFi.begins;clockMs+=Config::WIFI_RETRY_INITIAL_MS;service.tick();assert(WiFi.begins==calls+1);
   assert(dnsStarts==0);
 }else{
   assert(WiFi.selected==WIFI_AP && WiFi.begins==0);
   assert(WiFi.AP.bandwidthValue==WIFI_BW_HT20);
   assert(WiFi.protocol==(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|(Config::WIFI_AP_ALLOW_11N?WIFI_PROTOCOL_11N:0)));
   assert(service.radioState().configured && service.radioState().visible && service.radioState().dhcp);
   clockMs+=600000;service.tick();assert(WiFi.begins==0);
   assert(service.dashboardUrl()=="http://192.168.4.1/");
   assert(dnsTicks==((local||portal)?1:0));
   // Leaves, joins, and zero clients must not restart a healthy radio.
   int creates=WiFi.apCreates,modes=WiFi.modeCalls;
   for(int i=0;i<100;++i){
     WiFi.clients=0;WiFi.event(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);clockMs+=6000;service.tick();
     WiFi.clients=3;WiFi.event(ARDUINO_EVENT_WIFI_AP_STACONNECTED);service.tick();
   }
   assert(WiFi.apCreates==creates && WiFi.modeCalls==modes);
   assert(service.clientJoins()==100 && service.clientLeaves()==100);
   // Six is the simultaneous limit; counters continue to 100 without closing AP.
   WiFi.clients=6;service.tick();assert(service.hotspotClients()==6 && WiFi.apCreates==creates);
   WiFi.clients=5;WiFi.event(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED,8);service.tick();
   assert(service.hotspotClients()==5 && service.radioState().disconnectReason==8);
   WiFi.AP.up=false;WiFi.event(ARDUINO_EVENT_WIFI_AP_STOP);service.tick();
   assert(!service.apActive() && service.dashboardUrl()=="");
   clockMs+=Config::WIFI_RETRY_INITIAL_MS-1;service.tick();assert(WiFi.apCreates==creates);
   int mdns=mdnsBegins;++clockMs;service.tick();++creates;
   assert(service.apActive() && WiFi.apCreates==creates && mdnsBegins>mdns);
   assert(service.hotspotDnsActive()==(local||portal));
   // A failed DNS bind retries DNS alone, preserving camera associations.
   if(local||portal){
     dnsOK=false;WiFi.AP.up=false;service.tick();clockMs+=5000;service.tick();++creates;
     assert(!service.hotspotDnsActive());dnsOK=true;clockMs+=5000;service.tick();
     assert(service.hotspotDnsActive() && WiFi.apCreates==creates);
   }
   WiFi.selected=WIFI_OFF;WiFi.AP.up=false;service.tick();WiFi.apOK=false;
   clockMs+=5000;service.tick();assert(!service.apActive());int calls=WiFi.modeCalls;
   clockMs+=9999;service.tick();assert(WiFi.modeCalls==calls);
   ++clockMs;service.tick();assert(WiFi.modeCalls>calls);
   WiFi.apOK=true;clockMs+=20000;service.tick();assert(service.apActive());
   WiFi.apIp=IPAddress();service.tick();assert(!service.apActive());WiFi.apIp=IPAddress(192,168,4,1);
   clockMs+=5000;service.tick();assert(service.apActive());
   clockMs=std::numeric_limits<uint32_t>::max()-10;WiFi.AP.up=false;service.tick();
   calls=WiFi.apCreates;clockMs+=5000;service.tick();assert(WiFi.apCreates==calls+1 && service.apActive());
   // Started bits and IP can look healthy while DHCP/SSID/security/protocol drift.
   auto failAndRecover=[&](){clockMs+=1000;service.tick();assert(!service.apActive());int c=WiFi.apCreates;clockMs+=5000;service.tick();assert(WiFi.apCreates==c+1 && service.apActive());};
   WiFi.dhcpRunning=false;failAndRecover();assert(service.radioState().dhcp);
   WiFi.radioConfig.ap.ssid_hidden=1;failAndRecover();assert(service.radioState().visible);
   WiFi.radioConfig.ap.ssid[0]^=1;failAndRecover();
   WiFi.radioConfig.ap.password[0]^=1;failAndRecover();
   WiFi.radioConfig.ap.channel=11;failAndRecover();
   WiFi.radioConfig.ap.max_connection=1;failAndRecover();
   WiFi.radioConfig.ap.authmode=0;failAndRecover();
   WiFi.protocol=WIFI_PROTOCOL_11N;failAndRecover();
   WiFi.radioReadOK=false;clockMs+=1000;service.tick();assert(!service.apActive() && service.radioState().error!=0);
   WiFi.radioReadOK=true;clockMs+=1000;service.tick();assert(service.apActive());
   calls=WiFi.apCreates;clockMs+=60000;service.tick();assert(WiFi.apCreates==calls);
   assert(service.radioState().recoveryAttempts>=10);
   WiFi.AP.up=false;service.tick();WiFi.protocolOK=false;clockMs+=5000;service.tick();
   assert(!service.apActive() && service.radioState().error!=0);
   WiFi.protocolOK=true;clockMs+=10000;service.tick();assert(service.apActive());
   // Readback failure after start must back off, not count as successful recovery.
   WiFi.radioReadOK=false;clockMs+=1000;service.tick();clockMs+=5000;service.tick();
   assert(!service.apActive());calls=WiFi.apCreates;
   clockMs+=9999;service.tick();assert(WiFi.apCreates==calls);
   ++clockMs;service.tick();assert(WiFi.apCreates==calls+1 && !service.apActive());
   WiFi.radioReadOK=true;clockMs+=20000;service.tick();assert(service.apActive());
   // Startup failure must still allow the UI task and its recovery tick.
   WiFi.apOK=false;NetworkService bootFailure;assert(bootFailure.begin());assert(!bootFailure.apActive());
   WiFi.apOK=true;clockMs+=5000;bootFailure.tick();assert(bootFailure.apActive());
 }
 const auto subscriptions=WiFi.eventRegistrations;
 assert(service.begin());assert(WiFi.eventRegistrations==subscriptions);
 WiFi.eventsOK=false;NetworkService eventFailure;const auto createsBefore=WiFi.apCreates;
 assert(!eventFailure.begin()&&WiFi.apCreates==createsBefore);
 WiFi.eventsOK=true;assert(eventFailure.begin());const auto subscribed=WiFi.eventRegistrations;
 assert(eventFailure.begin()&&WiFi.eventRegistrations==subscribed);
 for(const auto&line:Serial.output)assert(line.find("Unique-Test-Passphrase") == std::string::npos);
 std::cout<<"PASS: "<<service.modeName()<<" network lifecycle + Host/Origin checks\n";
}
