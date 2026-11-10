"""Execute the saved LED route with real authorization and power service."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
server=(main/'src/web/DashboardServer.cpp').read_text(encoding='utf8')
auth=server[server.index('Security::Session* DashboardServer::authorize('):server.index('bool DashboardServer::readJsonBody(')]
route=server[server.index('  web.on("/api/v1/power/led-test"'):server.index('  bindRoute("/api/v1/settings"')]
route=route[route.index('[this]() {')+len('[this]() {'):route.rindex('});')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);source=p/'led.cpp'
 source.write_text(r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include "src/power/BatteryService.h"
#include "src/security/Access.h"
#include <cassert>
#include <iostream>
inline void convertFromJson(JsonVariantConst value,String& output){output=value.as<const char*>();}
uint32_t clockMs=1000;
struct Web{String csrf="matching";String header(const char*)const{return csrf;}};
struct Security{
 struct Session{Access::Role role=Access::Role::Developer;String csrf="matching";}session;
 bool signedIn=true;Session* current(Web&){return signedIn?&session:nullptr;}
 static bool equal(const String&a,const String&b){return a==b;}
};
struct DashboardServer{
 static JsonDocument makeJson(){return JsonDocument();}
 Security security;Web web;BatteryService battery;JsonDocument body,response;int code=0;String error;
 bool canonical=true,origin=true,locked=false;
 bool isCanonicalHost(){return canonical;}bool isValidOrigin(){return origin;}void acquireState(){locked=true;}
 Security::Session* authorize(bool write=false,Access::Role required=Access::Role::Developer);
 bool readJsonBody(JsonDocument& out){out=body;return true;}
 void sendError(int c,const String&e){code=c;error=e;}
 void sendJson(JsonDocument& out,int c=200){code=c;response=out;}
 void led();DashboardServer(){body["mode"]="critical";}
};
''' + auth + '\nvoid DashboardServer::led(){'+route+'}\n'+r'''
int main(){
 {DashboardServer d;d.led();assert(d.code==200&&d.locked&&d.battery.ledTestActive());
  assert(d.response["power"]["ledTestMode"]=="critical");
  clockMs+=15000;assert(!d.battery.ledTestActive());
  d.body["mode"]="blue";d.led();assert(d.battery.ledTestBlue());
  d.body["mode"]="stop";d.led();assert(!d.battery.ledTestActive());}
 for(int bad=0;bad<7;++bad){DashboardServer d;
  if(bad==0)d.security.signedIn=false;
  if(bad==1)d.security.session.role=Access::Role::Rider;
  if(bad==2)d.web.csrf="wrong";
  if(bad==3)d.origin=false;
  if(bad==4)d.canonical=false;
  if(bad==5)d.body["extra"]=true;
  if(bad==6)d.body["mode"]="invalid";
  d.led();assert(d.code>=400&&!d.battery.ledTestActive());
 }
 std::cout<<"PASS actual LED POST route: administrator only, session/origin/CSRF/host, strict body, modes, stop and expiry\n";
}
''',encoding='utf8')
 out=p/'led'
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(root/'testing/main/host'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(main),str(main/'src/power/BatteryService.cpp'),str(source),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
