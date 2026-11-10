"""Production AP recovery/heap policies and camera HTTP failure paths.
Radio, HTTP and cryptographic verification outcomes are controlled adapters;
test_camera_http.py separately executes the real signed Main handlers.
"""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
MAIN=ROOT/'Main_and_Cameras/SmartVest_SJKS01_Main'
CAMERA=ROOT/'Main_and_Cameras/SmartVest_SJKS01_Camera'
HOST=ROOT/'testing/main/host'
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)
 (p/'esp_heap_caps.h').write_text('''#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
inline size_t testFree=65536,testLargest=65536;
inline size_t heap_caps_get_free_size(unsigned){return testFree;}
inline size_t heap_caps_get_largest_free_block(unsigned){return testLargest;}
''')
 code=r'''
#include <cassert>
#include <iostream>
#include "src/network/NetworkService.h"
#include "src/web/HttpMemoryBudget.h"
#include "Config.h"
uint32_t clockMs=0;
int main(){
 NetworkService network;assert(network.begin());WiFi.clients=3;
 for(int n=0;n<120;n++){clockMs+=1000;network.tick();}assert(network.radioState().serviceRecoveries==0);
 const auto queries=WiFi.stationListQueries;
 for(unsigned n=0;n<10000;++n){clockMs+=2;network.tick();network.observeService(n+1,true);}
 assert(WiFi.stationListQueries==queries); // No full client-list query in the fast recovery loop.
 network.observeService(1,true);const auto before=WiFi.apCreates;
 WiFi.clients=0; // A stalled AP can lose every client while readback stays healthy.
 clockMs+=44999;network.tick();assert(WiFi.apCreates==before);
 ++clockMs;network.tick();assert(WiFi.apCreates==before+1&&network.radioState().serviceRecoveries==1);
 clockMs+=119999;network.tick();assert(WiFi.apCreates==before+1);
 ++clockMs;network.tick();assert(network.radioState().serviceRecoveries==2);
 clockMs+=239999;network.tick();assert(network.radioState().serviceRecoveries==2);
 ++clockMs;network.tick();assert(network.radioState().serviceRecoveries==3);
 clockMs+=3000000;network.tick();assert(network.radioState().serviceRecoveries==3);
 network.observeService(2,true);clockMs+=45000;network.tick();assert(network.radioState().serviceRecoveries==4);
 ApServiceWatchdog healthy;healthy.restarted(0);healthy.observe(1,1,false,0);
 assert(!healthy.recover(1000000,0));
 assert(healthy.recover(1000000,3));
 ApServiceWatchdog fresh;fresh.restarted(0);assert(!fresh.recover(1000000,0));
 ApServiceWatchdog wrapped;wrapped.restarted(0xfffffff0U);wrapped.observe(0xfffffff0U,1,true,0);
 assert(!wrapped.recover(uint32_t(0xfffffff0U+44999U),0));assert(wrapped.recover(uint32_t(0xfffffff0U+45000U),0));
 assert(HttpMemoryBudget::available(4096));testFree=HttpMemoryBudget::RESERVE+1024;assert(!HttpMemoryBudget::available(1025));assert(HttpMemoryBudget::available(1024));
 testLargest=100;assert(!HttpMemoryBudget::available(101));testFree=1;assert(!HttpMemoryBudget::available(1));
 assert(!HttpMemoryBudget::fits(size_t(-1),65536,65536));
 std::cout<<"PASS actual zero client-list queries in 10000 healthy ticks and capped AP recovery after all clients disappear, empty/idle AP, progressive cooldown, rearming, clock wrap and radio heap reserve\n";
}
'''
 f=p/'network.cpp';f.write_text(code);exe=p/'network.exe'
 subprocess.run(['g++','-std=c++17','-DARDUINO_ARCH_ESP32','-DCONFIG_IDF_TARGET_ESP32','-I'+str(p),'-I'+str(HOST),'-I'+str(MAIN),str(MAIN/'src/network/NetworkService.cpp'),str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 source=(CAMERA/'src/CameraApp.cpp').read_text()
 method=source[source.index('int request(const char* path,'):source.index('// Event callbacks')]
 pre=r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include <cstring>
inline ArduinoJson::DeserializationError deserializeJson(ArduinoJson::JsonDocument& d,String& s){return ArduinoJson::deserializeJson(d,s.c_str());}
uint32_t clockMs=1000;int responseCode=200,sessionCode=200;bool signedReply=true,completeReply=true;unsigned handshakes=0;
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS=2000,HTTP_READ_TIMEOUT_MS=3000;
const char* MAIN_URL="http://192.168.4.1";const char* TOKEN="fixture";const char* ROLE="front";
namespace CameraAuth {constexpr uint32_t REKEY_MS=1800000;struct Exchange{char client[33]{},session[33]{};uint32_t sequence=0;};
bool message(const char*,const char*,const Exchange&,const char*,const char*,const char*,size_t,int,char* out){memset(out,'a',64);out[64]=0;return true;}}
namespace CameraCrypto{bool equal(const char* a,const char* b,size_t n){return strlen(a)==n&&strlen(b)==n&&!memcmp(a,b,n);}}
CameraAuth::Exchange authenticated;uint32_t authenticatedAt=1000;
bool downloadControlStop=false;int snapshotMux=0;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))

void clientNonce(){authenticated.session[0]=0;authenticated.sequence=0;}
bool establishSession(){++handshakes;strcpy(authenticated.session,"01234567890123456789012345678901");authenticated.sequence=0;authenticatedAt=millis();return true;}
struct WiFiClient{};
struct HTTPClient{bool begin(WiFiClient&,const String&){return true;}void setConnectTimeout(uint32_t){}void setTimeout(uint32_t){}void setReuse(bool){}void collectHeaders(const char**,size_t){}
void addHeader(const char*,const char*){}int POST(const String&){return responseCode;}int GET(){return responseCode;}void end(){}String header(const char*){return signedReply?String(std::string(64,'a').c_str()):String("");}};
bool readReply(HTTPClient&,String& out){out="{\"command\":\"untrusted\"}";return completeReply;}
'''
 checks=r'''
int main(){establishSession();handshakes=0;responseCode=503;signedReply=false;JsonDocument reply;
for(int n=0;n<3;n++){assert(request("/api/v1/cameras/commands",nullptr,&reply)==503);assert(authenticated.session[0]&&reply.isNull());}assert(handshakes==0);
responseCode=-11;assert(request("/api/v1/cameras/commands",nullptr,&reply)==-11&&authenticated.session[0]&&reply.isNull());
responseCode=200;completeReply=false;assert(request("/api/v1/cameras/commands",nullptr,&reply)==-11&&authenticated.session[0]);
completeReply=true;assert(request("/api/v1/cameras/commands",nullptr,&reply)==-3&&!authenticated.session[0]&&reply.isNull());
establishSession();responseCode=401;assert(request("/api/v1/cameras/commands",nullptr,&reply)==401&&!authenticated.session[0]);
establishSession();responseCode=200;signedReply=true;assert(request("/api/v1/cameras/commands",nullptr,&reply)==200&&reply["command"]=="untrusted");
std::cout<<"PASS actual camera request: transient/partial errors preserve session, no extra handshakes, original diagnostics retained, unsigned success/expired auth rejected\n";}
'''
 f=p/'camera.cpp';f.write_text(pre+method+checks);exe=p/'camera.exe'
 subprocess.run(['g++','-std=c++17','-I'+str(HOST),'-I'+os.environ['SJKS_ARDUINOJSON'],str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
