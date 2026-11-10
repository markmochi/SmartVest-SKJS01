#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/web/ReadyWebServer.h"
#include "../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/HttpPacing.h"
#include <cassert>
#include <iostream>
#include <string>
int connect(ReadyWebServer& web){int pair[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);web.enqueue(pair[0]);return pair[1];}
void sendAll(int fd,const std::string& data){assert(::send(fd,data.data(),data.size(),0)==ssize_t(data.size()));}
std::string receive(int fd){std::string all;char bytes[4096];for(;;){const int n=::recv(fd,bytes,sizeof(bytes),MSG_DONTWAIT);if(n<=0)break;all.append(bytes,size_t(n));}return all;}
std::string request(const std::string& path){return "GET "+path+" HTTP/1.1\r\nHost: vest\r\n\r\n";}
int main(){
 {
  ReadyWebServer web(80);const char* headers[]={"Content-Type","Cookie"};web.collectHeaders(headers,2);unsigned calls=0;
  web.on("/camera",HTTP_GET,[&]{++calls;web.send(200,"text/plain","camera-ok");});
  web.on("/admin",HTTP_GET,[&]{++calls;web.send(200,"text/plain","dashboard-ok");});
  const int idle=connect(web),slow=connect(web),camera=connect(web),browser=connect(web);
  sendAll(slow,"POST /unfinished HTTP/1.1\r\nHost: vest\r\nContent-Length: 2\r\n\r\n{");
  sendAll(camera,request("/camera"));sendAll(browser,request("/admin"));
  for(int i=0;i<3;++i)web.handleClient();assert(calls==2&&web.waiting()==2);
  assert(receive(camera).find("camera-ok")!=std::string::npos);assert(receive(browser).find("dashboard-ok")!=std::string::npos);
  hostNow=2000;web.handleClient();assert(web.headerTimeouts==1&&web.waiting()==1);
  hostNow=5000;web.handleClient();assert(web.bodyTimeouts==1&&web.waiting()==0);assert(receive(slow).find("408 Request Timeout")!=std::string::npos);
  for(int fd:{idle,slow,camera,browser})::close(fd);
  for(unsigned run=0;run<500;++run){
   int a=connect(web),b=connect(web),c=connect(web);sendAll(a,request("/camera"));sendAll(b,request("/camera"));sendAll(c,request("/admin"));
   for(unsigned n=0;n<3;++n)web.handleClient();
   for(int fd:{a,b,c}){assert(receive(fd).find("200 OK")!=std::string::npos);::close(fd);}hostNow+=10;
  }
  assert(calls==1502&&web.admitted==1502&&!web.waiting());
 }
 for(const char* role:{"rider","developer"}){
  ReadyWebServer web(80);const char* headers[]={"Content-Type"};web.collectHeaders(headers,1);
  const std::string body=std::string("{\"username\":\"")+role+"\",\"password\":\"test-only\"}";
  unsigned calls=0;web.on("/api/v1/login",HTTP_POST,[&]{++calls;assert(web.arg("plain")==body.c_str());web.send(200,"application/json","{\"ok\":true}");});
  int fd=connect(web);const std::string wire="POST /api/v1/login HTTP/1.1\r\nHost: vest\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
  for(char c:wire){sendAll(fd,std::string(1,c));web.handleClient();++hostNow;}
  for(int i=0;i<4;++i)web.handleClient();assert(calls==1&&receive(fd).find("200 OK")!=std::string::npos);::close(fd);
 }
 {
  ReadyWebServer web(80);web.collectHeaders(nullptr,0);const std::string large(20000,'x');unsigned calls=0;
  web.on("/large",HTTP_GET,[&]{++calls;web.send(200,"text/plain",large.c_str());});web.on("/small",HTTP_GET,[&]{++calls;web.send(200,"text/plain","OK");});
  int pair[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);int small=1024;setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof(small));web.enqueue(pair[0]);sendAll(pair[1],request("/large"));
  const uint32_t start=hostNow;web.handleClient();assert(hostNow==start&&calls==1&&web.sending()==1);
  int healthy=connect(web);sendAll(healthy,request("/small"));web.handleClient();assert(calls==2&&receive(healthy).find("200 OK")!=std::string::npos);::close(healthy);
  hostNow+=2000;web.handleClient();assert(web.writeAborts==1&&!web.sending());::close(pair[1]);
 }
 using CameraHttp::Pacing;Pacing pacing;assert(pacing.ready(0));pacing.result(0,-1,0);assert(!pacing.ready(999)&&pacing.ready(1000));pacing.result(1000,-1,0);assert(!pacing.ready(2999)&&pacing.ready(3000));
 std::cout<<"PASS actual POSIX sockets with production bounded parser/transport and pinned SDK compatibility helpers: fragmented login for both roles, idle/slowbody deadlines, 1500 mixed requests, independent healthy request during stalled response, camera pacing\n";
}
