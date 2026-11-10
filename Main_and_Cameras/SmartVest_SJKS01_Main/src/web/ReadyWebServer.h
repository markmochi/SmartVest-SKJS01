#pragma once
#include <WebServer.h>
#include <lwip/sockets.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>
#include "HttpRequest.h"
#include "HttpMemoryBudget.h"

// One request per connection. The SDK supplies routing and responses, but
// never reads request bytes. Socket work and retained buffers are bounded.
class ReadyWebServer : public WebServer {
 public:
  explicit ReadyWebServer(int port):WebServer(port){}
  bool listening(){return bool(_server);}
  uint32_t admitted=0,rejected=0,expired=0,maxHandleMs=0;
  uint32_t writeAborts=0,flashCompleted=0,maxFlashMs=0,responseCompleted=0;
  uint32_t malformedRequests=0,oversizedRequests=0,requestMemoryFailures=0;
  uint32_t responseMemoryFailures=0,headerTimeouts=0,bodyTimeouts=0,peerDisconnects=0;
  size_t maxQueuedBytes=0;
  uint8_t waiting()const{uint8_t n=0;for(const auto& s:slots)if(s.used)++n;return n;}
  uint8_t sending()const{uint8_t n=0;for(const auto& s:slots)if(s.used&&s.transmitting)++n;return n;}
  size_t queuedBytes()const{size_t n=0;for(const auto& s:slots)n+=s.output.length()+s.body.length();return n;}
  // Borrow the already validated body. WebServer::arg() returns a full copy.
  const String& rawBody()const{
    return _currentArgCount&&_currentArgs[_currentArgCount-1].key=="plain" ? _currentArgs[_currentArgCount-1].value : emptyString;
  }
  uint32_t defer(){
    if(!activeSlot)return 0;
    auto& s=*activeSlot;
    if(++nextTicket==0)++nextTicket;
    s.ticket=nextTicket;s.version=_currentVersion;s.client=_currentClient;
    s.since=millis();s.used=s.deferred=true;return s.ticket;
  }
  template<class Reply> bool completeDeferred(uint32_t ticket,Reply reply){
    for(auto& s:slots)if(s.used&&s.deferred&&s.ticket==ticket){
      if(!s.client.connected()){closeSlot(s);return false;}
      clearRequest();activeSlot=&s;_currentClient=s.client;s.client=NetworkClient();
      _currentVersion=s.version;_currentMethod=HTTP_POST;s.used=s.deferred=false;s.ticket=0;
      reply();finishDispatch(s);return true;
    }
    return false;
  }
  // Keep the serialized body alive until the cooperative writer completes.
  // Moving it avoids a second body-sized allocation while Wi-Fi needs heap.
  void sendOwned(int code,const char* type,String&& body){
    if(!activeSlot)return;
    auto& s=*activeSlot;if(s.failed)return;
    String header;
    if(!HttpMemoryBudget::available(1024)||!header.reserve(1024)){responseFailure(s);return;}
    _prepareHeader(header,code,type,body.length());
    const size_t total=header.length()+body.length();
    if(!validHeader(header.c_str(),header.length())||total>MAX_RESPONSE||total>MAX_QUEUED-queuedBytes()){responseFailure(s);return;}
    s.output=std::move(header);s.body=std::move(body);
    s.bytes=reinterpret_cast<const uint8_t*>(s.body.c_str());s.length=s.body.length();
    startOutput(s);
  }
  void sendFlash(const char* type,const uint8_t* bytes,size_t length,bool head=false){
    if(!activeSlot)return;
    auto& s=*activeSlot;if(s.failed)return;
    String header;
    if(!HttpMemoryBudget::available(1024)||!header.reserve(1024)){responseFailure(s);return;}
    _prepareHeader(header,200,type,length);
    if(!validHeader(header.c_str(),header.length())||header.length()>MAX_RESPONSE||header.length()>MAX_QUEUED-queuedBytes()){responseFailure(s);return;}
    s.output=std::move(header);s.bytes=bytes;s.length=head?0:length;
    s.flashTransfer=true;startOutput(s);
  }
  void handleClient()override{
    const uint32_t now=millis();
    for(auto& s:slots)if(s.used){
      if(s.transmitting){pump(s,now);continue;}
      if(s.deferred){
        if(!s.client.connected())closeSlot(s);
        else if(uint32_t(now-s.since)>=30000){++expired;failRequest(s,408);}
        continue;
      }
      const uint32_t age=now-s.since;
      if(age>=REQUEST_MS||(!s.headerDone&&age>=HEADER_MS)){
        ++expired;if(s.headerDone)++bodyTimeouts;else ++headerTimeouts;
        if(!s.received)closeSlot(s);else failRequest(s,408);
      }
    }
    for(auto& s:slots)if(!s.used){
      NetworkClient incoming=_server.accept();if(!incoming)break;
      // Replies are small and latency-sensitive. The core defaults to Nagle;
      // do not wait for an ACK of the headers before delivering live data.
      incoming.setNoDelay(true);
      s.client=incoming;s.since=now;s.used=true;
    }
    // Give every reader a bounded slice before dispatching one handler.
    for(size_t offset=0;offset<SLOTS;++offset){
      auto& s=slots[(cursor+offset)%SLOTS];if(!s.used||s.transmitting||s.deferred||s.request.complete())continue;
      char bytes[READ_SLICE];const int n=::recv(s.client.fd(),bytes,sizeof(bytes),MSG_DONTWAIT);
      if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;
      if(n<=0){++peerDisconnects;closeSlot(s);continue;}
      s.received+=size_t(n);
      for(int i=0;i<n&&!s.request.complete()&&!s.request.error();++i)s.request.feed(uint8_t(bytes[i]));
      s.headerDone=s.request.headersComplete();
      if(const int error=s.request.error()){
        if(error==413||error==414||error==431)++oversizedRequests;
        else if(error==503)++requestMemoryFailures;else ++malformedRequests;
        failRequest(s,error);
      }
    }
    for(size_t offset=0;offset<SLOTS;++offset){
      const size_t index=(cursor+offset)%SLOTS;auto& s=slots[index];
      if(!s.used||s.transmitting||s.deferred||!s.request.complete())continue;
      cursor=(index+1)%SLOTS;dispatch(s);return;
    }
  }
 protected:
  size_t _currentClientWrite(const char* bytes,size_t length)override{return queueWrite(bytes,length);}
  size_t _currentClientWrite_P(PGM_P bytes,size_t length)override{return queueWrite(bytes,length);}
 private:
  static constexpr size_t SLOTS=4,READ_SLICE=512,MAX_RESPONSE=24*1024,MAX_QUEUED=32*1024;
  static constexpr uint32_t HEADER_MS=2000,REQUEST_MS=5000,WRITE_IDLE_MS=2000,RESPONSE_MS=5000,FLASH_TOTAL_MS=30000,FLASH_IDLE_MS=4000;
  struct Slot{
    NetworkClient client;HttpRequest::Parser request;String output,body;
    const uint8_t* bytes=nullptr;size_t length=0,offset=0,headerOffset=0,received=0;
    uint32_t since=0,progress=0;bool used=false,transmitting=false,flashTransfer=false,failed=false,headerDone=false;
    uint32_t ticket=0;uint8_t version=0;bool deferred=false;
  };
  Slot slots[SLOTS];Slot* activeSlot=nullptr;size_t cursor=0;uint32_t nextTicket=0;
  // Empty String assignment preserves capacity in ESP32 WString::move().
  // A null assignment invalidates it and returns the allocation to the heap.
  static void releaseText(String& value){value=static_cast<const char*>(nullptr);}
  static bool copy(String& out,const char* bytes,size_t size){releaseText(out);return HttpMemoryBudget::available(size)&&out.reserve(size)&&(!size||out.concat(bytes,size))&&out.length()==size;}
  static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
  static int decode(String& out,const char* bytes,size_t length){
    char decoded[HttpRequest::MAX_TARGET+1];size_t size=0;
    for(size_t i=0;i<length;++i){
      uint8_t c=uint8_t(bytes[i]);
      if(c=='%'){if(i+2>=length||hex(bytes[i+1])<0||hex(bytes[i+2])<0)return 400;c=uint8_t(hex(bytes[i+1])*16+hex(bytes[i+2]));i+=2;}
      else if(c=='+')c=' ';
      if(c<32||c==127)return 400;
      decoded[size++]=char(c);
    }
    return copy(out,decoded,size)?0:503;
  }
  void clearRequest(){
    delete[] _currentArgs;_currentArgs=nullptr;_currentArgCount=0;
    delete[] _postArgs;_postArgs=nullptr;_postArgsLen=0;
    _currentUpload.reset();_currentRaw.reset();releaseText(_hostHeader);releaseText(_currentUri);_currentHandler=nullptr;
    _currentMethod=HTTP_ANY;_currentVersion=0;_clientContentLength=0;_chunked=false;_contentLength=CONTENT_LENGTH_NOT_SET;_responseCode=0;
    for(auto* h=_currentHeaders;h;h=h->next)releaseText(h->value);
    _clearResponseHeaders();
  }
  int prepare(const HttpRequest::Parser& request){
    clearRequest();
    switch(request.method()){
      case HttpRequest::Method::Get:_currentMethod=HTTP_GET;break;
      case HttpRequest::Method::Head:_currentMethod=HTTP_HEAD;break;
      case HttpRequest::Method::Post:_currentMethod=HTTP_POST;break;
      case HttpRequest::Method::Put:_currentMethod=HTTP_PUT;break;
      case HttpRequest::Method::Patch:_currentMethod=HTTP_PATCH;break;
      case HttpRequest::Method::Delete:_currentMethod=HTTP_DELETE;break;
      case HttpRequest::Method::Options:_currentMethod=HTTP_OPTIONS;break;
    }
    _currentVersion=request.version();_clientContentLength=int(request.bodyLength());
    if(!copy(_currentUri,request.path(),strlen(request.path()))||!copy(_hostHeader,request.host(),strlen(request.host())))return 503;
    for(size_t i=0;i<request.headerCount();++i)for(auto* h=_currentHeaders;h;h=h->next)if(h->key.equalsIgnoreCase(request.headerName(i))){
      if(!copy(h->value,request.headerValue(i),strlen(request.headerValue(i))))return 503;
    }
    const char* query=request.query();size_t count=0;
    for(const char* p=query;*p;){
      const char* end=strchr(p,'&');if(!end)end=p+strlen(p);
      if(memchr(p,'=',size_t(end-p))&&++count>HttpRequest::MAX_ARGS)return 400;
      p=*end?end+1:end;
    }
    const size_t total=count+(request.bodyLength()?1:0);
    if(total){_currentArgs=new(std::nothrow) RequestArgument[total]{};if(!_currentArgs)return 503;}
    for(const char* p=query;*p;){
      const char* end=strchr(p,'&');if(!end)end=p+strlen(p);
      const char* equals=static_cast<const char*>(memchr(p,'=',size_t(end-p)));
      if(equals){
        auto& arg=_currentArgs[_currentArgCount];int error=decode(arg.key,p,size_t(equals-p));if(error)return error;
        error=decode(arg.value,equals+1,size_t(end-equals-1));if(error)return error;
        if(!arg.key.length()||arg.key=="plain")return 400;
        for(int j=0;j<_currentArgCount;++j)if(arg.key==_currentArgs[j].key)return 400;
        ++_currentArgCount;
      }
      p=*end?end+1:end;
    }
    if(request.bodyLength()){
      auto& arg=_currentArgs[_currentArgCount++];
      if(!copy(arg.key,"plain",5)||!copy(arg.value,request.body(),request.bodyLength()))return 503;
    }
    for(auto* h=_firstHandler;h;h=h->next())if(h->canHandle(*this,_currentMethod,_currentUri)){_currentHandler=h;break;}
    return 0;
  }
  void dispatch(Slot& s){
    activeSlot=&s;_currentClient=s.client;s.client=NetworkClient();s.used=false;
    const uint32_t start=millis();const int error=prepare(s.request);
    s.request.reset(); // Body/header copies now belong to WebServer.
    if(error){++rejected;if(error==503)++requestMemoryFailures;else ++malformedRequests;errorOutput(s,error);}
    else{
      _currentStatus=HC_WAIT_READ;_statusChange=millis();
      if(_chain)_chain->runChain(*this,[this](){return _handleRequest();});else _handleRequest();
      ++admitted;
    }
    maxHandleMs=std::max(maxHandleMs,uint32_t(millis()-start));finishDispatch(s);
  }
  void finishDispatch(Slot& s){
    clearRequest();
    if(!s.used||(!s.transmitting&&!s.deferred))_currentClient.stop();
    _currentClient=NetworkClient();_currentStatus=HC_NONE;activeSlot=nullptr;
    if(s.used&&s.transmitting)pump(s,millis());
  }
  void closeSlot(Slot& s){
    s.client.stop();s.client=NetworkClient();s.request.reset();releaseText(s.output);releaseText(s.body);s.bytes=nullptr;
    s.length=s.offset=s.headerOffset=s.received=0;s.used=s.transmitting=s.flashTransfer=s.failed=s.headerDone=false;
    s.ticket=0;s.deferred=false;
  }
  void startOutput(Slot& s){s.client=_currentClient;s.since=s.progress=millis();s.transmitting=s.used=true;maxQueuedBytes=std::max(maxQueuedBytes,queuedBytes());}
  static const char* errorBytes(int code){
    switch(code){
      case 405:return "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 408:return "HTTP/1.1 408 Request Timeout\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 411:return "HTTP/1.1 411 Length Required\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 413:return "HTTP/1.1 413 Content Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 414:return "HTTP/1.1 414 URI Too Long\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 415:return "HTTP/1.1 415 Unsupported Media Type\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 417:return "HTTP/1.1 417 Expectation Failed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 431:return "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 501:return "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 505:return "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      case 503:return "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: 39\r\nConnection: close\r\n\r\n{\"error\":\"Response memory unavailable\"}";
      default:return "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }
  }
  void errorOutput(Slot& s,int code){
    s.ticket=0;s.deferred=false;
    releaseText(s.output);releaseText(s.body);s.bytes=reinterpret_cast<const uint8_t*>(errorBytes(code));s.length=strlen(errorBytes(code));
    s.offset=s.headerOffset=0;s.flashTransfer=false;s.failed=true;s.request.reset();
    if(activeSlot==&s)startOutput(s);else{s.since=s.progress=millis();s.transmitting=s.used=true;}
  }
  void failRequest(Slot& s,int code){++rejected;errorOutput(s,code);pump(s,millis());}
  void responseFailure(Slot& s){++responseMemoryFailures;errorOutput(s,503);}
  bool validHeader(const char* bytes,size_t length)const{
    // SDK header construction ignores concat failures. Match its retained
    // header nodes before accepting the first write, without another allocation.
    if(_responseCode<100||_responseCode>599)return false;
    char prefix[24];const int count=snprintf(prefix,sizeof(prefix),"HTTP/1.%u %d ",unsigned(_currentVersion),_responseCode);
    if(count<=0||length<size_t(count)+4||memcmp(bytes,prefix,size_t(count)))return false;
    size_t at=size_t(count),reason=at;
    while(at<length&&bytes[at]!='\r'){if(uint8_t(bytes[at])<32||uint8_t(bytes[at])>126)return false;++at;}
    if(at==reason||at+2>length||bytes[at+1]!='\n')return false;
    at+=2;
    auto match=[&](const char* value,size_t size){if(size>length-at||memcmp(bytes+at,value,size))return false;at+=size;return true;};
    bool contentLength=false,connection=false,type=false;
    for(auto* h=_responseHeaders;h;h=h->next){
      if(!h->key.length()||!h->value.length())return false;
      if(h->key.equalsIgnoreCase("Content-Length")){
        if(contentLength)return false;contentLength=true;
        for(size_t i=0;i<h->value.length();++i)if(h->value[i]<'0'||h->value[i]>'9')return false;
      }
      if(h->key.equalsIgnoreCase("Connection")){if(connection||h->value!="close")return false;connection=true;}
      if(h->key.equalsIgnoreCase("Content-Type"))type=true;
      if(!match(h->key.c_str(),h->key.length())||!match(": ",2)||!match(h->value.c_str(),h->value.length())||!match("\r\n",2))return false;
    }
    return contentLength&&connection&&type&&match("\r\n",2)&&at==length;
  }
  size_t queueWrite(const char* bytes,size_t length){
    if(!length||!activeSlot||activeSlot->failed)return 0;
    auto& s=*activeSlot;
    if(!s.transmitting&&!validHeader(bytes,length)){responseFailure(s);return 0;}
    if(length>MAX_RESPONSE-s.output.length()||length>MAX_QUEUED-queuedBytes()||!HttpMemoryBudget::available(s.output.length()+length)||!s.output.reserve(s.output.length()+length)||!s.output.concat(bytes,length)){responseFailure(s);return 0;}
    if(!s.transmitting)startOutput(s);
    maxQueuedBytes=std::max(maxQueuedBytes,queuedBytes());return length;
  }
  void pump(Slot& s,uint32_t now){
    const uint32_t total=s.flashTransfer?FLASH_TOTAL_MS:RESPONSE_MS;
    if(uint32_t(now-s.progress)>=(s.flashTransfer?FLASH_IDLE_MS:WRITE_IDLE_MS)||uint32_t(now-s.since)>=total){closeSlot(s);++writeAborts;return;}
    const size_t headerLeft=s.output.length()-s.headerOffset,bodyLeft=s.length-s.offset;
    if(headerLeft||bodyLeft){
      // One bounded packet joins the header/body boundary without a heap copy
      // of the complete reply. Short writes advance each owner independently.
      char packet[1460];const size_t headerBytes=std::min(sizeof(packet),headerLeft);
      const size_t bodyBytes=std::min(sizeof(packet)-headerBytes,bodyLeft);
      const char* data=bodyBytes?reinterpret_cast<const char*>(s.bytes)+s.offset:nullptr;
      if(headerBytes){memcpy(packet,s.output.c_str()+s.headerOffset,headerBytes);if(bodyBytes)memcpy(packet+headerBytes,data,bodyBytes);data=packet;}
      const int n=::send(s.client.fd(),data,headerBytes+bodyBytes,MSG_DONTWAIT);
      if(n>0){const size_t sentHeader=std::min(size_t(n),headerBytes);s.headerOffset+=sentHeader;s.offset+=size_t(n)-sentHeader;s.progress=now;}
      else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)){closeSlot(s);++writeAborts;return;}
    }
    if(s.headerOffset==s.output.length()&&s.offset==s.length){if(s.flashTransfer){maxFlashMs=std::max(maxFlashMs,uint32_t(now-s.since));++flashCompleted;}++responseCompleted;closeSlot(s);}
  }
};
