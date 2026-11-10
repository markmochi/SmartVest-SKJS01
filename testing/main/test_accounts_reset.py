"""Actual account/reset routes with controlled storage, crypto and coordinator outcomes."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2];main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
source=(main/'src/web/rider/RiderDashboard.cpp').read_text(encoding='utf8')
routes=source[source.index('void DashboardServer::registerRiderRoutes'):source.index('void DashboardServer::sendRiderStatus')]
methods=source[source.index('void DashboardServer::sendRiderAccount'):]
binding=(main/'src/web/DashboardServer.cpp').read_text('utf-8')
binding=binding[binding.index('void DashboardServer::bindRoute('):binding.index('// Both camera views')]

code=r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include "src/core/Models.h"
#include "src/core/Domain.h"
#include "src/security/Access.h"
#include "src/web/CameraTelemetry.h"
#include <cassert>
#include <functional>
#include <map>
#include <iostream>
uint32_t clockMs=100;
inline void convertFromJson(JsonVariantConst v,String& out){out=v.as<const char*>();}
inline bool canConvertFromJson(JsonVariantConst v,const String&){return v.is<const char*>();}
bool locked=false;
inline String operator+(const String& s,size_t n){return s+String(unsigned(n));}
struct Security {
 struct Session {Access::Role role=Access::Role::Rider;String token;};
 Session sessions[2];unsigned failures=0;uint32_t lastFailure=0;bool blocked=false;
 bool throttled(){return blocked;}
 static bool verifyAdminPassword(const String& p,const RiderCredentials&){assert(!locked);return p=="admin-secret";}
 static bool verifyRiderPassword(const String& p,const RiderCredentials&){assert(!locked);return p=="rider-secret";}
 static String randomHex(size_t n){return std::string(n*2,'a');}
 static String hashPassword(const String&,const String&){return std::string(64,'b');}
 static bool validNewRiderPassword(const String& p){return p.length()>=12&&p.length()<=64;}
 void clearRiderSessions(){for(auto& s:sessions)if(s.role==Access::Role::Rider)s=Session();}
};
constexpr int HTTP_GET=0,HTTP_POST=1;using HTTPMethod=int;
struct DashboardServer {
 static JsonDocument makeJson(){return JsonDocument();}
 void beginLogin(){}
 void cameraHeartbeat(){}
 void cameraSession(){}
 void cancelMessages(){}
 void changePassword(){}
 void commandImuLab(){}
 void commandPiezo(){}
 void commandRiderCalibration(){}
 void handleJobAction(){}
 void queueContactCheck(){}
 void queueIntroduction(){}
 void receiveCameraFiles(){}
 void sendCameraMonitor(){}
 void sendImuLab(){}
 void sendIntegrations(){}
 void sendJobs(){}
 void sendModemTrace(){}
 void sendRiderPose(){}
 void sendStatus(){}
 void bindRoute(const char*,HTTPMethod,void(DashboardServer::*)(),Access::Role=Access::Role::Developer,bool=true);
 struct Store {DeviceState data;bool fail=false;unsigned commits=0;bool commit(const DeviceState& next){++commits;if(fail)return false;data=next;return true;}}store;
 struct Emergency {Store& store;bool ready=true,fail=false;bool canResetData(){return ready;}bool finishDataReset(){if(fail)return false;store.data.dataResetPending=false;return true;}}coordinator{store},*emergency=&coordinator;
 struct {bool busy=false;bool active(){return busy;}}download;
 struct {bool busy=false;bool transactionActive(){return busy;}}sim800l;
 struct Web {std::map<std::pair<std::string,int>,std::function<void()>> handlers;String cookie;
 void on(const char* p,int m,std::function<void()> f){handlers[{p,m}]=f;}void sendHeader(const char*,const String& s){cookie=s;}}web;
 Security security;CameraMonitor::Journal cameraLogs;uint32_t diagnosticEpoch=1;
 Access::Role role=Access::Role::Developer;bool logged=true,csrf=true;
 JsonDocument input,response;int code=0;
 DashboardServer(){security.sessions[0].role=Access::Role::Developer;security.sessions[0].token="dev";security.sessions[1].token="rider";store.data.rider="Alex";store.data.salt=std::string(32,'s');store.data.passwordHash=std::string(64,'h');}
 Security::Session* authorize(bool write=false,Access::Role required=Access::Role::Developer){
  if(!logged){sendError(401,"");return nullptr;}if(!Access::allowed(role,required)|| (write&&!csrf)){sendError(403,"");return nullptr;}locked=true;return &security.sessions[role==Access::Role::Developer?0:1];
 }
 bool readJsonBody(JsonDocument& out){out=input;return true;}
 void acquireState(){locked=true;}void releaseState(){locked=false;}
 void sendError(int c,const String&){code=c;releaseState();}
 void sendJson(JsonDocument& d,int status=200){response=d;code=status;releaseState();}
 void sendSettings(){}void saveSettings(){}void sendRiderStatus(){}
 void registerRiderRoutes();void sendRiderAccount(bool=false);void changeRiderAccount(bool=false);void resetData();void completeRiderSetup();
 void call(const char* path,int method){code=0;web.handlers.at({path,method})();assert(!locked);}
};
''' +binding+routes+methods+r'''
int main(){
 {DashboardServer d;d.registerRiderRoutes();d.call("/api/v1/admin/account",HTTP_GET);assert(d.response["username"]==Config::DEVELOPER_USERNAME);
 d.call("/api/v1/rider/account",HTTP_GET);assert(d.code==403);d.role=Access::Role::Rider;d.call("/api/v1/rider/account",HTTP_GET);assert(d.response["username"]=="admin");d.call("/api/v1/admin/account",HTTP_GET);assert(d.code==403);d.call("/api/v1/admin/reset-data",HTTP_POST);assert(d.code==403);}
 for(bool admin:{false,true}){DashboardServer d;d.registerRiderRoutes();d.role=admin?Access::Role::Developer:Access::Role::Rider;
 d.input["username"]=admin?"owner":"newrider";d.input["current"]=admin?"admin-secret":"rider-secret";d.input["password"]="private-password";
 d.call(admin?"/api/v1/admin/account":"/api/v1/rider/account",HTTP_POST);assert(d.code==200);
 assert(admin?d.store.data.adminUsername=="owner"&&d.store.data.riderUsername=="admin":d.store.data.riderUsername=="newrider"&&d.store.data.adminUsername==Config::DEVELOPER_USERNAME);
 assert(admin?d.security.sessions[0].token.length()==0&&d.security.sessions[1].token.length():d.security.sessions[1].token.length()==0&&d.security.sessions[0].token.length());}
 for(int fault=0;fault<9;fault++){DashboardServer d;d.registerRiderRoutes();d.input["current"]="admin-secret";d.input["confirm"]="RESET";
 d.store.data.adminUsername="owner";d.store.data.adminHash=std::string(64,'x');d.store.data.nextJob=900;d.store.data.contacts[0].phone="+639170000001";d.cameraLogs.count=5;d.store.data.riderSetupComplete=true;
 if(fault==1)d.csrf=false;if(fault==2)d.input["current"]="wrong";if(fault==3)d.input["confirm"]="wrong";if(fault==4)d.coordinator.ready=false;if(fault==5)d.sim800l.busy=true;if(fault==6)d.store.fail=true;if(fault==7)d.coordinator.fail=true;if(fault==8)d.download.busy=true;
 d.call("/api/v1/admin/reset-data",HTTP_POST);
 if((fault>0&&fault<7)||fault==8){assert(d.code!=200&&d.store.data.rider=="Alex"&&d.cameraLogs.count==5);continue;}
 assert(d.code==(fault==7?202:200));assert(d.store.data.dataResetPending==(fault==7));assert(d.store.data.rider.length()==0&&d.store.data.contacts[0].phone.length()==0);
 assert(d.store.data.adminUsername=="owner"&&d.store.data.adminHash==std::string(64,'x'));assert(d.store.data.riderUsername==Config::DEFAULT_RIDER_USERNAME&&d.store.data.nextJob==900);
 assert(!d.store.data.riderSetupComplete);assert(d.cameraLogs.count==0&&d.diagnosticEpoch==2&&d.security.sessions[0].token.length()&&d.security.sessions[1].token.length()==0);
 }
 {DashboardServer d;d.registerRiderRoutes();d.input["current"]="admin-secret";d.input["confirm"]="RESET";d.store.data.jobs[0].id=1;d.store.data.jobs[0].state="queued";d.call("/api/v1/admin/reset-data",HTTP_POST);assert(d.code==409&&d.store.commits==0);}
 {DashboardServer d;d.registerRiderRoutes();d.input["revision"]=0U;d.call("/api/v1/rider/setup",HTTP_POST);assert(d.code==403);
 d.role=Access::Role::Rider;d.call("/api/v1/rider/setup",HTTP_POST);assert(d.code==400&&!d.store.data.riderSetupComplete);
 d.store.data.contacts[0].phone="+639170000001";d.store.fail=true;d.call("/api/v1/rider/setup",HTTP_POST);assert(d.code==503&&!d.store.data.riderSetupComplete);
 d.store.fail=false;d.call("/api/v1/rider/setup",HTTP_POST);assert(d.code==200&&d.store.data.riderSetupComplete);
 d.call("/api/v1/rider/setup",HTTP_POST);assert(d.code==409);}
 std::cout<<"PASS production role-specific account/reset routes: authorization, CSRF, passwords, role session invalidation, active-work refusal, storage failure, reset marker, profile/history clearing and administrator preservation\n";
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(code,encoding='utf8')
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(root/'testing/main/host'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(main),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
