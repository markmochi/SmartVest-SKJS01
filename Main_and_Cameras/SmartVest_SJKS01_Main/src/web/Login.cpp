#include "DashboardServer.h"
#include <esp_timer.h>

void DashboardServer::beginLogin(){
  if(!isValidOrigin()){sendError(403,"Use the local dashboard address.");return;}
  auto body=makeJson();
  if(!readJsonBody(body))return;
  if(!body["username"].is<const char*>()||!body["password"].is<const char*>()||
      body["username"].as<JsonString>().size()>32||body["password"].as<JsonString>().size()>64){
    sendError(400,"Enter a username and password within the supported length.");return;
  }
  if(std::strlen(body["username"].as<const char*>())!=body["username"].as<JsonString>().size()||
      std::strlen(body["password"].as<const char*>())!=body["password"].as<JsonString>().size()){
    sendError(400,"Enter a username and password within the supported length.");return;
  }
  if(login.busy()){sendError(429,"A sign-in is already in progress. Please wait and try again.");return;}
  if(security.throttled()){sendError(429,"Too many attempts. Please wait one minute and try again.");return;}
  acquireState();
  const String username=body["username"]|"";
  if(!store.healthy&&username!=store.data.adminUsername){sendError(503,"Settings could not be loaded.");return;}
  // The worker owns a credential-only copy. It never touches device history,
  // the state mutex, sessions, camera authentication or the network server.
  if(!login.start(username,body["password"]|"",store.data)){
    sendError(503,"Sign-in is temporarily unavailable. Please try again.");return;
  }
  releaseState();
  if(body["utcMs"].is<uint64_t>()&&UtcClock::valid(body["utcMs"].as<uint64_t>()))login.utc=body["utcMs"];
  login.ticket=web.defer();
}

void DashboardServer::finishLogin(){
  if(!login.ready())return;
  // Only the dashboard task owns diagnostic timing fields. The worker
  // publishes its result through phase before any of these fields are read.
  login.lastMs=uint32_t(millis()-login.started);
  if(login.lastMs>login.maxMs)login.maxMs=login.lastMs;
  // A disconnected client must not evade the failed-password attempt limit.
  if(!login.valid)security.finishLogin(false,login.developer);
  web.completeDeferred(login.ticket,[this](){
    acquireState();
    if(!login.matches(store.data)){sendError(409,"Sign-in details changed. Please sign in again.");return;}
    if(!store.healthy&&!login.developer){sendError(503,"Settings could not be loaded.");return;}
    if(!login.valid){sendError(401,"Incorrect username or password.");return;}
    auto* session=security.finishLogin(true,login.developer);
    if(!session){sendError(429,"Too many attempts. Please wait one minute and try again.");return;}
    if(login.utc)namingClock.set(login.utc+uint32_t(millis()-login.started),esp_timer_get_time()/1000ULL,2);
    web.sendHeader("Set-Cookie","SVSID="+session->token+
      "; HttpOnly; SameSite=Strict; Path=/; Max-Age="+String(Config::SESSION_TIMEOUT_MS/1000));
    sendSession(session,store.data.defaultPassword);
  });
  login.reset();
}
