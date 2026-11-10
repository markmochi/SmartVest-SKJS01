"""Exercise the actual response function with ArduinoJson and allocation failures."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
s=(r/'SmartVest_SJKS01_Main/src/web/DashboardServer.cpp').read_text(encoding='utf8')
body=s[s.index('void DashboardServer::sendJson('):s.index('void DashboardServer::sendSession(')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'test.cpp'
 f.write_text(r"""
#include <ArduinoJson.h>
#include "ResponseMemory.h"
#include <algorithm>
struct FakeESP{uint32_t getFreeHeap(){return 66000;}uint32_t getMaxAllocHeap(){return 45000;}} ESP;
#include "CameraAuth.h"
constexpr int HTTP_POST=1;
#include <cassert>
#include <cstdlib>
#include <unordered_map>
#include <string>
#include <iostream>
struct Alloc:ArduinoJson::Allocator {
 std::unordered_map<void*,size_t> blocks;size_t bytes=0,peak=0;bool fail=false;
 void* allocate(size_t n) override{if(fail)return nullptr;void* p=malloc(n);if(p){blocks[p]=n;bytes+=n;if(bytes>peak)peak=bytes;}return p;}
 void deallocate(void* p) override{if(!p)return;bytes-=blocks.at(p);blocks.erase(p);free(p);}
 void* reallocate(void* p,size_t n) override{if(fail)return nullptr;if(!p)return allocate(n);size_t old=blocks.at(p);void* q=realloc(p,n);if(q){blocks.erase(p);blocks[q]=n;bytes=bytes-old+n;if(bytes>peak)peak=bytes;}return q;}
} alloc;size_t expectedRetainedBytes=0;
struct String {
 std::string v;static bool fail,shortWrite;
 bool reserve(size_t n){if(fail)return false;v.reserve(n);return true;}
 const char* c_str()const{return v.c_str();}size_t length()const{return v.size();}
 size_t write(uint8_t c){if(shortWrite)return 0;v+=char(c);return 1;}
 size_t write(const uint8_t* p,size_t n){if(shortWrite)return 0;v.append((const char*)p,n);return n;}
};bool String::fail=false;bool String::shortWrite=false;
const char* cameraKey(int){return "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";}const char* cameraRole(int){return "front";}
struct DashboardServer {
 ResponseMemory::Health responseHealth;
 int cameraResponseRole=-1;CameraAuth::Exchange cameraExchange;
 struct Web {int status=0;std::string payload;
 int method(){return 0;}String uri(){String s;s.v="/fixture";return s;}void sendHeader(const char*,const char*){}
 void send(int code,const char*,const char* text){assert(alloc.bytes==expectedRetainedBytes);status=code;payload=text;}
 void sendOwned(int code,const char* type,String&& text){send(code,type,text.v.c_str());}
 void send(int code,const char* type,const String& text){send(code,type,text.v.c_str());}
 } web;
 bool released=false;void releaseState(){released=true;}void addSecurityHeaders(){assert(released);}
 void sendJson(JsonDocument&,int);
};
"""+body+r"""
int main(){DashboardServer server;
 for(int run=0;run<10000;++run){
  JsonDocument doc(&alloc);auto a=doc["values"].to<JsonArray>();for(int i=0;i<150;++i){auto o=a.add<JsonObject>();o["sample"]=i;o["state"]="connected";}
  assert(alloc.bytes>0);std::string expected;serializeJson(doc,expected);
  server.sendJson(doc,200);assert(server.web.status==200&&server.web.payload==expected);assert(alloc.bytes==0&&doc.isNull());
 }
 {JsonDocument doc(&alloc);doc["ok"]=true;String::fail=true;server.sendJson(doc,200);assert(server.web.status==503&&alloc.bytes==0);String::fail=false;}
 {JsonDocument doc(&alloc);doc["ok"]=true;alloc.fail=true;for(int i=0;i<500;++i)doc["list"].add(i);assert(doc.overflowed());server.sendJson(doc,200);assert(server.web.status==503&&alloc.bytes==0);alloc.fail=false;}
 {JsonDocument doc(&alloc);doc["ok"]=true;String::shortWrite=true;server.sendJson(doc,200);assert(server.web.status==503&&alloc.bytes==0&&server.responseHealth.lastFailureStage==3&&server.responseHealth.lengthFailures==1);String::shortWrite=false;}
 {JsonDocument doc(&alloc);doc["recovered"]=true;server.sendJson(doc,202);assert(server.web.status==202&&server.web.payload=="{\"recovered\":true}");}

 {JsonDocument front(&alloc),rear(&alloc);
  for(auto* page:{&front,&rear}){
   auto rows=(*page)["clips"].to<JsonArray>();
   for(unsigned i=0;i<5;++i){auto row=rows.add<JsonObject>();row["name"]=std::string(80,'a');row["session"]=UINT64_MAX;row["start"]=UINT64_MAX;row["utcMs"]=1792800000000ULL;row["protected"]=true;row["protectedUtcMs"]=1792800000000ULL;row["protectionReason"]=2;}
   (*page)["error"]="";(*page)["recoveryFilesDeleted"]=100;page->shrinkToFit();assert(measureJson(*page)<=2600);
  }
  expectedRetainedBytes=alloc.bytes;alloc.peak=alloc.bytes;
  for(unsigned i=0;i<3;++i){JsonDocument reply=front;server.sendJson(reply,200);assert(front["clips"].size()==5&&rear["clips"].size()==5&&alloc.bytes==expectedRetainedBytes);}
  const auto transientWithEncoding=alloc.peak-expectedRetainedBytes+server.web.payload.size();assert(transientWithEncoding<8192);
  String::fail=true;{JsonDocument reply=front;server.sendJson(reply,200);assert(server.web.status==503&&front["clips"].size()==5&&alloc.bytes==expectedRetainedBytes);}String::fail=false;
  std::cout<<"PASS two retained five-clip pages: cached bytes="<<expectedRetainedBytes<<", transient copy plus encoding bytes="<<transientWithEncoding<<"; repeat reads and allocation failure preserve the cache\n";
  front.clear();rear.clear();expectedRetainedBytes=0;assert(alloc.bytes==0);
 }
 std::cout<<"PASS 10000 actual JSON responses: exact payload, tree released before TCP, no retained document allocations, reserve/overflow 503 and recovery. Host peak document bytes="<<alloc.peak<<"\n";
}
""",encoding='utf8')
 subprocess.run(['g++','-std=c++17','-I'+str(r.parent/'testing/system/platform'),'-UNDEBUG','-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(r.parent/'common'),'-I'+str(r/'SmartVest_SJKS01_Main/src/web'),str(f),'-lcrypto','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
