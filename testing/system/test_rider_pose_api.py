"""Exercise the rider pose serializer and calibration handlers with real auth policy.
Only HTTP request/response, UART transport and the physical coordinator are adapted.
No boards, modem or network service are contacted.
"""
from pathlib import Path
import os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
headers=Path(os.environ['SJKS_ARDUINOJSON'])
mainroot=ROOT/'Main_and_Cameras/SmartVest_SJKS01_Main'
routes=(mainroot/'src/web/ImuLabRoutes.cpp').read_text(encoding='utf8')
handlers=routes[routes.index('void DashboardServer::sendRiderPose()'):routes.index('void DashboardServer::sendImuLab()')]
server=(mainroot/'src/web/DashboardServer.cpp').read_text(encoding='utf8')
auth=server[server.index('Security::Session* DashboardServer::authorize('):server.index('bool DashboardServer::readJsonBody(')]
assert 'bindRoute("/api/v1/rider/pose",HTTP_GET,&DashboardServer::sendRiderPose,Access::Role::Developer,false);' in server
assert 'bindRoute("/api/v1/rider/calibration",HTTP_POST,&DashboardServer::commandRiderCalibration,Access::Role::Developer,false);' in server
assert 'if(!authorize())return;' in routes[routes.index('void DashboardServer::sendImuLab()'):]
assert 'if(!authorize(true))return;' in routes[routes.index('void DashboardServer::commandImuLab()'):]
with tempfile.TemporaryDirectory() as tmp:
 folder=Path(tmp);main=folder/'main';shutil.copytree(mainroot,main)
 (main/'src/emergency/EmergencyCoordinator.h').write_text('#pragma once\n#include "../link/CrashProtocol.h"\nclass EmergencyCoordinator {public:bool ready=true;bool readyForCrash()const{return ready;}void observeImpact(uint32_t,uint32_t,uint8_t){}bool accept(const CrashProtocol::Frame&,bool,uint32_t){return true;}};\n',encoding='utf8')
 source=folder/'pose.cpp';source.write_text(r'''#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cassert>
#include <iostream>
#define private public
#include "src/link/SensorLink.h"
#undef private
#include "src/security/Access.h"
namespace L=ImuLabProtocol;
uint32_t clockMs=1000;
struct Web {String csrf="matching";String header(const char*)const{return csrf;}};
struct Security {
 struct Session {Access::Role role=Access::Role::Rider;String csrf="matching";} session;
 bool signedIn=true;
 Session* current(Web&){return signedIn?&session:nullptr;}
 static bool equal(const String& a,const String& b){return a==b;}
};
struct DashboardServer {
 static JsonDocument makeJson(){return JsonDocument();}
 EmergencyCoordinator coordinator;SensorLink actual{coordinator};SensorLink* sensorLink=&actual;
 Security security;Web web;JsonDocument body,response;int code=0;String error;
 bool canonical=true,origin=true,locked=false;
 bool isCanonicalHost(){return canonical;}
 bool isValidOrigin(){return origin;}
 void acquireState(){locked=true;}
 Security::Session* authorize(bool write=false,Access::Role required=Access::Role::Developer);
 bool readJsonBody(JsonDocument& out){out=body;return true;}
 void sendError(int c,const String& e){code=c;error=e;}
 void sendJson(JsonDocument& out,int c=200){code=c;response=out;}
 void sendRiderPose();void commandRiderCalibration();
 DashboardServer(){
  actual.seen=actual.labSeen=true;actual.lastSeen=actual.labSeenAt=clockMs;
  actual.labBoot=actual.heartbeat.boot=9;actual.heartbeat.flags=15;actual.labSession=88;
  actual.labLive.healthy=true;actual.labLive.sample.flags=L::AttitudeValid;
  actual.labLive.calibrationFlags=15;actual.labLive.sample.sequence=200;
  actual.labLive.side=12.5f;actual.labLive.forward=-9.75f;actual.labLive.automaticReady=true;
 }
};
''' + auth + handlers + r'''
int main(){
 {DashboardServer d;d.sendRiderPose();assert(d.code==200&&d.locked);
  auto& r=d.response;assert(r["pose"]["valid"].as<bool>());
  assert(r["pose"]["sideDeg"].as<float>()==12.5f&&r["pose"]["forwardDeg"].as<float>()==-9.75f);
  assert(r["boot"].as<unsigned>()==9&&r["sequence"].as<unsigned>()==200);
  assert(r["detector"]["automaticReady"]==true);
  d.coordinator.ready=false;d.sendRiderPose();assert(r["pose"]["valid"]==true&&r["detector"]["automaticReady"]==false);
  JsonDocument full;d.actual.writeLabJson(full.to<JsonObject>());assert(full["detector"]["automaticReady"]==false&&full["detector"]["automaticAlerts"]==false);
  d.coordinator.ready=true;d.sendRiderPose();assert(r["detector"]["automaticReady"]==true);
  assert(r.size()==9&&r["calibration"].size()==5&&r["command"].size()==3&&r["detector"].size()==4);
  std::string encoded;serializeJson(r,encoded);assert(encoded.size()<700);
  for(const auto* key:{"profile","diagnostics","trial","sample","gyroBiasDps","firmware","pins","contacts","cameraKeys","passwordHash"})assert(r[key].isNull());
  d.actual.labSeenAt=clockMs-1500;d.sendRiderPose();assert(!r["online"].as<bool>()&&!r["pose"]["valid"].as<bool>()&&r["pose"]["sideDeg"].isNull());
 }
 {DashboardServer d;d.actual.labSeenAt=clockMs-351;d.sendRiderPose();assert(d.response["online"].as<bool>()&&!d.response["pose"]["valid"].as<bool>()&&!d.response["detector"]["automaticReady"].as<bool>());}
 {DashboardServer d;d.actual.labLive.calibrationFlags=3;d.sendRiderPose();assert(!d.response["pose"]["valid"].as<bool>()&&d.response["pose"]["forwardDeg"].isNull());}
 {DashboardServer d;assert(!d.authorize(false)&&d.code==403);}
 {DashboardServer d;d.actual.labBoot=10;d.sendRiderPose();assert(!d.response["online"].as<bool>()&&d.response["pose"]["sideDeg"].isNull());}
 {DashboardServer d;d.actual.labLive.sample.flags=0;d.sendRiderPose();assert(!d.response["pose"]["valid"].as<bool>());}
 {DashboardServer d;d.actual.labLive.healthy=false;d.sendRiderPose();assert(!d.response["healthy"].as<bool>()&&!d.response["pose"]["valid"].as<bool>());}
 for(bool write:{false,true}){
  {DashboardServer d;d.security.signedIn=false;if(write)d.commandRiderCalibration();else d.sendRiderPose();assert(d.code==401&&!d.actual.commandPending);}
  {DashboardServer d;d.canonical=false;if(write)d.commandRiderCalibration();else d.sendRiderPose();assert(d.code==403&&!d.actual.commandPending);}
 }
 for(auto role:{Access::Role::Rider,Access::Role::Developer}){
  for(const char* action:{"gyro","upright","forward","neutral"}){
   DashboardServer d;d.security.session.role=role;d.body["action"]=action;d.commandRiderCalibration();
   assert(d.code==202&&d.actual.commandPending&&d.response["token"].as<unsigned>());
   assert(d.actual.labCommand.data[0]>=2&&d.actual.labCommand.data[0]<=5);
  }
 }
 for(const char* action:{"retry","clear_body","start","stop","read","reset_candidate","","UPRIGHT"}){
  DashboardServer d;d.body["action"]=action;d.commandRiderCalibration();assert(d.code==400&&!d.actual.commandPending);
 }
 {DashboardServer d;d.body["action"]=3;d.commandRiderCalibration();assert(d.code==400&&!d.actual.commandPending);}
 {DashboardServer d;d.body["action"]="gyro";d.body["seconds"]=10;d.commandRiderCalibration();assert(d.code==400&&!d.actual.commandPending);}
 {DashboardServer d;d.body["action"]="gyro";d.web.csrf="wrong";d.commandRiderCalibration();assert(d.code==403&&!d.actual.commandPending);}
 {DashboardServer d;d.body["action"]="gyro";d.origin=false;d.commandRiderCalibration();assert(d.code==403&&!d.actual.commandPending);}
 {DashboardServer d;d.sensorLink=nullptr;d.sendRiderPose();assert(d.code==503);d.body["action"]="gyro";d.commandRiderCalibration();assert(d.code==409);}
 {DashboardServer d;d.body["action"]="gyro";d.actual.labLive.healthy=false;d.commandRiderCalibration();assert(d.code==409&&!d.actual.commandPending);}
 {DashboardServer d;d.body["action"]="gyro";d.actual.labSeenAt=clockMs-351;d.commandRiderCalibration();assert(d.code==409&&!d.actual.commandPending);}
 {DashboardServer d;d.body["action"]="gyro";d.actual.commandPending=true;d.commandRiderCalibration();assert(d.code==409);}
 {DashboardServer d;d.body["action"]="gyro";d.actual.labLive.calibrationResult=1;d.commandRiderCalibration();assert(d.code==409&&!d.actual.commandPending);}
 for(auto state:{L::State::Countdown,L::State::Recording}){DashboardServer d;d.body["action"]="upright";d.actual.labLive.state=state;d.commandRiderCalibration();assert(d.code==409&&!d.actual.commandPending);}
 {DashboardServer d;d.actual.labLive.calibrationFlags=0;d.body["action"]="forward";d.commandRiderCalibration();assert(d.code==409&&!d.actual.commandPending);}
 {DashboardServer d;d.actual.labLive.calibrationFlags=L::UprightSet;d.body["action"]="neutral";d.commandRiderCalibration();assert(d.code==409&&!d.actual.commandPending);}
 std::cout<<"PASS rider pose and calibration: bounded private view, calibrated validity/staleness, real session/role/host/CSRF/origin policy, four-action whitelist and command/prerequisite gates\n";
}
''',encoding='utf8')
 binary=folder/'rider-pose'
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-DARDUINO_ARCH_ESP32=1','-DCONFIG_IDF_TARGET_ESP32=1',
   '-I'+str(ROOT/'testing/main/host'),'-I'+str(headers),'-I'+str(main),
   str(main/'src/link/SensorLink.cpp'),str(main/'src/link/ImuLabReceiver.cpp'),str(main/'src/link/PiezoReceiver.cpp'),str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
