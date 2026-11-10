"""Run production request parsing with the internal-heap allocator and failure injection."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2];main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
s=(main/'src/web/DashboardServer.cpp').read_text('utf-8')
method=s[s.index('bool DashboardServer::readJsonBody('):s.index('bool DashboardServer::sendConnectivityCheck(')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 (p/'esp_heap_caps.h').write_text("#pragma once\n#include <cstddef>\nconstexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;\ninline size_t freeBytes=65536,largestBlock=65536;\ninline size_t heap_caps_get_free_size(unsigned){return freeBytes;}\ninline size_t heap_caps_get_largest_free_block(unsigned){return largestBlock;}\n")
 code=r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include "src/web/JsonRequestMemory.h"
#include <cassert>
#include <iostream>
uint32_t clockMs=0;
inline DeserializationError deserializeJson(JsonDocument& d,const String& s){return ArduinoJson::deserializeJson(d,s.c_str());}
struct DashboardServer {
 struct Web {String raw="{\"value\":42}",type="application/json";unsigned reads=0;
 const String& rawBody(){++reads;return raw;}String header(const char*){return type;}
 }web;
 int code=0;void sendError(int status,const char*){code=status;}
 bool readJsonBody(JsonDocument&);
};
'''+method+r'''
int main(){
 DashboardServer d;JsonDocument body;
 assert(d.readJsonBody(body)&&body["value"]==42&&d.web.reads==1);
 d.web.raw="{\"next\":43}";assert(d.readJsonBody(body)&&body["next"]==43&&body["value"].isNull());
 // The document retains a process-lifetime allocator after the function returns.
 body["afterReturn"]=44;assert(body["afterReturn"]==44);body.clear();
 freeBytes=HttpMemoryBudget::RESERVE;assert(!d.readJsonBody(body)&&d.code==503&&body.isNull());
 freeBytes=65536;largestBlock=1;assert(!d.readJsonBody(body)&&d.code==503);
 largestBlock=65536;assert(d.readJsonBody(body)&&body["next"]==43);body.clear();
 for(const char* text:{"[1]","{","null"}){d.web.raw=text;assert(!d.readJsonBody(body)&&d.code==400&&body.isNull());}
 d.web.raw=std::string(4097,'x');assert(!d.readJsonBody(body)&&d.code==413);
 d.web.type="text/plain";assert(!d.readJsonBody(body)&&d.code==415);
 d.web.type="application/json";d.web.raw="{\"ok\":true}";
 for(unsigned n=0;n<10000;++n){assert(d.readJsonBody(body)&&body["ok"]==true);body.clear();}
 std::cout<<"PASS actual borrowed-body JSON parser: shared allocator lifetime, 12 KiB internal reserve, contiguous allocation refusal, 503 recovery, input validation and 10000 repeated parses\n";
}
'''
 (p/'test.cpp').write_text(code,'utf-8')
 subprocess.run(['g++','-std=c++17','-DARDUINO_ARCH_ESP32','-I'+str(p),'-I'+str(root/'testing/main/host'),'-I'+str(main),'-I'+os.environ['SJKS_ARDUINOJSON'],str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
