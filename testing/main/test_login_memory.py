"""Execute production asynchronous login routes and verifier with task/network adapters.
Security.cpp and the socket transport are exercised by their separate suites.
"""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[2];main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
text=(main/'src/web/DashboardServer.cpp').read_text(encoding='utf8')
helper=text[text.index('void DashboardServer::sendSession'):text.index('void DashboardServer::sendError')]
routes=re.sub(r'^#include.*\n','',(main/'src/web/Login.cpp').read_text(encoding='utf8'),flags=re.M)
verifier=re.sub(r'^#include.*\n','',(main/'src/security/LoginVerifier.h').read_text(encoding='utf8'),flags=re.M).replace('#pragma once','')
code=r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include "src/core/Models.h"
#include "src/security/Access.h"
#include <atomic>
#include <cassert>
#include <iostream>
uint32_t clockMs=100;
inline void convertFromJson(JsonVariantConst value,String& output){output=value.as<const char*>();}
inline bool canConvertFromJson(JsonVariantConst value,const String&){return value.is<const char*>();}
bool locked=false,taskOK=true;
unsigned workerRuns=0,workerDeletes=0;
void(*queuedTask)(void*)=nullptr;void* taskContext=nullptr;
constexpr int pdPASS=1;
int xTaskCreatePinnedToCore(void(*f)(void*),const char*,unsigned stack,void* context,unsigned priority,void*,unsigned core){
 assert(stack==4096&&priority==0&&core==1&&!queuedTask);
 if(!taskOK)return 0;queuedTask=f;taskContext=context;return pdPASS;
}
void vTaskDelete(void*){++workerDeletes;}
void runTask(){assert(queuedTask&&!locked);auto f=queuedTask;queuedTask=nullptr;++workerRuns;clockMs+=6100;f(taskContext);}
namespace HttpMemoryBudget {bool memoryOK=true;bool available(size_t bytes){assert(bytes==8192);return memoryOK;}}
namespace UtcClock {bool valid(uint64_t value){return value>1600000000000ULL;}}
long long esp_timer_get_time(){return clockMs*1000LL;}
struct Security {
 struct Session {String token=std::string(64,'a'),csrf=std::string(64,'b');Access::Role role=Access::Role::Rider;}session;
 bool blocked=false;unsigned calls=0,failures=0;
 bool throttled(){return blocked||failures>=5;}
 static bool verifyRiderPassword(const String& entered,const RiderCredentials& snapshot){
  assert(!locked);return entered=="admin123"&&snapshot.passwordHash==std::string(64,'h');
 }
 static bool verifyAdminPassword(const String& entered,const RiderCredentials&){assert(!locked);return entered=="ownerpass";}
 static bool factoryRiderAccount(const RiderCredentials&){return true;}
 Session* finishLogin(bool valid,bool developer){
  if(throttled())return nullptr;
  if(!valid){++failures;return nullptr;}
  ++calls;failures=0;session.role=developer?Access::Role::Developer:Access::Role::Rider;return &session;
 }
};
'''+verifier+r'''
struct DashboardServer {
 static JsonDocument makeJson(){return JsonDocument();}
 struct {bool healthy=true;DeviceState data;}store;
 struct Web {
  String cookie;bool connected=true;uint32_t ticket=0;
  void sendHeader(const char*,const String& value){cookie=value;}
  uint32_t defer(){return ++ticket;}
  template<class Reply> bool completeDeferred(uint32_t id,Reply reply){if(!connected||id!=ticket)return false;reply();return true;}
 }web;
 struct {unsigned changes=0;uint64_t utc=0;void set(uint64_t value,uint64_t,int){utc=value;++changes;}}namingClock;
 Security security;LoginVerifier login;JsonDocument input,response;int code=0;bool origin=true;
 DashboardServer(){
  store.data.salt=std::string(32,'s');store.data.passwordHash=std::string(64,'h');
  store.data.riderUsername="admin";store.data.adminUsername="owner";
  for(auto& c:store.data.contacts){c.name=std::string(40,'n');c.phone="+639170000001";}
  for(auto& j:store.data.jobs){j.id=1;j.phone="+639170000001";j.text=std::string(160,'t');j.detail=std::string(160,'d');}
  input["username"]="admin";input["password"]="admin123";input["utcMs"]=1700000000000ULL;
 }
 bool isValidOrigin(){return origin;}bool readJsonBody(JsonDocument& out){out=input;return true;}
 void acquireState(){if(!locked)locked=true;}void releaseState(){locked=false;}
 void sendError(int c,const String&){releaseState();code=c;}
 void sendJson(JsonDocument& out){releaseState();code=200;response=out;}
 void beginLogin();void finishLogin();void sendSession(Security::Session*,bool);
};
'''+routes+helper+r'''
int main(){
 static_assert(sizeof(RiderCredentials)<sizeof(DeviceState)/4,"Exclude device history from login snapshots");
 for(bool developer:{false,true}){
  DashboardServer d;
  if(developer){d.input["username"]="owner";d.input["password"]="ownerpass";d.store.healthy=false;}
  const unsigned before=workerRuns;d.beginLogin();
  assert(!d.code&&!locked&&d.login.busy()&&!d.login.ready()&&queuedTask&&workerRuns==before);
  d.finishLogin();assert(!d.security.calls);
  d.beginLogin();assert(d.code==429&&queuedTask&&workerRuns==before);
  runTask();assert(d.login.ready());d.finishLogin();assert(d.code==200&&!locked&&d.security.calls==1&&!d.login.busy());
  assert(d.response["role"]==(developer?"developer":"rider")&&d.web.cookie.startsWith("SVSID="));
  assert(d.namingClock.changes==1&&d.namingClock.utc==1700000006100ULL&&d.login.lastMs==6100);
  assert(!d.login.credentials.passwordHash.length()&&!d.login.credentials.adminHash.length());
  for(const auto& j:d.store.data.jobs)assert(j.text.length()==160&&j.detail.length()==160);
  d.finishLogin();assert(d.security.calls==1);
 }
 {DashboardServer d;d.origin=false;d.beginLogin();assert(d.code==403&&!queuedTask&&!locked);}
 {DashboardServer d;d.store.healthy=false;d.beginLogin();assert(d.code==503&&!queuedTask&&!locked);}
 {DashboardServer d;d.security.blocked=true;d.beginLogin();assert(d.code==429&&!queuedTask&&!locked);}
 {DashboardServer d;d.input["password"]="wrong";d.beginLogin();runTask();d.finishLogin();assert(d.code==401&&d.security.failures==1&&!locked);}
 {DashboardServer d;d.beginLogin();d.store.data.salt=std::string(32,'x');runTask();d.finishLogin();assert(d.code==409&&!d.security.calls&&!locked);}
 {DashboardServer d;d.input["username"]="owner";d.input["password"]="ownerpass";d.beginLogin();d.store.data.adminHash=std::string(64,'x');runTask();d.finishLogin();assert(d.code==409&&!d.security.calls&&!locked);}
 {DashboardServer d;d.beginLogin();d.web.connected=false;runTask();d.finishLogin();assert(!d.security.calls&&!d.login.busy()&&!locked);}
 {DashboardServer d;d.beginLogin();d.store.healthy=false;runTask();d.finishLogin();assert(d.code==503&&!d.security.calls&&!locked);}
 {DashboardServer d;HttpMemoryBudget::memoryOK=false;d.beginLogin();assert(d.code==503&&!queuedTask&&!d.login.busy()&&!locked);HttpMemoryBudget::memoryOK=true;d.beginLogin();runTask();d.finishLogin();assert(d.code==200);}
 {DashboardServer d;taskOK=false;d.beginLogin();assert(d.code==503&&!queuedTask&&!d.login.busy()&&!locked);taskOK=true;d.beginLogin();runTask();d.finishLogin();assert(d.code==200);}
 {DashboardServer d;d.input["password"]="wrong";d.web.connected=false;for(unsigned n=0;n<5;++n){d.beginLogin();runTask();d.finishLogin();}assert(d.security.failures==5);d.beginLogin();assert(d.code==429&&!queuedTask);}
 for(const char* key:{"username","password"}){DashboardServer d;d.input[key]=std::string(key==std::string("username")?33:65,'x');d.beginLogin();assert(d.code==400&&!queuedTask);}
 for(const char* key:{"username","password"}){DashboardServer d;d.input[key]=std::string("admin\0hidden",12);d.beginLogin();assert(d.code==400&&!queuedTask);}
 assert(workerRuns==workerDeletes&&!queuedTask);
 std::cout<<"PASS production deferred login and verifier: independent worker, one pending job, hardware mutex released, both roles, full histories excluded, cookie/clock, credential changes/reset, disconnect, throttle, allocation/task failure and recovery, credential cleanup\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'login.cpp').write_text(code,encoding='utf8')
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(root/'testing/main/host'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(main),str(p/'login.cpp'),'-o',str(p/'login')],check=True)
 subprocess.run([str(p/'login')],check=True)
