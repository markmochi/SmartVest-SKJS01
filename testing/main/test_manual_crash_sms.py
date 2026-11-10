"""Execute the saved DashboardServer manual-message method with real storage.
Only HTTP response/body and physical modem/GPS are adapted. No real SMS is sent.
Run with the Windows host runner (ArduinoJson plus its crypto adapter).
"""
from pathlib import Path
import os,shutil,subprocess,tempfile
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
headers=Path(os.environ['SJKS_ARDUINOJSON'])
source=(r/'SmartVest_SJKS01_Main/src/web/DashboardServer.cpp').read_text(encoding='utf8')
method=source[source.index('void DashboardServer::queueManualMessage('):source.index('void DashboardServer::handleJobAction(')]
assert 'if (authorize(true)) queueManualMessage("manual_crash");' in source
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);s=p/'sketch';h=p/'host';shutil.copytree(r/'SmartVest_SJKS01_Main',s);shutil.copytree(r/'../testing/main/host',h)
 (h/'mbedtls').mkdir()
 (h/'esp_system.h').write_text('#pragma once\n#include <cstdint>\ninline uint32_t esp_random(){static uint32_t seed=42;seed=seed*1664525u+1013904223u;return seed>>24;}\n',encoding='utf8')
 (h/'WebServer.h').write_text('#pragma once\n#include "Arduino.h"\nclass WebServer{public:String cookie;String header(const char*){return cookie;}};\n',encoding='utf8')
 (h/'mbedtls/md.h').write_text('#pragma once\n#define MBEDTLS_MD_SHA256 6\n',encoding='utf8')
 (h/'mbedtls/pkcs5.h').write_text('#pragma once\n#include <openssl/evp.h>\ninline int mbedtls_pkcs5_pbkdf2_hmac_ext(int,const unsigned char* password,size_t plen,const unsigned char* salt,size_t slen,unsigned long rounds,size_t length,unsigned char* output){return PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password),plen,salt,slen,rounds,EVP_sha256(),length,output)==1?0:-1;}\n',encoding='utf8')
 program=p/'manual.cpp';program.write_text(r'''
#include <cassert>
#include <iostream>
#include "src/storage/StateStore.h"
#include "src/core/Domain.h"
uint32_t clockMs=100000;
struct DashboardServer {
 static JsonDocument makeJson(){return JsonDocument();}
 StateStore store;JsonDocument body,response;int code=0;String error;
 bool manualMessageSent=false;uint32_t lastManualMessage=0;
 struct GPS {bool present=true,fresh=true;uint32_t age=0;
  bool latestLocation(double& a,double& b,uint32_t& ms,bool& f){a=14.5;b=121;ms=age;f=fresh;return present;}
 } neo6m;
 struct Modem {bool registered(){return true;}String registrationText(){return "Registered";}} sim800l;
 bool readJsonBody(JsonDocument& out){out=body;return true;}
 void sendError(int c,const String& e){code=c;error=e;}
 void sendJson(JsonDocument& out,int c=200){code=c;response=out;}
 void queueManualMessage(const String& kind);
 DashboardServer(){MockNvs::values.clear();MockNvs::failSettings=false;assert(store.begin());
  auto next=store.data;next.rider="Alex";next.contacts[0].name="Jamie";next.contacts[0].phone="+639170000001";assert(store.commit(next));body["phone"]="+639170000001";}
 const SmsJob* queued()const {for(const auto& j:store.data.jobs)if(j.id)return &j;return nullptr;}
};
''' + method + r'''
int main(){
 {DashboardServer d;d.queueManualMessage("manual_crash");assert(d.code==202);
  const auto* job=d.queued();assert(job&&job->phone=="+639170000001"&&job->kind=="manual_crash");
  assert(!job->eventBoot&&!job->eventSequence);
  assert(job->text==Domain::crashMessage("Alex",Domain::smsLocation(14.5,121),true).c_str());
  assert(!job->text.startsWith(Config::SMS_TEST_HEADER));
  const String captured=job->text;d.neo6m.present=false;assert(job->text==captured);
  assert(d.response["locationAttached"].as<bool>()&&d.response["locationFresh"].as<bool>());
  d.queueManualMessage("manual_crash");assert(d.code==429);
  d.manualMessageSent=false;d.queueManualMessage("manual_crash");assert(d.code==409);}
 {DashboardServer d;d.neo6m.fresh=false;d.neo6m.age=60000;d.queueManualMessage("manual_crash");
  assert(d.code==202&&d.response["locationAttached"].as<bool>()&&!d.response["locationFresh"].as<bool>());
  assert(d.queued()->text==Domain::crashMessage("Alex",Domain::smsLocation(14.5,121),false).c_str());}
 {DashboardServer d;d.neo6m.age=Config::GPS_LAST_LOCATION_MAX_MS+1;d.queueManualMessage("manual_crash");
  assert(d.code==202&&!d.response["locationAttached"].as<bool>());
  assert(d.queued()->text==Domain::crashMessage("Alex","",false).c_str());}
 {DashboardServer d;d.body["phone"]="+639170000099";d.queueManualMessage("manual_crash");assert(d.code==400&&!d.queued());}
 {DashboardServer d;d.store.data.rider="";d.queueManualMessage("manual_crash");assert(d.code==400&&!d.queued());}
 {DashboardServer d;for(auto& j:d.store.data.jobs){j.id=100;j.phone="+639170000099";j.text="Waiting";j.kind="addition";j.state="queued";}
  d.queueManualMessage("manual_crash");assert(d.code==409);}
 {DashboardServer d;MockNvs::failSettings=true;d.queueManualMessage("manual_crash");assert(d.code==503&&!d.queued()&&!d.manualMessageSent);MockNvs::failSettings=false;}
 {DashboardServer d;d.queueManualMessage("addition");assert(d.code==202&&d.queued()->text==Domain::invitationMessage("Alex").c_str());}
 std::cout<<"PASS actual manual-message method with real storage: selected contact, normal crash text, frozen fresh/stale/missing GPS, no incident identity, cooldown/duplicate/unknown recipient/queue reserve/storage failure and introductions\n";
}
''',encoding='utf8')
 command=['g++','-std=c++17','-I'+str(h),'-I'+str(headers),'-I'+str(s),str(s/'src/security/Security.cpp'),str(s/'src/storage/StateStore.cpp'),str(program),'-lcrypto','-o',str(p/'test')]
 subprocess.run(command,check=True);subprocess.run([str(p/'test')],check=True)
