"""Exercise actual portal response methods; phone OS detection is not simulated."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
source=(r/'SmartVest_SJKS01_Main/src/web/DashboardServer.cpp').read_text(encoding='utf8')
methods=source[source.index('void DashboardServer::redirectToDashboard()'):source.index('void DashboardServer::sendAdminPage()')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);code=p/'signin.cpp'
 code.write_text(r'''
#include <cassert>
#include <map>
#include <string>
#include <iostream>
#define PROGMEM
struct Network {bool portal=true;bool captivePortalActive()const{return portal;}};
struct Http {
 int status=0;std::string type,body,asset;std::map<std::string,std::string> headers;
 void sendHeader(const char* k,const char* v,bool=false){headers[k]=v;}
 void send(int s,const char* t,const char* b){status=s;type=t;body=b;}
 void send_P(int s,const char* t,const char* b){send(s,t,b);}
};
struct DashboardServer {
 Network network;Http web;bool canonical=true;
 bool isCanonicalHost()const{return canonical;}
 void sendError(int status,const char* message){web.send(status,"application/json",message);}
 void sendWebAsset(const char* path){web.asset=path;web.status=200;}
 void redirectToDashboard();void sendAndroidCaptivePage();void sendPortalPage();
};
'''+methods+r'''
int main(){
 DashboardServer standard;standard.redirectToDashboard();
 assert(standard.web.status==302);
 assert(standard.web.headers["Location"]=="http://192.168.4.1/");
 assert(standard.web.headers["Cache-Control"].find("no-store")!=std::string::npos);
 assert(standard.web.headers["Connection"]=="close");
 DashboardServer android;android.sendAndroidCaptivePage();
 assert(android.web.status==200 && android.web.type=="text/html");
 assert(android.web.body.find("0;url=http://192.168.4.1/")!=std::string::npos);
 assert(android.web.body.find("href=\"http://192.168.4.1/\"")!=std::string::npos);
 assert(android.web.body.find("Success")==std::string::npos);
 assert(!android.web.headers.count("Location"));
 DashboardServer dashboard;dashboard.sendPortalPage();
 assert(dashboard.web.status==200 && dashboard.web.asset=="/");
 DashboardServer foreign;foreign.canonical=false;foreign.sendPortalPage();
 assert(foreign.web.status==403 && foreign.web.asset.empty());
 DashboardServer station;station.network.portal=false;station.redirectToDashboard();
 assert(station.web.status==403 && !station.web.headers.count("Location"));
 std::cout<<"PASS actual portal methods: OS redirect/Android HTML target dashboard root directly, no extra landing page or internet-success reply; canonical-host and home-mode guards\n";
}
''',encoding='utf8')
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',str(code),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
