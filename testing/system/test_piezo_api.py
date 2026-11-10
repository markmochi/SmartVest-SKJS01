"""Execute the actual administrator piezo route and shared authorization policy."""
from pathlib import Path
import os,subprocess,tempfile,shutil
root=Path(__file__).resolve().parents[2];main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
s=(main/'src/web/DashboardServer.cpp').read_text(encoding='utf8')
auth=s[s.index('Security::Session* DashboardServer::authorize('):s.index('bool DashboardServer::readJsonBody(')]
route=(main/'src/web/PiezoRoutes.cpp').read_text(encoding='utf8');route=route[route.index('void DashboardServer::commandPiezo()'):].replace('Config::ENABLE_BENCH_TESTS','testBenchEnabled')
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'test.cpp'
 f.write_text(r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
#include <cassert>
#include <iostream>
#include "src/security/Access.h"
#include "src/link/ImuLabProtocol.h"
#include "src/link/PiezoProtocol.h"
bool testBenchEnabled=true;
inline void convertFromJson(JsonVariantConst v,String& s){s=v.as<const char*>();}
uint32_t clockMs=1000;
struct Web{String csrf="same";String header(const char*)const{return csrf;}};
struct Security{struct Session{Access::Role role=Access::Role::Developer;String csrf="same";}session;bool signedIn=true;Session* current(Web&){return signedIn?&session:nullptr;}static bool equal(const String&a,const String&b){return a==b;}};
struct Sensor{
 unsigned calls=0;uint16_t front=0,rear=0;uint32_t revision=0;ImuLabProtocol::Op operation=ImuLabProtocol::Op::Retry;
 ImuLabProtocol::Result ready=ImuLabProtocol::Result::Accepted;
 auto piezoCommandReady(ImuLabProtocol::Op)const{return ready;}
 uint32_t requestLab(ImuLabProtocol::Op op,uint16_t f,uint16_t r,uint32_t rev){++calls;operation=op;front=f;rear=r;revision=rev;return 9;}
};
struct DashboardServer{
 static JsonDocument makeJson(){return JsonDocument();}
 Security security;Web web;Sensor sensor;Sensor* sensorLink=&sensor;JsonDocument body,response;int code=0;bool canonical=true,origin=true;
 bool isCanonicalHost(){return canonical;}bool isValidOrigin(){return origin;}void acquireState(){}
 Security::Session* authorize(bool write=false,Access::Role required=Access::Role::Developer);
 bool readJsonBody(JsonDocument& out){out=body;return true;}void sendError(int c,const String&){code=c;}void sendJson(JsonDocument& out,int c){code=c;response=out;}
 void commandPiezo();DashboardServer(){body["action"]="thresholds";body["revision"]=1;body["front"]=600;body["rear"]=700;}
};
'''+auth+route+r'''
int main(){
 {DashboardServer d;d.commandPiezo();assert(d.code==202&&d.sensor.calls==1&&d.sensor.front==600&&d.sensor.rear==700&&d.sensor.revision==1&&d.response["token"]==9);}
 for(unsigned n=0;n<12;++n){DashboardServer d;
  if(n==0)d.security.signedIn=false;if(n==1)d.security.session.role=Access::Role::Rider;
  if(n==2)d.web.csrf="wrong";if(n==3)d.origin=false;if(n==4)d.canonical=false;
  if(n==5)d.body["extra"]=1;if(n==6)d.body["action"]="invalid";
  if(n==7)d.body["front"]=49;if(n==8)d.body["rear"]=801;if(n==9)d.body["revision"]=-1;if(n==10)d.body["front"]=500.5;
  if(n==11)d.sensor.ready=ImuLabProtocol::Result::Offline;
  d.commandPiezo();assert(d.code>=400&&d.sensor.calls==0);
 }
 for(unsigned value:{100u,799u,800u}){DashboardServer d;d.body["front"]=value;d.body["rear"]=value;d.commandPiezo();assert(d.code==202&&d.sensor.front==value&&d.sensor.rear==value);}
 {DashboardServer d;d.body.clear();d.body["action"]="normal";d.body["revision"]=1;testBenchEnabled=false;d.commandPiezo();assert(d.code==409&&d.sensor.calls==0);
  d.body["action"]="stop";d.commandPiezo();assert(d.code==202&&d.sensor.operation==ImuLabProtocol::Op::PiezoStop);testBenchEnabled=true;}
 std::cout<<"PASS real piezo POST: administrator/session/host/origin/CSRF, strict fields/types, bounded thresholds, fresh source, queued confirmation and stop with bench gate off\n";
}
''',encoding='utf8')
 binary=p/'test'
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(root/'testing/main/host'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(main),str(f),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);m=p/'main';shutil.copytree(main,m)
 (m/'src/emergency/EmergencyCoordinator.h').write_text('#pragma once\n#include "../link/CrashProtocol.h"\nclass EmergencyCoordinator {public:bool readyForCrash()const{return true;}void observeImpact(uint32_t,uint32_t,uint8_t){}bool accept(const CrashProtocol::Frame&,bool,uint32_t){return true;}};\n',encoding='utf8')
 binary=p/'receiver'
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(root/'testing/main/host'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(m),str(m/'src/link/SensorLink.cpp'),str(m/'src/link/ImuLabReceiver.cpp'),str(m/'src/link/PiezoReceiver.cpp'),str(root/'testing/main/host/piezo_receiver_scenarios.cpp'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
