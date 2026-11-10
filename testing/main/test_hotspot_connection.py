"""Compile the real connectivity handler with HTTP adapters; no physical Wi-Fi."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
s=r/'SmartVest_SJKS01_Main'
source=(s/'src/web/DashboardServer.cpp').read_text(encoding='utf8')
handler=source[source.index('bool DashboardServer::sendConnectivityCheck()'):source.index('void DashboardServer::redirectToDashboard()')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);code=p/'hotspot.cpp'
 code.write_text('''#include <cassert>
#include <map>
#include <string>
#include <iostream>
#include "src/network/HotspotConnectivity.h"
enum {HTTP_GET,HTTP_HEAD,HTTP_POST};
struct Text:std::string{using std::string::string;};
struct Network {bool enabled=true;bool localDeviceMode()const{return enabled;}};
struct Http {
 Text host="connectivitycheck.gstatic.com",path="/generate_204";int verb=HTTP_GET,status=0;
 std::string type,body;std::map<std::string,std::string> headers;
 Text hostHeader()const{return host;}Text uri()const{return path;}int method()const{return verb;}
 void sendHeader(const char* k,const char* v){headers[k]=v;}
 void send(int s,const char* t,const char* b){status=s;type=t;body=b;}
};
struct DashboardServer {Network network;Http web;bool sendConnectivityCheck();};
'''+handler+r'''
int main(){
 struct Case {const char* host;const char* path;int status;const char* expected;};
 const Case cases[]={
  {"connectivitycheck.gstatic.com","/generate_204",204,""},
  {"connectivitycheck.platform.hicloud.com","/generate_204",204,""},
  {"connect.rom.miui.com","/generate_204",204,""},
  {"clients3.google.com","/gen_204",204,""},
  {"captive.apple.com","/hotspot-detect.html",200,"Success"},
  {"www.apple.com","/library/test/success.html",200,"Success"},
  {"CAPTIVE.APPLE.COM:80","/",200,"Success"},
  {"www.appleiphonecell.com","/",200,"Success"},
  {"www.msftconnecttest.com","/connecttest.txt",200,"Microsoft Connect Test"},
  {"www.msftncsi.com","/ncsi.txt",200,"Microsoft NCSI"},
  {"detectportal.firefox.com","/success.txt",200,"success\n"}};
 for(const auto& c:cases){DashboardServer d;d.web.host=c.host;d.web.path=c.path;
  assert(d.sendConnectivityCheck());assert(d.web.status==c.status);
  if(c.status==204)assert(d.web.body.empty());else assert(d.web.body.find(c.expected)!=std::string::npos);
  assert(d.web.headers["Cache-Control"]=="no-store" && d.web.headers["Connection"]=="close");
  assert(!d.web.headers.count("Location"));
  d.web.verb=HTTP_HEAD;assert(d.sendConnectivityCheck());assert(d.web.status==c.status&&d.web.body.empty());
  d.web.verb=HTTP_POST;assert(d.sendConnectivityCheck());assert(d.web.status==405&&d.web.headers["Allow"]=="GET, HEAD");
 }
 for(const auto* path:{"/","/admin","/api/v1/session","/api/v1/crash","/api/v1/cameras/commands","/assets/x.js","/generate_204/extra","/unknown"}){
  DashboardServer d;d.web.host="192.168.4.1";d.web.path=path;assert(!d.sendConnectivityCheck());assert(!d.web.status);
 }
 for(const auto* host:{"captive.apple.com.evil.test","captive.apple.com:81","smartvest.local","example.com"}){
  DashboardServer d;d.web.host=host;d.web.path="/";assert(!d.sendConnectivityCheck());
 }
 DashboardServer disabled;disabled.network.enabled=false;assert(!disabled.sendConnectivityCheck());assert(!disabled.web.status);
 assert(!HotspotConnectivity::lookup(nullptr,"/generate_204"));
 assert(!HotspotConnectivity::lookup("host",nullptr));
 std::cout<<"PASS actual hotspot HTTP handler: Android/Apple/Windows/Firefox responses; GET/HEAD; POST rejected; no redirects/cache; dashboard/API/foreign-host isolation; disabled mode\n";
}
''',encoding='utf8')
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(s),str(code),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
