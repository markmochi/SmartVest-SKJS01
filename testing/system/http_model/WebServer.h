#pragma once
#include "Arduino.h"
#include "SocketModel.h"
#include <functional>
#include <memory>
#include <cassert>
#include <vector>
constexpr size_t CONTENT_LENGTH_NOT_SET=std::numeric_limits<size_t>::max()-1,CONTENT_LENGTH_UNKNOWN=std::numeric_limits<size_t>::max();
#define WEBSERVER_MAX_QUERY_ARGS 32
enum HTTPMethod{HTTP_ANY,HTTP_GET,HTTP_HEAD,HTTP_POST,HTTP_PUT,HTTP_PATCH,HTTP_DELETE,HTTP_OPTIONS};
enum HTTPClientStatus{HC_NONE,HC_WAIT_READ,HC_WAIT_CLOSE};
struct HTTPUpload{};struct HTTPRaw{};
namespace mime {enum{html};struct Entry{const char* mimeType;};inline Entry mimeTable[]={{"text/html"}};}
inline const char AUTHORIZATION_HEADER[]="Authorization",ETAG_HEADER[]="If-None-Match",Content_Length[]="Content-Length";
inline const String emptyString;
inline size_t hostRequestNodes=0;
class WebServer;
class RequestHandler {
  RequestHandler* following=nullptr;
 public:
  virtual ~RequestHandler()=default;
  virtual bool canHandle(WebServer&,HTTPMethod,const String&)=0;
  virtual bool process(WebServer&,HTTPMethod,const String&)=0;
  RequestHandler* next(){return following;}
  void next(RequestHandler* p){following=p;}
};
struct MiddlewareChain {
  std::function<bool(WebServer&,std::function<bool()>)> callback;
  bool runChain(WebServer& server,std::function<bool()> next){return callback?callback(server,next):next();}
};
class WebServer {
 public:
  using THandlerFunction=std::function<void()>;
  explicit WebServer(int){}
  virtual ~WebServer(){_clearResponseHeaders();_clearRequestHeaders();delete[] _currentArgs;delete[] _postArgs;while(_firstHandler){auto* next=_firstHandler->next();delete _firstHandler;_firstHandler=next;}delete _chain;}
  virtual void handleClient(){assert(false&&"Inherited unbounded parser must never run");}
#if defined(SJKS_HTTP_REAL_SOCKET)
  void enqueue(int fd){_server.pending.emplace_back(fd);}
#else
  std::shared_ptr<HostSocket> enqueue(const std::string& input){auto state=std::make_shared<HostSocket>();state->input=input;_server.pending.emplace_back(state);return state;}
#endif
  void on(const char* path,HTTPMethod method,THandlerFunction callback);
  void onNotFound(THandlerFunction callback){_notFoundHandler=std::move(callback);}
  void addMiddleware(std::function<bool(WebServer&,std::function<bool()>)> callback){if(!_chain)_chain=new MiddlewareChain;_chain->callback=std::move(callback);}
  HTTPMethod method()const{return _currentMethod;}
  String uri()const{return _currentUri;}
  String hostHeader()const;
  String arg(const String&)const;String arg(int)const;String argName(int)const;int args()const;bool hasArg(const String&)const;
  String header(const String&)const;String header(int)const;String headerName(int)const;bool hasHeader(const String&)const;int headers()const;
  void collectHeaders(const char*[],size_t);void collectAllHeaders();
  void sendHeader(const String&,const String&,bool first=false);
  void send(int code,const char* type,const String& body){String h;_prepareHeader(h,code,type,body.length());_currentClientWrite(h.c_str(),h.length());_currentClientWrite(body.c_str(),body.length());}
  void send(int code,const String& type,const String& body){send(code,type.c_str(),body);}
  void send(int code,const char* type,const char* body){send(code,type,String(body));}
  void sendContent(const String& body){_currentClientWrite(body.c_str(),body.length());}
  const String version()const;static String responseCodeToString(int);
  static String urlDecode(const String&);
 protected:
  struct RequestArgument {
    String key,value;RequestArgument* next=nullptr;
    RequestArgument(){++hostRequestNodes;}
    ~RequestArgument(){--hostRequestNodes;}
  };
  virtual size_t _currentClientWrite(const char*,size_t){return 0;}
  virtual size_t _currentClientWrite_P(PGM_P,size_t){return 0;}
  bool _handleRequest();void _finalizeResponse();
  void _parseArguments(const String&);bool _collectHeader(const char*,const char*);
  void _prepareHeader(String&,int,const char*,size_t);
  void _clearResponseHeaders();void _clearRequestHeaders();
  NetworkServer _server;NetworkClient _currentClient;
  HTTPMethod _currentMethod=HTTP_ANY;String _currentUri;uint8_t _currentVersion=0;HTTPClientStatus _currentStatus=HC_NONE;unsigned long _statusChange=0;
  RequestHandler* _currentHandler=nullptr;RequestHandler* _firstHandler=nullptr;RequestHandler* _lastHandler=nullptr;
  THandlerFunction _notFoundHandler;
  int _currentArgCount=0,_postArgsLen=0,_headerKeysCount=0,_responseHeaderCount=0,_responseCode=0,_clientContentLength=0;
  RequestArgument* _currentArgs=nullptr;RequestArgument* _postArgs=nullptr;RequestArgument* _currentHeaders=nullptr;RequestArgument* _responseHeaders=nullptr;
  std::unique_ptr<HTTPUpload> _currentUpload;std::unique_ptr<HTTPRaw> _currentRaw;
  size_t _contentLength=CONTENT_LENGTH_NOT_SET;String _hostHeader;bool _chunked=false,_collectAllHeaders=false,_corsEnabled=false;MiddlewareChain* _chain=nullptr;
};
class HostHandler final:public RequestHandler {
  String path;HTTPMethod verb;WebServer::THandlerFunction callback;
 public:
  HostHandler(const char* p,HTTPMethod v,WebServer::THandlerFunction fn):path(p),verb(v),callback(std::move(fn)){}
  bool canHandle(WebServer&,HTTPMethod method,const String& uri)override{return (verb==HTTP_ANY||verb==method)&&uri==path;}
  bool process(WebServer& server,HTTPMethod method,const String& uri)override{if(!canHandle(server,method,uri))return false;callback();return true;}
};
inline void WebServer::on(const char* path,HTTPMethod method,THandlerFunction callback){auto* handler=new HostHandler(path,method,std::move(callback));if(_lastHandler)_lastHandler->next(handler);else _firstHandler=handler;_lastHandler=handler;}
#include "core_methods.inc"
