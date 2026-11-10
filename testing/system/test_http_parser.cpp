#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/web/HttpRequest.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
namespace {
std::unordered_map<void*,size_t> live;
size_t peak=0,current=0,largest=0,allocations=0;
bool allocationFailure=false;
void* allocate(size_t n){assert(n<=HttpRequest::MAX_BODY+1);if(allocationFailure)return nullptr;void* p=std::malloc(n);assert(p);live[p]=n;current+=n;peak=std::max(peak,current);largest=std::max(largest,n);++allocations;return p;}
void release(void* p){assert(live.count(p));current-=live.at(p);live.erase(p);std::free(p);}
void feed(HttpRequest::Parser& p,const std::string& request){for(unsigned char c:request)p.feed(c);}
std::string post(const std::string& body,const std::string& headers=""){return "POST /api/v1/test HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: "+std::to_string(body.size())+"\r\nContent-Type: application/json; charset=UTF-8\r\n"+headers+"\r\n"+body;}
void fails(const std::string& request,unsigned status){HttpRequest::Parser p(allocate,release);feed(p,request);if(p.error()!=status||p.complete())std::cerr<<"Expected "<<status<<", got "<<p.error()<<" for "<<request.substr(0,160)<<"\n";assert(p.error()==status&&!p.complete());p.reset();assert(live.empty());}
}
int main(){
 const std::string body=" {\r\n \"message\": \"tap + \\u03a9\", \"value\":3000 } \r\n";
 const std::string request=post(body,"Cookie: SVSID=fixture\r\nX-SV-MAC: AbCd\r\n");
 for(size_t split=0;split<=request.size();++split){
  HttpRequest::Parser p(allocate,release);feed(p,request.substr(0,split));
  if(split<request.size())assert(!p.complete());feed(p,request.substr(split));
  assert(p.complete()&&!p.error()&&p.method()==HttpRequest::Method::Post&&p.version()==1);
  assert(p.bodyLength()==body.size()&&std::string(p.body(),p.bodyLength())==body);
  assert(std::string(p.path())=="/api/v1/test"&&std::string(p.host())=="192.168.4.1");
  p.reset();assert(live.empty()&&!p.complete()&&!p.error());
 }
 {HttpRequest::Parser p(allocate,release);feed(p,"GET /status?x=a%20b&before=42 HTTP/1.0\r\n\r\n");assert(p.complete()&&p.version()==0&&!p.bodyLength()&&std::string(p.query())=="x=a%20b&before=42");}
 {HttpRequest::Parser p(allocate,release);feed(p,post(std::string(4096,'x')));assert(p.complete()&&p.bodyLength()==4096&&largest==4097);p.reset();assert(live.empty());}
 const size_t allocationsBeforeOversize=allocations;
 fails(post(std::string(4097,'x')),413);assert(allocations==allocationsBeforeOversize);
 for(const char* length:{"-1","+1","1x","","1,1","1 1"})fails(std::string("POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: ")+length+"\r\n\r\nx",400);
 fails("POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: 99999999999999999999999999999999999999999999\r\n\r\n",413);
 fails("POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: 1\r\ncontent-length: 1\r\n\r\nx",400);
 fails("POST / HTTP/1.1\r\nHost: vest\r\nTransfer-Encoding: chunked\r\n\r\n",501);
 fails("POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: 1\r\nTransfer-Encoding: identity\r\n\r\nx",501);
 fails("POST / HTTP/1.1\r\nHost: vest\r\nExpect: 100-continue\r\nContent-Length: 1\r\n\r\nx",417);
 fails("POST / HTTP/1.1\r\nHost: vest\r\nContent-Type: multipart/form-data; boundary=x\r\nContent-Length: 0\r\n\r\n",415);
 fails("POST / HTTP/1.1\r\nHost: vest\r\n\r\n",411);
 fails("GET / HTTP/1.1\r\n\r\n",400);
 fails("GET / HTTP/1.1\r\nHost: vest\r\nhost: vest\r\n\r\n",400);
 fails("GET / HTTP/1.1\r\nHost: vest:999999\r\n\r\n",400);
 fails("GET / HTTP/1.1\r\nHost: vest\r\n x:y\r\n\r\n",400);
 fails("GET / HTTP/1.1\r\nHost : vest\r\n\r\n",400);
 fails("GET / HTTP/1.1\nHost: vest\n\n",400);
 fails("GET / HTTP/2.0\r\nHost: vest\r\n\r\n",505);
 fails("TRACE / HTTP/1.1\r\nHost: vest\r\n\r\n",405);
 fails("GET http://vest/ HTTP/1.1\r\nHost: vest\r\n\r\n",400);
 fails("GET /#fragment HTTP/1.1\r\nHost: vest\r\n\r\n",400);
 fails("GET /"+std::string(512,'x')+" HTTP/1.1\r\nHost: vest\r\n\r\n",414);
 fails("GET / HTTP/1.1\r\nHost: vest\r\nX: "+std::string(1025,'x')+"\r\n\r\n",431);
 std::string many="GET / HTTP/1.1\r\nHost: vest\r\n";for(int i=0;i<32;++i)many+="X-"+std::to_string(i)+": x\r\n";fails(many+"\r\n",431);
 fails("GET / HTTP/1.1\r\nHost: vest\r\nX1: "+std::string(1010,'x')+"\r\nX2: "+std::string(1010,'y')+"\r\n\r\n",431);
 fails(post(std::string("a\0b",3)),400);
 fails("GET / HTTP/1.1\r\nHost: vest\r\nContent-Length: 2\r\n\r\n{}",400);
 {HttpRequest::Parser p(allocate,release);allocationFailure=true;feed(p,post("{}"));assert(p.error()==503&&!p.complete()&&live.empty());allocationFailure=false;p.reset();feed(p,post("{}"));assert(p.complete());}
 assert(live.empty());
 {HttpRequest::Parser p(allocate,release);std::string wire=post("{}");feed(p,wire+"GET /second HTTP/1.1\r\nHost: vest\r\n\r\n");assert(p.complete()&&std::string(p.body())=="{}"&&std::string(p.path())=="/api/v1/test");}
 // Deterministic adversarial bytes and valid-prefix mutations cannot allocate
 // outside the body ceiling or retain memory after an error/reset.
 std::mt19937 random(0x51a30);for(unsigned run=0;run<20000;++run){
  HttpRequest::Parser p(allocate,release);
  std::string fuzz=run%3==0?"GET / HTTP/1.1\r\nHost: vest\r\n":run%3==1?"POST / HTTP/1.1\r\nHost: vest\r\nContent-Length: 256\r\n\r\n":"";
  for(unsigned n=random()%600;n;--n)fuzz+=char(random()%256);
  feed(p,fuzz);if(p.complete()){assert(!p.error()&&p.bodyLength()<=4096&&p.headerCount()<=32&&std::strlen(p.path())<=512);}
  p.reset();assert(live.empty()&&current==0);
 }
 assert(live.empty()&&current==0&&peak<=4097);
 std::cout<<"PASS actual HttpRequest parser: every request split, exact bytes, 4096/4097 boundaries, strict framing/header/target limits, unsupported encodings, allocation failure/recovery, reset, pipeline isolation and 20000 deterministic mutations; peak body allocation="<<peak<<" bytes\n";
}
