"""Run Main's production camera library handlers with controlled transport/state."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2];main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
source=(main/'src/web/CameraFiles.cpp').read_text();source=source[source.index('void DashboardServer::serveCameraFiles'):]
code=r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include "CameraPresence.h"
#include "CameraFiles.h"
#include "DownloadLease.h"
#include <map>
#include <cassert>
#include <iostream>
inline void convertFromJson(JsonVariantConst value,String& out){out=value.as<const char*>();}
uint32_t clockMs=1000;uint32_t esp_random(){static uint32_t n=80;return ++n;}
namespace Access {enum class Role{Rider,Developer};}
struct Security{
 struct Session{String csrf=String("1111111111111111111111111111111111111111111111111111111111111111");Access::Role role=Access::Role::Rider;};Session sessions[2];
 static bool equal(const String& a,const String& b){return a==b;}
 static String randomHex(size_t){return String("abcdef0123456789abcdef0123456789");}
};
namespace CameraCrypto{bool equal(const char* a,const char* b,size_t n){return memcmp(a,b,n)==0;}}
struct TestAddress{int operator[](int i)const{return i==0?192:i==1?168:i==2?4:2;}String toString()const{return "192.168.4.2";}};
namespace Config {constexpr unsigned CAMERA_OFFLINE_MS=15000,CAMERA_STATUS_FRESH_MS=7000,CAMERA_FRAME_FRESH_AT_REPORT_MS=2000;}
struct DashboardServer{
 static JsonDocument makeJson(){return JsonDocument();}
 struct Camera:CameraPresence{bool sdReady=true,recording=true;TestAddress address;JsonDocument telemetry;struct{uint32_t stream=22;}cursor;}cameras[2];
 struct Battery{void writeRiderBattery(JsonObject out){out["percent"]=80;}}battery;
 Security security;Security::Session actor;DownloadLease download;
 void writeDownload(JsonObject,Security::Session*);void observeDownload(int);
 CameraFiles cameraFiles[2];
 struct Web{std::map<std::string,String> args;String arg(const char* key){return args[key];}bool hasArg(const char* key){return args.count(key);}}web;
 struct Emergency{bool ready=true,downloadReady=true;bool canResetData(){return ready;}bool canDownloadFrom(int){return downloadReady;}}coordinator,*emergency=&coordinator;
 bool logged=true,csrf=true;int camera=0,code=0;JsonDocument input,response;
 Security::Session* authorize(bool write,Access::Role){if(!logged){code=401;return nullptr;}if(write&&!csrf){code=403;return nullptr;}return &actor;}
 bool readJsonBody(JsonDocument& out){out=input;return true;}
 void sendJson(JsonDocument& out,int status=200){response=out;out.clear();code=status;}
 void sendError(int status,const String& message){code=status;response.clear();response["error"]=message;}
 int cameraIdentity(){return camera;}
 void serveCameraFiles(bool);void receiveCameraFiles();void offerCameraFiles(int,JsonObject);
 DashboardServer(){for(auto& c:cameras){c.heartbeat(clockMs,15000);c.telemetry["stream"]=22;c.telemetry["filesSupported"]=1;c.telemetry["downloadsSupported"]=1;c.telemetry["activeLocks"]=0;c.telemetry["normalRetained"]=20;c.telemetry["lockedRetained"]=5;}web.args["camera"]="front";}
};
'''+source+r'''
int main(){
 {DashboardServer x;x.cameraFiles[0].id=444;x.cameraFiles[0].pending=true;x.cameraFiles[0].at=clockMs;
  x.serveCameraFiles(false);assert(x.response["busy"]==true&&x.response["requestId"]==444);
  x.cameraFiles[1].id=445;x.cameraFiles[1].at=clockMs;x.cameraFiles[1].result["clips"].to<JsonArray>();
  const auto now=clockMs;clockMs+=120001;x.serveCameraFiles(false);assert(x.response["busy"]==false&&!x.cameraFiles[0].pending);assert(x.cameraFiles[1].id==0&&x.cameraFiles[1].result.isNull());clockMs=now;}

 // A retained failed incident may keep offering LOCK retries after all safety
 // work is terminal. It must not starve file management or cancel downloads.
 { DashboardServer x; x.input["operation"]=1;x.serveCameraFiles(true);assert(x.code==202);
   JsonDocument out;out["command"]["type"]="LOCK";x.coordinator.ready=false;
   x.offerCameraFiles(0,out.as<JsonObject>());assert(!out["command"].isNull()&&out["files"]["id"]==x.cameraFiles[0].id);
   x.cameraFiles[0].operation=3;out.clear();out["command"]["type"]="LOCK";
   x.offerCameraFiles(0,out.as<JsonObject>());assert(out["files"].isNull());
   x.coordinator.ready=true;out.clear();out["command"]["type"]="LOCK";
   x.offerCameraFiles(0,out.as<JsonObject>());assert(out["command"].isNull()&&out["files"]["operation"]==3);
 }

 {DashboardServer x;x.input["operation"]=7;x.input["confirm"]="DELETE INTERRUPTED";x.serveCameraFiles(true);assert(x.code==409&&!x.cameraFiles[0].pending);
  x.cameras[0].telemetry["filesSupported"]=2;x.input["confirm"]="DELETE ALL";x.serveCameraFiles(true);assert(x.code==400);
  x.input["confirm"]=JsonString("DELETE INTERRUPTED\0junk",23);x.serveCameraFiles(true);assert(x.code==400);
  x.input["confirm"]=123;x.serveCameraFiles(true);assert(x.code==400);
  x.input["confirm"]="DELETE INTERRUPTED";x.coordinator.ready=false;x.serveCameraFiles(true);assert(x.code==409);
  x.coordinator.ready=true;x.serveCameraFiles(true);assert(x.code==202&&x.cameraFiles[0].operation==7);
  JsonDocument offered;x.offerCameraFiles(0,offered.to<JsonObject>());assert(offered["files"]["operation"]==7);
 }
 {DashboardServer x;x.input["operation"]=1;x.serveCameraFiles(true);const auto id=x.cameraFiles[0].id;
  x.serveCameraFiles(true);assert(x.code==202&&x.response["id"]==id);
  x.input["beforeStart"]=uint64_t(100);x.serveCameraFiles(true);assert(x.code==409&&x.cameraFiles[0].id==id);
  x.input.clear();x.input["operation"]=2;x.serveCameraFiles(true);assert(x.code==409);
  x.input["operation"]=7;x.input["confirm"]="DELETE INTERRUPTED";x.cameras[0].telemetry["filesSupported"]=2;x.serveCameraFiles(true);assert(x.code==409);
 }
 DashboardServer boot;boot.input["operation"]=5;boot.input["beforeName"]="/clips/front_saved";boot.input["beforeSession"]=uint64_t(2);boot.input["beforeStart"]=uint64_t(100);boot.serveCameraFiles(true);assert(boot.code==409&&!boot.download.active()&&!boot.cameraFiles[0].pending);
 DashboardServer d;
 d.web.args.clear();d.serveCameraFiles(false);assert(d.code==200&&d.response["battery"]["percent"]==80&&d.response["cameras"][0]["recording"]==true);d.web.args["camera"]="front";
 d.logged=false;d.serveCameraFiles(false);assert(d.code==401);d.logged=true;
 d.serveCameraFiles(false);assert(d.code==200&&d.response["normalRetained"]==20);
 d.input["operation"]=260;d.serveCameraFiles(true);assert(d.code==400);
 d.input["operation"]=4;d.csrf=false;d.serveCameraFiles(true);assert(d.code==403);d.csrf=true;
 d.serveCameraFiles(true);assert(d.code==400);d.input["confirm"]="DELETE ALL";
 d.coordinator.ready=false;d.serveCameraFiles(true);assert(d.code==409);d.coordinator.ready=true;
 d.serveCameraFiles(true);assert(d.code==202&&d.cameraFiles[0].pending&&!d.cameraFiles[1].pending);const uint32_t id=d.response["id"];
 d.serveCameraFiles(true);assert(d.code==409&&d.cameraFiles[0].id==id);
 JsonDocument offer;d.coordinator.ready=false;offer["command"]["type"]="LOCK";d.offerCameraFiles(0,offer.as<JsonObject>());assert(offer["files"].isNull());offer.clear();
 d.coordinator.ready=false;d.offerCameraFiles(0,offer.to<JsonObject>());assert(offer["files"].isNull());d.coordinator.ready=true;d.offerCameraFiles(0,offer.to<JsonObject>());assert(offer["files"]["id"]==id);
 d.input.clear();d.input["id"]=id;d.input["stream"]=22;d.input["error"]="";d.input["deleted"]=25;
 d.camera=1;d.receiveCameraFiles();assert(d.code==409&&d.cameraFiles[0].pending);d.camera=0;
 auto rows=d.input["clips"].to<JsonArray>();for(unsigned i=0;i<6;++i)rows.add<JsonObject>();d.receiveCameraFiles();assert(d.code==400);d.input.remove("clips");
 d.receiveCameraFiles();assert(d.code==200&&!d.cameraFiles[0].pending&&d.cameraFiles[0].result["deleted"]==25);
 d.web.args["job"]=String(id);d.serveCameraFiles(false);assert(d.code==200&&d.response["deleted"]==25);
 d.serveCameraFiles(false);assert(d.code==200&&d.response["deleted"]==25&&d.cameraFiles[0].result["deleted"]==25);
 d.web.args.erase("job");d.input.clear();d.input["operation"]=1;d.serveCameraFiles(true);assert(d.code==202);
 d.cameras[0].cursor.stream=99;offer.clear();d.offerCameraFiles(0,offer.to<JsonObject>());assert(!d.cameraFiles[0].pending&&d.cameraFiles[0].result["error"].is<const char*>());
 d.cameras[0].telemetry["filesSupported"]=nullptr;d.serveCameraFiles(true);assert(d.code==409);
 clockMs=20000;for(auto& camera:d.cameras)camera.heartbeat(clockMs,15000);
 d.cameras[0].telemetry["filesSupported"]=1;d.cameras[0].cursor.stream=22;d.cameraFiles[0].pending=false;
 d.input.clear();d.input["operation"]=5;d.input["beforeName"]="/clips/front_saved";d.input["beforeSession"]=uint64_t(2);d.input["beforeStart"]=uint64_t(100);
 d.coordinator.ready=false; // Unrelated alert work must also allow starting.
 d.serveCameraFiles(true);assert(d.code==202&&d.download.active());const uint32_t job=d.download.id;
 JsonDocument terminal;terminal["command"]["type"]="LOCK";d.offerCameraFiles(0,terminal.as<JsonObject>());assert(terminal["command"].isNull()&&d.download.state==5);
 // Pending SMS / other-camera work does not cancel a saved-clip transfer.
 d.coordinator.ready=false;terminal.clear();terminal["command"]["type"]="LOCK";d.offerCameraFiles(0,terminal.as<JsonObject>());assert(d.download.state==5&&terminal["command"].isNull());
 // New protection work on this camera still preempts the download.
 d.coordinator.downloadReady=false;terminal.clear();terminal["command"]["type"]="LOCK";d.offerCameraFiles(0,terminal.as<JsonObject>());assert(d.download.state==6&&!terminal["command"].isNull());
 d.coordinator.downloadReady=true;
 d.coordinator.ready=true;d.download.state=5;d.cameraFiles[0].pending=true;
 d.web.args["camera"]="rear";d.serveCameraFiles(true);assert(d.code==409);d.web.args["camera"]="front";
 offer.clear();d.offerCameraFiles(0,offer.to<JsonObject>());assert(offer["download"]["id"]==job&&offer["download"]["leaseMs"]==10000&&strlen(offer["files"]["token"].as<const char*>())==32);
 d.input.clear();d.input["id"]=job;d.input["stream"]=22;d.input["error"]="";d.input["downloadReady"]=true;d.input["downloadSize"]=10000;d.receiveCameraFiles();assert(d.code==200&&d.download.active());
 d.web.args["job"]=String(job);d.serveCameraFiles(false);assert(d.code==200&&d.response["url"].as<String>().length()>32);
 d.actor.csrf="different";d.serveCameraFiles(false);assert(d.response["url"].isNull());d.web.args.erase("job");
 d.input.clear();d.input["operation"]=6;d.input["id"]=job;d.serveCameraFiles(true);assert(d.code==403);
 d.actor=d.security.sessions[0];d.serveCameraFiles(true);assert(d.code==200&&d.download.state==6);
 auto& status=d.cameras[0].telemetry;status["downloadId"]=job;status["downloadState"]=2;status["downloadSent"]=uint64_t(512);status["downloadSize"]=uint64_t(10000);d.observeDownload(0);assert(d.download.state==6);
 status["downloadState"]=3;d.observeDownload(0);assert(!d.download.active()&&d.download.state==3);
 d.download.state=2;d.download.started=1000;d.download.until=901999;assert(d.download.allowance(899001)==0);d.download.tick(901000);assert(!d.download.active());
 d.cameraFiles[0].pending=true;d.cameraFiles[0].operation=5;d.cameraFiles[0].id=d.download.id;
 d.download.state=5;d.download.until=clockMs;d.input.clear();d.input["operation"]=5;d.input["beforeName"]="/clips/front_saved";d.input["beforeSession"]=uint64_t(2);d.input["beforeStart"]=uint64_t(100);d.serveCameraFiles(true);assert(d.code==202&&d.download.active()&&!d.download.startup);
 std::cout<<"PASS actual Main camera handlers: authentication, CSRF, confirmation, incident priority, busy exclusion, role-bound responses, bounded page validation, result polling and camera restart\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'api.cpp';f.write_text(code);exe=p/'api.exe'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'testing/main/host'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(main/'src/web'),str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
