"""Execute the production session, identity and signed response handlers."""
from pathlib import Path
import os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
s=(root/'SmartVest_SJKS01_Main/src/web/DashboardServer.cpp').read_text(encoding='utf8')
handlers=s[s.index('namespace {\nconst char* cameraKey'):s.index('void DashboardServer::addSecurityHeaders')]
response=s[s.index('void DashboardServer::sendJson('):s.index('void DashboardServer::sendSession(')]+s[s.index('void DashboardServer::sendError('):s.index('bool DashboardServer::isCanonicalHost')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);host=p/'host';shutil.copytree(root/'../testing/main/host',host)
 code=r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include "ResponseMemory.h"
#include <algorithm>
struct FakeESP{uint32_t getFreeHeap(){return 66000;}uint32_t getMaxAllocHeap(){return 45000;}} ESP;
#include "CameraAuth.h"
#include <map>
#include <cassert>
#include <iostream>
uint32_t clockMs=1000;
uint32_t esp_random(){static uint32_t n=27;return ++n;}
constexpr int HTTP_POST=1,HTTP_GET=0;
namespace CameraPairing {
constexpr const char* FRONT_KEY="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
constexpr const char* REAR_KEY="101112131415161718191a1b1c1d1e1f000102030405060708090a0b0c0d0e0f";
}
struct DashboardServer {
 static JsonDocument makeJson(){return JsonDocument();}
 ResponseMemory::Health responseHealth;
 struct Web {
  std::map<std::string,String> incoming,outgoing;String body,path="/api/v1/cameras/session",sent;int verb=HTTP_GET,status=0;
  String header(const char* s){return incoming[s];}String arg(const char*){return body;}const String& rawBody(){return body;}int method(){return verb;}String uri(){return path;}
  void sendHeader(const char* name,const char* value){outgoing[name]=value;}
  void sendOwned(int code,const char*,String&& text){status=code;sent=std::move(text);}
  void send(int code,const char*,const String& text){status=code;sent=text;}
 } web;
 char* mutableMessage=nullptr;bool canonical=true,locked=false;int cameraResponseRole=-1;CameraAuth::Exchange cameraExchange;CameraAuth::Peer cameraPeers[2];
 bool isCanonicalHost(){return canonical;}void acquireState(){locked=true;}void releaseState(){locked=false;if(mutableMessage)strcpy(mutableMessage,"changed");}void addSecurityHeaders(){}
 void cameraSession();int cameraIdentity();void sendJson(JsonDocument&,int code=200);void sendError(int,const String&);void sendError(int,const char*);
 void reset(){web.outgoing.clear();web.status=0;web.sent="";cameraResponseRole=-1;locked=false;}
};
''' +handlers+response+r'''
int main(){
 DashboardServer d;CameraAuth::Exchange x;strcpy(x.client,"11111111111111111111111111111111");char tag[65];
 d.web.incoming["X-SV-Camera"]="front";d.web.incoming["X-SV-Client"]=x.client;
 d.cameraSession();assert(d.web.status==401);
 assert(CameraAuth::hello(CameraPairing::FRONT_KEY,"front",x.client,tag));d.web.incoming["X-SV-MAC"]=tag;d.reset();d.cameraSession();assert(d.web.status==200);
 JsonDocument reply;assert(!deserializeJson(reply,d.web.sent.c_str()));strcpy(x.session,reply["session"].as<const char*>());
 assert(CameraAuth::proof(CameraPairing::FRONT_KEY,"front",x,tag));assert(CameraCrypto::equal(tag,reply["proof"],64));
 x.sequence=1;d.web.path="/api/v1/cameras/ack";d.web.verb=HTTP_POST;d.web.body="{\"stage\":\"protected\"}";
 d.web.incoming["X-SV-Session"]=x.session;d.web.incoming["X-SV-Seq"]="1";
 assert(CameraAuth::message(CameraPairing::FRONT_KEY,"front",x,"POST",d.web.path.c_str(),d.web.body.c_str(),d.web.body.length(),0,tag));
 d.web.incoming["X-SV-MAC"]=tag;d.canonical=false;d.reset();assert(d.cameraIdentity()==-1&&d.web.status==403);
 d.canonical=true;d.reset();assert(d.cameraIdentity()==0&&d.locked);JsonDocument result;result["ok"]=true;d.sendJson(result);
 assert(!d.locked&&result.isNull());assert(CameraAuth::message(CameraPairing::FRONT_KEY,"front",x,"POST",d.web.path.c_str(),d.web.sent.c_str(),d.web.sent.length(),200,tag));
 assert(CameraCrypto::equal(tag,d.web.outgoing["X-SV-MAC"].c_str(),64));
 d.reset();assert(d.cameraIdentity()==-1&&d.web.status==401); // Replay.
 x.sequence=2;d.web.incoming["X-SV-Seq"]="2";assert(CameraAuth::message(CameraPairing::FRONT_KEY,"front",x,"POST",d.web.path.c_str(),d.web.body.c_str(),d.web.body.length(),0,tag));d.web.incoming["X-SV-MAC"]=tag;
 String saved=d.web.body;d.web.body="{}";d.reset();assert(d.cameraIdentity()==-1);d.web.body=saved;
 d.web.incoming["X-SV-Camera"]="rear";d.reset();assert(d.cameraIdentity()==-1);d.web.incoming["X-SV-Camera"]="front";
 d.reset();assert(d.cameraIdentity()==0);d.sendError(409,"Unknown incident");
 assert(CameraAuth::message(CameraPairing::FRONT_KEY,"front",x,"POST",d.web.path.c_str(),d.web.sent.c_str(),d.web.sent.length(),409,tag));assert(CameraCrypto::equal(tag,d.web.outgoing["X-SV-MAC"].c_str(),64));
 d.web.incoming.clear();d.web.incoming["Authorization"]="Bearer fixture-key";d.reset();assert(d.cameraIdentity()==-1&&d.web.status==401);
 d.reset();char message[]="Original mutable error";d.mutableMessage=message;d.sendError(409,message);assert(strcmp(message,"changed")==0&&d.web.sent=="{\"error\":\"Original mutable error\"}");
 std::cout<<"PASS actual Main HTTP handlers: signed handshake, canonical host, exact-body and role checks, replay rejection, signed success/error replies, mutex release and legacy bearer rejection\n";
}
'''
 f=p/'http.cpp';f.write_text(code,encoding='utf8');exe=p/'http.exe'
 subprocess.run(['g++','-std=c++17','-I'+str(root.parent/'testing/system/platform'),'-Wall','-Wextra','-Werror','-I'+str(host),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(root.parent/'common'),'-I'+str(root/'SmartVest_SJKS01_Main/src/web'),str(f),'-lcrypto','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
