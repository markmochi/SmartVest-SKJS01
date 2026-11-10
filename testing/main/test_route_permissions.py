"""Verify shared route callbacks retain the preceding per-route permission policy."""
from pathlib import Path
import json,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
MAIN=ROOT/'Main_and_Cameras/SmartVest_SJKS01_Main'
EXPECTED=json.loads(r'''[
  {
    "path": "/api/v1/login",
    "method": "HTTP_POST",
    "handler": "beginLogin",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/status",
    "method": "HTTP_GET",
    "handler": "sendStatus",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/rider/pose",
    "method": "HTTP_GET",
    "handler": "sendRiderPose",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/rider/calibration",
    "method": "HTTP_POST",
    "handler": "commandRiderCalibration",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/sensing/imu",
    "method": "HTTP_GET",
    "handler": "sendImuLab",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/sensing/command",
    "method": "HTTP_POST",
    "handler": "commandImuLab",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/sensing/piezo",
    "method": "HTTP_POST",
    "handler": "commandPiezo",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/gsm/trace",
    "method": "HTTP_GET",
    "handler": "sendModemTrace",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/settings",
    "method": "HTTP_GET",
    "handler": "sendSettings",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/settings",
    "method": "HTTP_POST",
    "handler": "saveSettings",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/messages/cancel",
    "method": "HTTP_POST",
    "handler": "cancelMessages",
    "role": "Rider",
    "gated": true
  },
  {
    "path": "/api/v1/jobs",
    "method": "HTTP_GET",
    "handler": "sendJobs",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/messages/check",
    "method": "HTTP_POST",
    "handler": "queueContactCheck",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/messages/introduction",
    "method": "HTTP_POST",
    "handler": "queueIntroduction",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/jobs/action",
    "method": "HTTP_POST",
    "handler": "handleJobAction",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/security/password",
    "method": "HTTP_POST",
    "handler": "changePassword",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/integrations",
    "method": "HTTP_GET",
    "handler": "sendIntegrations",
    "role": "Developer",
    "gated": true
  },
  {
    "path": "/api/v1/cameras/files/result",
    "method": "HTTP_POST",
    "handler": "receiveCameraFiles",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/cameras/monitor",
    "method": "HTTP_GET",
    "handler": "sendCameraMonitor",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/cameras/session",
    "method": "HTTP_GET",
    "handler": "cameraSession",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/cameras/heartbeat",
    "method": "HTTP_POST",
    "handler": "cameraHeartbeat",
    "role": "Developer",
    "gated": false
  },
  {
    "path": "/api/v1/rider/status",
    "method": "HTTP_GET",
    "handler": "sendRiderStatus",
    "role": "Rider",
    "gated": true
  },
  {
    "path": "/api/v1/rider/settings",
    "method": "HTTP_GET",
    "handler": "sendSettings",
    "role": "Rider",
    "gated": true
  },
  {
    "path": "/api/v1/rider/settings",
    "method": "HTTP_POST",
    "handler": "saveSettings",
    "role": "Rider",
    "gated": true
  },
  {
    "path": "/api/v1/admin/reset-data",
    "method": "HTTP_POST",
    "handler": "resetData",
    "role": "Developer",
    "gated": true
  }
]''')
source=(MAIN/'src/web/DashboardServer.cpp').read_text('utf8')
rider=(MAIN/'src/web/rider/RiderDashboard.cpp').read_text('utf8')
found=[]
pattern=r'bindRoute\("([^"]+)",\s*(HTTP_GET|HTTP_POST),\s*&DashboardServer::(\w+)(?:,Access::Role::(\w+))?(?:,(false|true))?\);'
for m in re.finditer(pattern,source+rider):
 path,method,handler,role,gate=m.groups()
 found.append(dict(path=path,method=method,handler=handler,role=role or 'Developer',gated=gate!='false'))
assert found==EXPECTED,'A shared route changed its path, handler, role or authorization gate'
helper=source[source.index('void DashboardServer::bindRoute('):source.index('// Both camera views')]
methods=sorted({x['handler'] for x in EXPECTED})
code=r'''
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <iostream>
enum HTTPMethod {HTTP_GET,HTTP_POST};
namespace Access {enum class Role:unsigned char {Rider,Developer};}
struct DashboardServer {
 struct {std::map<std::pair<std::string,HTTPMethod>,std::function<void()>> handlers;
 void on(const char* path,HTTPMethod method,std::function<void()> f){handlers[{path,method}]=std::move(f);}}web;
 bool granted=true,lastWrite=false;unsigned authCalls=0,hits=0;Access::Role lastRole=Access::Role::Rider;
 bool authorize(bool write,Access::Role role){++authCalls;lastWrite=write;lastRole=role;return granted;}
 void bindRoute(const char*,HTTPMethod,void(DashboardServer::*)(),Access::Role=Access::Role::Developer,bool=true);
'''+'\n'.join(' void '+name+'(){++hits;}' for name in methods)+r'''
};
'''+helper+r'''
int main(){
 struct Case {const char* path;HTTPMethod method;void(DashboardServer::*handler)();Access::Role role;bool gated;};
 const Case cases[]={
'''+',\n'.join('{'+json.dumps(x['path'])+','+x['method']+',&DashboardServer::'+x['handler']+',Access::Role::'+x['role']+','+str(x['gated']).lower()+'}' for x in EXPECTED)+r'''
 };
 DashboardServer d;
 for(const auto& c:cases)d.bindRoute(c.path,c.method,c.handler,c.role,c.gated);
 for(const auto& c:cases)for(bool grant:{false,true}){
  d.granted=grant;d.authCalls=d.hits=0;d.web.handlers.at({c.path,c.method})();
  assert(d.hits==unsigned(!c.gated||grant));assert(d.authCalls==unsigned(c.gated));
  if(c.gated){assert(d.lastRole==c.role&&d.lastWrite==(c.method==HTTP_POST));}
 }
 std::cout<<"PASS 25 production route bindings preserve role/write checks, authenticated rejection, self-authenticated camera/rider handlers and independent callback captures\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code,'utf8')
 subprocess.run(['g++','-std=c++17','-UNDEBUG',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
