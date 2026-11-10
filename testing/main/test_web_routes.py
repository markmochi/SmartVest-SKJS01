"""Execute production flash lookup for every delivered URL and security boundary."""
from pathlib import Path
import json,subprocess,tempfile

root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
main=root/'SmartVest_SJKS01_Main'
manifest=json.loads((root/'ui/manifest.json').read_text(encoding='utf8'))
source=(main/'src/web/WebPages.cpp').read_text(encoding='utf8').replace('#include "DashboardServer.h"','')
checks=[]
for asset in manifest['assets']:
    checks.append('{'+','.join([json.dumps(asset['path']),json.dumps('text/html; charset=utf-8' if asset['path'].find('/assets/')!=0 else {'js':'text/javascript; charset=utf-8','css':'text/css; charset=utf-8','svg':'image/svg+xml'}[asset['path'].rsplit('.',1)[1]]),f'WEB_ASSET_{asset["payload"]}',str(asset['gzipBytes'])])+'}')
with tempfile.TemporaryDirectory() as temporary:
    folder=Path(temporary)
    code=folder/'routes.cpp'
    code.write_text(r'''
#include <cassert>
#include <map>
#include <string>
#include <iostream>
#define PROGMEM
enum {HTTP_GET,HTTP_HEAD,HTTP_POST};
struct Http {
 int verb=HTTP_GET;bool sent=false,head=false;const uint8_t* data=nullptr;size_t size=0;
 std::string type;std::map<std::string,std::string> headers;
 int method(){return verb;}
 void sendHeader(const char* k,const char* v){headers[k]=v;}
 void sendFlash(const char* t,const uint8_t* p,size_t n,bool h){sent=true;type=t;data=p;size=n;head=h;}
};
struct DashboardServer {
 Http web;bool canonical=true,cached=false;int error=0;
 bool isCanonicalHost(){return canonical;}
 void sendError(int e,const char*){error=e;web.sent=true;}
 void addSecurityHeaders(bool cache){cached=cache;}
 bool sendWebAsset(const char*);
};
'''+source+r'''
int main(){
 struct Expected {const char* url;const char* type;const uint8_t* bytes;size_t size;};
 const Expected cases[]={'''+','.join(checks)+r'''};
 for(const auto& item:cases){
  for(int verb:{HTTP_GET,HTTP_HEAD}){
   DashboardServer s;s.web.verb=verb;assert(s.sendWebAsset(item.url));
   assert(s.web.sent&&!s.error&&s.web.data==item.bytes&&s.web.size==item.size);
   assert(s.web.type==item.type&&s.web.head==(verb==HTTP_HEAD));
   assert(s.cached==(std::strncmp(item.url,"/assets/",8)==0));
   assert(s.web.headers["Content-Encoding"]=="gzip"&&s.web.headers["Vary"]=="Accept-Encoding");
  }
  if(std::strncmp(item.url,"/assets/",8)==0){
   const std::string full=item.url;const auto slash=full.find('/',8);
   DashboardServer retry;const auto alias=full.substr(0,slash+1)+"_r/123/"+full.substr(slash+1);
   assert(retry.sendWebAsset(alias.c_str())&&retry.web.data==item.bytes&&retry.web.size==item.size);
   for(const char* invalid:{"_r//","_r/x/","_r/1234567/","_r/1/_r/2/","_r/1/../"}){
    DashboardServer malformed;const auto bad=full.substr(0,slash+1)+invalid+full.substr(slash+1);
    assert(!malformed.sendWebAsset(bad.c_str())&&!malformed.web.sent);
   }
  }
  DashboardServer denied;denied.canonical=false;assert(denied.sendWebAsset(item.url)&&denied.error==403);
  DashboardServer post;post.web.verb=HTTP_POST;assert(post.sendWebAsset(item.url)&&post.error==405);
  assert(post.web.headers["Allow"]=="GET, HEAD");
 }
 for(const char* url:{"/api/v1/missing","/missing","/assets/old/shared/boot.js","/shared/boot.js","shared/boot.js","/admin/not-a-page"}){
  DashboardServer s;assert(!s.sendWebAsset(url)&&!s.web.sent);
 }
 std::cout<<"PASS all delivered routes and retry aliases: exact version/alias, GET/HEAD, MIME/gzip/body, cache policy, host denial, method rejection and unknown-route fallthrough\n";
}
''',encoding='utf8')
    exe=folder/'test'
    subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(root.parent/'testing/main/host'),'-I'+str(main/'src/web'),str(code),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
