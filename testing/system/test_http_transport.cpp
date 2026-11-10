#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/web/ReadyWebServer.h"
#include "../../common/CameraAuth.h"
#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/web/generated/WebPolicy.h"
#include <cassert>
#include <iostream>
#include <sstream>
namespace {
std::string request(const char* method,const std::string& path,const std::string& body="",const std::string& extra=""){
 std::string wire=std::string(method)+" "+path+" HTTP/1.1\r\nHost: 192.168.4.1\r\n"+extra;
 if(std::strcmp(method,"POST")==0||!body.empty())wire+="Content-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\n";
 return wire+"\r\n"+body;
}
void configure(ReadyWebServer& web){const char* fields[]={"Cookie","X-CSRF-Token","Origin","Content-Type","X-SV-MAC"};web.collectHeaders(fields,5);}
void drain(ReadyWebServer& web,const std::shared_ptr<HostSocket>& peer,unsigned limit=20000,uint32_t step=1){
 for(unsigned n=0;n<limit&&peer->alive;++n){const uint32_t before=hostNow;web.handleClient();assert(hostNow==before);hostNow+=step;}
 assert(!peer->alive);
}
std::string body(const std::shared_ptr<HostSocket>& peer){auto at=peer->output.find("\r\n\r\n");assert(at!=std::string::npos);return peer->output.substr(at+4);}
void status(const std::shared_ptr<HostSocket>& peer,int code){const std::string prefix="HTTP/1.1 "+std::to_string(code)+" ";if(peer->output.rfind(prefix,0)!=0)std::cerr<<"Expected "<<code<<" got "<<peer->output.substr(0,120)<<"\n";assert(peer->output.rfind(prefix,0)==0);}
}
int main(){
 struct ListenerProbe:ReadyWebServer{ListenerProbe():ReadyWebServer(80){}void ready(bool value){_server.ready=value;}};
 {ListenerProbe listener;assert(listener.listening());listener.ready(false);assert(!listener.listening());}

 // A slow password derivation owns one bounded socket, never the dispatcher.
 {
  hostNow=100;ReadyWebServer web(80);configure(web);uint32_t ticket=0;unsigned cameraCalls=0;
  web.on("/login",HTTP_POST,[&]{assert(web.rawBody()=="secret"&&web.arg("plain")=="secret");ticket=web.defer();assert(ticket);});
  web.on("/camera",HTTP_GET,[&]{++cameraCalls;web.send(200,"text/plain","camera-ok");});
  const size_t baseline=hostStringHeapCapacity(),nodes=hostRequestNodes;
  auto login=web.enqueue(request("POST","/login","secret"));web.handleClient();
  assert(web.waiting()==1&&!web.sending()&&hostRequestNodes==nodes&&web.queuedBytes()==0&&hostStringHeapCapacity()==baseline);
  for(unsigned i=0;i<60;++i){
   auto camera=web.enqueue(request("GET","/camera"));drain(web,camera);status(camera,200);hostNow+=100;
  }
  assert(cameraCalls==60&&login->alive);
  assert(web.completeDeferred(ticket,[&]{web.sendHeader("Set-Cookie","SVSID=fixture; HttpOnly");web.send(200,"application/json","{}");}));
  drain(web,login);status(login,200);assert(login->output.find("Set-Cookie: SVSID=fixture; HttpOnly")!=std::string::npos);
  assert(!web.completeDeferred(ticket,[&]{assert(false);}));
  auto cancelled=web.enqueue(request("POST","/login","secret"));web.handleClient();const auto stale=ticket;
  cancelled->remoteClosed=true;web.handleClient();assert(!cancelled->alive);
  auto replacement=web.enqueue(request("POST","/login","secret"));web.handleClient();assert(ticket!=stale);
  assert(!web.completeDeferred(stale,[&]{assert(false);}));
  hostNow+=30000;web.handleClient();drain(web,replacement);status(replacement,408);
  assert(!web.completeDeferred(ticket,[&]{assert(false);}));
  assert(!web.waiting()&&hostStringHeapCapacity()==baseline);
  auto healthy=web.enqueue(request("GET","/camera"));drain(web,healthy);status(healthy,200);
  assert(hostStringHeapCapacity()==baseline&&hostRequestNodes==nodes);
  std::cout<<"PASS deferred login preserves camera service, releases credentials/request buffers, isolates cookies, rejects old tickets, disconnects and absolute timeout, and restores capacity\n";
 }
 // Reproduce a header/body delayed-ACK boundary and fragmented short writes.
 {
  hostNow=100;ReadyWebServer web(80);configure(web);
  const std::string payload="{\"fresh\":true}";
  web.on("/live",HTTP_GET,[&]{String owned(payload.c_str());web.sendOwned(200,"application/json",std::move(owned));});
  auto peer=web.enqueue(request("GET","/live"));peer->delayedAck=true;
  drain(web,peer);status(peer,200);assert(body(peer)==payload&&peer->noDelay&&peer->sendCalls==1&&hostNow<110);
  for(size_t slice:{size_t(1),size_t(17),size_t(1460)}){
   auto shortWrite=web.enqueue(request("GET","/live"));shortWrite->sendLimit=slice;
   drain(web,shortWrite);status(shortWrite,200);assert(body(shortWrite)==payload&&web.queuedBytes()==0);
  }
  std::cout<<"PASS immediate header/body delivery, TCP_NODELAY and exact partial-write ownership\n";
 }
 {
  hostNow=100;ReadyWebServer web(80);configure(web);size_t hits=0;std::string seen;
  web.on("/echo",HTTP_POST,[&]{++hits;seen=web.arg("plain").c_str();assert(web.header("Cookie")=="SVSID=fixture");assert(web.header("X-CSRF-Token")=="csrf");web.send(200,"text/plain",seen.c_str());});
  web.on("/query",HTTP_GET,[&]{++hits;assert(web.arg("first")=="a b"&&web.arg("plus")=="+"&&web.arg("last")=="42");assert(!web.hasArg("plain"));web.send(200,"text/plain","query-ok");});
  const std::string payload=" {\r\n \"name\": \"tap + \\u03a9\", \"value\":3000 } \r\n";
  const auto wire=request("POST","/echo",payload,"Cookie: SVSID=fixture\r\nX-CSRF-Token: csrf\r\n");
  for(size_t split=0;split<=wire.size();++split){
   auto peer=web.enqueue(wire.substr(0,split));web.handleClient();assert(!web.sending()||split==wire.size());
   peer->input+=wire.substr(split);drain(web,peer);status(peer,200);assert(body(peer)==payload&&seen==payload);assert(!web.waiting()&&web.queuedBytes()==0&&hostOwners==0);
  }
  const std::string maximum(4096,'x');
  auto maxBody=web.enqueue(request("POST","/echo",maximum,"Cookie: SVSID=fixture\r\nX-CSRF-Token: csrf\r\n"));drain(web,maxBody);status(maxBody,200);assert(body(maxBody)==maximum&&seen==maximum);
  const size_t beforeOversize=hits;
  auto tooLarge=web.enqueue("POST /echo HTTP/1.1\r\nHost: vest\r\nContent-Length: 4097\r\n\r\n");drain(web,tooLarge);status(tooLarge,413);assert(hits==beforeOversize);
  auto q=web.enqueue(request("GET","/query?first=a%20b&plus=%2B&last=42"));drain(web,q);status(q,200);assert(body(q)=="query-ok");
  for(const std::string& query:{"plain=poison","same=1&same=2","x=%00","x=%zz","x=%01"}){auto peer=web.enqueue(request("POST","/echo?"+query,"{}"));drain(web,peer);status(peer,400);}
  std::string many;for(int i=0;i<17;++i)many+=(i?"&":"")+std::string("x")+std::to_string(i)+"=1";
  auto excessive=web.enqueue(request("GET","/query?"+many));drain(web,excessive);status(excessive,400);
  auto unknown=web.enqueue(request("GET","/missing"));drain(web,unknown);status(unknown,404);
  assert(hits==wire.size()+3);
 }
 // Logical queue length alone misses native String allocations retained after
 // empty assignment. Check capacity while the server and its slots stay alive.
 {
  ReadyWebServer web(80);configure(web);const size_t baseline=hostStringHeapCapacity();
  const std::string large(20000,'r'),cookie(900,'c');
  web.on("/large",HTTP_GET,[&]{assert(web.header("Cookie").length()==cookie.size());web.send(200,"application/json",large.c_str());});
  web.on("/small",HTTP_GET,[&]{assert(web.header("Cookie").length()==0);web.send(200,"text/plain","OK");});
  for(unsigned cycle=0;cycle<1000;++cycle){
   auto peer=web.enqueue(request("GET","/large","","Cookie: "+cookie+"\r\n"));
   drain(web,peer);status(peer,200);assert(body(peer)==large);
   assert(web.queuedBytes()==0&&hostStringHeapCapacity()==baseline);
  }
  std::vector<std::shared_ptr<HostSocket>> blocked;
  for(unsigned i=0;i<4;++i){
   auto peer=web.enqueue(request("GET","/large","","Cookie: "+cookie+"\r\n"));peer->writable=false;blocked.push_back(peer);
   for(unsigned tick=0;tick<3;++tick)web.handleClient();
  }
  assert(web.waiting()==4&&web.queuedBytes()>20000&&hostStringHeapCapacity()>baseline);
  hostNow+=2000;web.handleClient();
  assert(!web.waiting()&&!web.queuedBytes()&&hostStringHeapCapacity()==baseline);
  for(auto& peer:blocked)assert(!peer->alive);
  auto healthy=web.enqueue(request("GET","/small"));drain(web,healthy);status(healthy,200);
  assert(hostStringHeapCapacity()==baseline);
 }
 assert(hostRequestNodes==0&&hostOwners==0&&hostSockets.empty());
 {
  ReadyWebServer web(80);configure(web);unsigned callbacks=0;web.on("/",HTTP_ANY,[&]{++callbacks;web.send(200,"text/plain","OK");});
  const std::vector<std::pair<std::string,int>> cases={
   {"POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: 4097\r\n\r\n",413},
   {"POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: -1\r\n\r\n",400},
   {"POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: 2\r\ncontent-length: 2\r\n\r\n{}",400},
   {"POST / HTTP/1.1\r\nHost: vest\r\nTransfer-Encoding: chunked\r\n\r\n",501},
   {"POST / HTTP/1.1\r\nHost: vest\r\nExpect: 100-continue\r\nContent-Length: 2\r\n\r\n",417},
   {"POST / HTTP/1.1\r\nHost: vest\r\nContent-Type: multipart/form-data\r\nContent-Length: 0\r\n\r\n",415},
   {"POST / HTTP/1.1\r\nHost: vest\r\n\r\n",411},
   {"GET / HTTP/1.1\r\nHost: vest\r\nhost: vest\r\n\r\n",400},
   {"TRACE / HTTP/1.1\r\nHost: vest\r\n\r\n",405},
   {"GET / HTTP/2.0\r\nHost: vest\r\n\r\n",505},
   {"GET / HTTP/1.1\nHost: vest\n\n",400},
   {"GET / HTTP/1.1\r\nHost: vest\r\nX: "+std::string(1100,'x')+"\r\n\r\n",431}
  };
  for(const auto& item:cases){auto peer=web.enqueue(item.first);drain(web,peer);status(peer,item.second);assert(callbacks==0&&web.queuedBytes()==0);}
  auto pipeline=web.enqueue(request("POST","/","{}")+request("GET","/"));drain(web,pipeline);status(pipeline,200);assert(callbacks==1);
  auto nul=web.enqueue(request("POST","/",std::string("a\0b",3)));drain(web,nul);status(nul,400);assert(callbacks==1);
 }
 {
  ReadyWebServer web(80);configure(web);unsigned calls=0;web.on("/ok",HTTP_GET,[&]{++calls;web.send(200,"text/plain","OK");});
  hostNow=100;auto idle=web.enqueue("");auto slow=web.enqueue("GET /ok HTTP/1.1\r\nHost: ves");auto normal=web.enqueue(request("GET","/ok"));
  drain(web,normal);status(normal,200);assert(calls==1&&web.waiting()==2);
  hostNow=2099;slow->input+="t";web.handleClient();assert(idle->alive&&slow->alive);
  hostNow=2100;web.handleClient();drain(web,slow);assert(!idle->alive);status(slow,408);assert(web.headerTimeouts==2&&!web.waiting());
  hostNow=3000;auto slowBody=web.enqueue("POST /never HTTP/1.1\r\nHost: vest\r\nContent-Length: 100\r\n\r\n");web.handleClient();
  for(unsigned i=1;i<5;++i){hostNow=3000+i*999;slowBody->input+="x";web.handleClient();assert(slowBody->alive);}
  hostNow=8000;web.handleClient();drain(web,slowBody);status(slowBody,408);assert(web.bodyTimeouts==1&&!web.waiting());
  hostNow=0xfffffff0U;auto wrap=web.enqueue("GET /ok HTTP/1.1\r\nHost: ");web.handleClient();hostNow+=2000;web.handleClient();drain(web,wrap);status(wrap,408);
  auto broken=web.enqueue("GET /ok HTTP/1.1\r\nHost: ");web.handleClient();broken->remoteClosed=true;web.handleClient();assert(!broken->alive&&web.peerDisconnects==1&&!web.waiting());
  const unsigned beforeFullDisconnect=calls;
  auto fullDisconnect=web.enqueue(request("GET","/ok"));fullDisconnect->remoteClosed=true;web.handleClient();
  assert(calls==beforeFullDisconnect+1&&!fullDisconnect->alive&&!web.waiting()&&hostOwners==0);
 }
 {
  ReadyWebServer web(80);configure(web);unsigned calls=0;web.on("/ok",HTTP_GET,[&]{++calls;web.send(200,"text/plain","OK");});
  hostNow=100;std::vector<std::shared_ptr<HostSocket>> blocked;
  for(unsigned i=0;i<4;++i)blocked.push_back(web.enqueue("GET /ok HTTP/1.1\r\nHost: "));
  web.handleClient();assert(web.waiting()==4&&calls==0);
  auto queued=web.enqueue(request("GET","/ok"));hostNow=2099;web.handleClient();assert(queued->alive&&calls==0&&web.waiting()==4);
  hostNow=2100;web.handleClient();drain(web,queued);status(queued,200);assert(calls==1&&web.headerTimeouts==4&&!web.waiting());
  for(auto& peer:blocked){assert(!peer->alive);status(peer,408);}
  assert(hostOwners==0&&hostSockets.empty());
 }
 {
  ReadyWebServer web(80);configure(web);std::string large(18000,'z');unsigned cameraCalls=0;
  web.on("/asset",HTTP_GET,[&]{
   web.sendHeader("Cache-Control","public, max-age=31536000, immutable");
   web.sendHeader("X-Content-Type-Options","nosniff");web.sendHeader("X-Frame-Options","DENY");
   web.sendHeader("Referrer-Policy","no-referrer");web.sendHeader("Content-Security-Policy",WEB_CONTENT_SECURITY_POLICY);
   web.sendHeader("Content-Encoding","gzip");web.sendHeader("Vary","Accept-Encoding");
   web.sendFlash("application/javascript",reinterpret_cast<const uint8_t*>(large.data()),large.size());
  });
  web.on("/camera",HTTP_GET,[&]{++cameraCalls;web.send(200,"text/plain","camera-ok");});
  web.on("/head",HTTP_HEAD,[&]{web.sendFlash("text/plain",reinterpret_cast<const uint8_t*>(large.data()),large.size(),true);});
  hostNow=100;auto asset=web.enqueue(request("GET","/asset"));asset->sendLimit=100;web.handleClient();assert(web.sending()==1);
  auto camera=web.enqueue(request("GET","/camera"));drain(web,camera);assert(cameraCalls==1&&body(camera)=="camera-ok"&&asset->alive);
  drain(web,asset,250,100);assert(web.flashCompleted==1&&web.maxFlashMs>15000&&body(asset)==large);
  auto head=web.enqueue(request("HEAD","/head"));drain(web,head);assert(body(head).empty()&&head->output.find("Content-Length: 18000")!=std::string::npos);
  hostNow=0xfffffff0U;auto stalled=web.enqueue(request("GET","/asset"));stalled->writable=false;web.handleClient();hostNow+=3999;web.handleClient();assert(stalled->alive);hostNow++;web.handleClient();assert(!stalled->alive&&web.writeAborts==1&&!web.sending());
  hostNow=10;auto drip=web.enqueue(request("GET","/asset"));drip->sendLimit=1;web.handleClient();for(int i=0;i<31;++i){hostNow+=1000;web.handleClient();}assert(!drip->alive&&web.writeAborts==2);
 }
 {
  ReadyWebServer web(80);configure(web);unsigned sent=0,middleware=0;web.addMiddleware([&](WebServer&,std::function<bool()> next){++middleware;return next();});
  web.on("/reply",HTTP_GET,[&]{++sent;web.send(200,"text/plain",std::string(17000,'x').c_str());});
  auto first=web.enqueue(request("GET","/reply"));first->writable=false;web.handleClient();assert(web.queuedBytes()>17000&&web.queuedBytes()<=32768);
  auto second=web.enqueue(request("GET","/reply"));second->writable=false;web.handleClient();assert(sent==2&&middleware==2&&web.responseMemoryFailures==1&&web.queuedBytes()<=32768);
  second->writable=true;drain(web,second);status(second,503);
  hostNow+=2000;web.handleClient();assert(!first->alive&&web.queuedBytes()==0&&!web.waiting());
  String::failReserve=true;auto failure=web.enqueue(request("GET","/reply"));drain(web,failure);String::failReserve=false;status(failure,503);assert(sent==2);
  auto recovered=web.enqueue(request("GET","/reply"));drain(web,recovered);status(recovered,200);assert(body(recovered).size()==17000);
 }
 {
  ReadyWebServer web(80);configure(web);unsigned failAt=0;
  web.on("/header-allocation",HTTP_GET,[&]{String::concatCalls=0;String::failConcatAt=failAt;web.send(200,"application/json","{\"ok\":true}");String::failConcatAt=0;});
  for(unsigned at:{1U,5U,11U,15U}){failAt=at;auto peer=web.enqueue(request("GET","/header-allocation"));drain(web,peer);status(peer,503);assert(web.queuedBytes()==0&&!web.waiting());}
  failAt=0;auto healthy=web.enqueue(request("GET","/header-allocation"));drain(web,healthy);status(healthy,200);assert(body(healthy)=="{\"ok\":true}");
 }
 {
  ReadyWebServer web(80);configure(web);
  CameraAuth::Exchange exchange;std::strcpy(exchange.client,"11111111111111111111111111111111");std::strcpy(exchange.session,"22222222222222222222222222222222");exchange.sequence=1;
  const char* key="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  const std::string raw=" {\r\n \"stage\": \"protected\" } \r\n";char signature[65];
  assert(CameraAuth::message(key,"front",exchange,"POST","/camera/ack",raw.data(),raw.size(),0,signature));
  web.on("/camera/ack",HTTP_POST,[&]{const String body=web.arg("plain");char got[65];assert(CameraAuth::message(key,"front",exchange,"POST",web.uri().c_str(),body.c_str(),body.length(),0,got));assert(CameraCrypto::equal(got,web.header("X-SV-MAC").c_str(),64));web.send(200,"application/json","{\"ok\":true}");});
  auto peer=web.enqueue(request("POST","/camera/ack",raw,std::string("X-SV-MAC: ")+signature+"\r\n"));peer->readLimit=1;drain(web,peer);status(peer,200);
 }
 {
  ReadyWebServer web(80);configure(web);const size_t baseline=hostStringHeapCapacity();
  const std::string payload(16000,'z');
  web.on("/owned",HTTP_GET,[&]{String data(payload.c_str());String::largestReservation=0;web.sendOwned(200,"application/json",std::move(data));assert(String::largestReservation<=1024);});
  for(unsigned i=0;i<1000;++i){auto peer=web.enqueue(request("GET","/owned"));peer->sendLimit=333;drain(web,peer);status(peer,200);assert(body(peer)==payload);assert(web.queuedBytes()==0&&hostStringHeapCapacity()==baseline);}
  // Model fragmented free memory: no new contiguous reservation above 4 KiB.
  // The body already exists when delivery starts, as it does after sendJson.
  String::maxReservation=4096;
  auto fragmented=web.enqueue(request("GET","/owned"));drain(web,fragmented);status(fragmented,200);assert(body(fragmented)==payload);
  web.on("/copied",HTTP_GET,[&]{web.send(200,"application/json",payload.c_str());});
  auto copied=web.enqueue(request("GET","/copied"));drain(web,copied);status(copied,503);
  String::maxReservation=std::numeric_limits<size_t>::max();
  auto stalled=web.enqueue(request("GET","/owned"));stalled->writable=false;web.handleClient();assert(web.queuedBytes()>16000);
  hostNow+=2000;web.handleClient();assert(!stalled->alive&&web.queuedBytes()==0&&hostStringHeapCapacity()==baseline);
  auto a=web.enqueue(request("GET","/owned"));a->writable=false;web.handleClient();
  auto b=web.enqueue(request("GET","/owned"));b->writable=false;web.handleClient();
  auto c=web.enqueue(request("GET","/owned"));drain(web,c);status(c,503);
  a->writable=true;b->writable=true;drain(web,a);drain(web,b);assert(web.queuedBytes()==0&&hostStringHeapCapacity()==baseline);
 }
 // 100,000 completed mixed requests across 20 minutes of virtual uptime.
 // Idle/response/body attacks above use real byte queues and independent clocks;
 // virtual elapsed time does not claim physical Wi-Fi or CPU performance.
 {
  ReadyWebServer web(80);configure(web);unsigned calls=0;const size_t nodes=hostRequestNodes,capacity=hostStringHeapCapacity();
  web.on("/live",HTTP_GET,[&]{++calls;assert(web.header("Cookie")=="SVSID=rider");assert(!web.hasArg("plain"));web.send(200,"application/json","{\"online\":true}");});
  web.on("/front",HTTP_POST,[&]{++calls;assert(web.arg("plain")=="{\"camera\":\"front\"}");assert(web.header("Cookie").length()==0);web.send(200,"application/json","{\"ok\":true}");});
  web.on("/rear",HTTP_POST,[&]{++calls;assert(web.arg("plain")=="{\"camera\":\"rear\"}");assert(web.header("Cookie").length()==0);web.send(200,"application/json","{\"ok\":true}");});
  hostNow=100;for(unsigned cycle=0;cycle<25000;++cycle){
   std::vector<std::shared_ptr<HostSocket>> peers={web.enqueue(request("GET","/live","","Cookie: SVSID=rider\r\n")),web.enqueue(request("POST","/front","{\"camera\":\"front\"}")),web.enqueue(request("POST","/rear","{\"camera\":\"rear\"}")),web.enqueue(request("GET","/live","","Cookie: SVSID=rider\r\n"))};
   for(unsigned n=0;n<4;++n){web.handleClient();hostNow+=12;}
   for(auto& peer:peers){assert(!peer->alive);status(peer,200);}
   assert(!web.waiting()&&!web.sending()&&!web.queuedBytes()&&hostOwners==0&&hostRequestNodes==nodes&&hostSockets.empty());
   assert(hostStringHeapCapacity()==capacity);
  }
  assert(calls==100000&&web.admitted==100000&&hostNow==1200100&&web.writeAborts==0&&web.expired==0&&web.responseCompleted==100000);
 }
 assert(hostRequestNodes==0&&hostOwners==0&&hostSockets.empty());
 std::cout<<"PASS production ReadyWebServer + exact pinned SDK routing/header/argument/response helpers: fragmented requests, exact signed bytes, strict framing, absolute deadlines/wrap, independent clients, state reset, output caps/stalls, allocation recovery, 1000 large responses and 100000 mixed requests over 20 virtual minutes; no retained String capacity/sockets/argument nodes\n";
}
