#include "DashboardServer.h"


#include "../../Config.h"
#include "../core/Domain.h"
#include "../core/Version.h"
#include "../security/CameraPairingKeys.h"
#include "../network/HotspotConnectivity.h"
#include "../diagnostics/Console.h"
#include "generated/WebPolicy.h"
#include "JsonRequestMemory.h"
#include <esp_system.h>
#include <esp_timer.h>
#include <Network.h>

namespace {
bool cameraFrameFreshAtReport(const JsonDocument& telemetry) {
  const JsonObjectConst t = telemetry.as<JsonObjectConst>();
  return t["schema"] == 1 && t["currentFrames"].is<uint64_t>() &&
         t["currentFrames"].as<uint64_t>() > 0 &&
         t["frameAgeMs"].is<uint64_t>() &&
         t["frameAgeMs"].as<uint64_t>() <= Config::CAMERA_FRAME_FRESH_AT_REPORT_MS;
}
}

JsonDocument DashboardServer::makeJson(){return JsonDocument();}

void DashboardServer::bindRoute(const char* path,HTTPMethod method,
    void(DashboardServer::*handler)(),Access::Role role,bool gated){
  using Handler=void(DashboardServer::*)();
  static constexpr Handler handlers[]={&DashboardServer::beginLogin,&DashboardServer::cameraHeartbeat,&DashboardServer::cameraSession,&DashboardServer::cancelMessages,&DashboardServer::changePassword,&DashboardServer::commandImuLab,&DashboardServer::commandPiezo,&DashboardServer::commandRiderCalibration,&DashboardServer::handleJobAction,&DashboardServer::queueContactCheck,&DashboardServer::queueIntroduction,&DashboardServer::receiveCameraFiles,&DashboardServer::resetData,&DashboardServer::saveSettings,&DashboardServer::sendCameraMonitor,&DashboardServer::sendImuLab,&DashboardServer::sendIntegrations,&DashboardServer::sendJobs,&DashboardServer::sendModemTrace,&DashboardServer::sendRiderPose,&DashboardServer::sendRiderStatus,&DashboardServer::sendSettings,&DashboardServer::sendStatus};
  constexpr size_t count=sizeof(handlers)/sizeof(handlers[0]);
  static_assert(count<256,"Too many route handlers");
  uint8_t index=0;while(index<count&&handlers[index]!=handler)++index;
  if(index==count)return;
  const bool write=method!=HTTP_GET;
  const auto callback=[this,index,role,gated,write](){
    if(!gated||authorize(write,role))(this->*handlers[index])();
  };
  // The capture fits the ESP32 std::function inline storage. Sharing one
  // callback type removes per-route thunks without allocating another object.
  static_assert(sizeof(callback)<=2*sizeof(void*),"Route capture exceeds inline storage");
  web.on(path,method,callback);
}

// Both camera views serialize the same summary. Share its code while keeping
// each response's owned JSON values and freshness checks unchanged.
template <typename Camera>
__attribute__((noinline)) uint32_t writeCameraSummary(JsonObject output,
    const Camera& c, const char* id, uint32_t now) {
  const bool online=c.online(now,Config::CAMERA_OFFLINE_MS);
  const uint32_t reportAge=c.present?uint32_t(now-c.seen):UINT32_MAX;
  const bool statusFresh=online&&reportAge<=Config::CAMERA_STATUS_FRESH_MS;
  output["id"]=id;output["online"]=online;output["statusFresh"]=statusFresh;
  output["connectionState"]=c.state(now,Config::CAMERA_OFFLINE_MS);
  output["connections"]=c.connections;
  output["recordingReported"]=online&&c.recording;output["sdReported"]=online&&c.sdReady;
  output["recordingConfirmed"]=statusFresh&&c.recording&&c.sdReady&&cameraFrameFreshAtReport(c.telemetry);
  output["lockSupported"]=online&&c.lockSupported;output["firmware"]=c.firmware;
  output["heartbeats"]=c.heartbeats;output["commandRequests"]=c.commands;
  output["acknowledgements"]=c.acknowledgements;
  if(c.present)output["heartbeatAgeMs"]=reportAge;else output["heartbeatAgeMs"]=nullptr;
  return reportAge;
}

void DashboardServer::acquireState() {
  if (stateMutex && !stateLocked) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    stateLocked = true;
  }
}
void DashboardServer::releaseState() {
  if (stateLocked) {
    stateLocked = false;
    xSemaphoreGive(stateMutex);
  }
}
namespace {
const char* cameraKey(int role){return role?CameraPairing::REAR_KEY:CameraPairing::FRONT_KEY;}
const char* cameraRole(int role){return role?"rear":"front";}
}
void DashboardServer::cameraSession() {
  if(!isCanonicalHost()){sendError(403,"Invalid host");return;}
  const String who=web.header("X-SV-Camera"),client=web.header("X-SV-Client"),signature=web.header("X-SV-MAC");
  const int role=who=="front"?0:who=="rear"?1:-1;
  char expected[65];
  if(role<0||!CameraAuth::hello(cameraKey(role),who.c_str(),client.c_str(),expected)||!CameraCrypto::equal(expected,signature.c_str(),64)){
    sendError(401,"Camera authentication required.");return;
  }
  uint8_t random[16];for(size_t i=0;i<16;i+=4){uint32_t value=esp_random();memcpy(random+i,&value,4);}char nonce[33];CameraCrypto::hex(random,16,nonce);
  CameraAuth::Exchange offer;
  if(!cameraPeers[role].offer(client.c_str(),nonce,millis(),offer)){sendError(429,"Camera session retry pending.");return;}
  if(!CameraAuth::proof(cameraKey(role),who.c_str(),offer,expected)){sendError(503,"Camera authentication unavailable.");return;}
  auto response=makeJson();response["apiVersion"]=2;response["id"]=who;response["client"]=offer.client;response["session"]=offer.session;response["proof"]=expected;sendJson(response);
}
int DashboardServer::cameraIdentity() {
  if(!isCanonicalHost()){sendError(403,"Invalid host");return -1;}
  const String who=web.header("X-SV-Camera"),client=web.header("X-SV-Client"),session=web.header("X-SV-Session");
  const String sequence=web.header("X-SV-Seq"),signature=web.header("X-SV-MAC");
  const int role=who=="front"?0:who=="rear"?1:-1;
  CameraAuth::Exchange request;
  if(role<0||!CameraCrypto::validHex(client.c_str(),32)||!CameraCrypto::validHex(session.c_str(),32)||!CameraAuth::number(sequence.c_str(),request.sequence)){
    sendError(401,"Camera authentication required.");return -1;
  }
  memcpy(request.client,client.c_str(),33);memcpy(request.session,session.c_str(),33);
  const String& body=web.rawBody();
  const char* method=web.method()==HTTP_POST?"POST":"GET";
  if(!cameraPeers[role].accept(cameraKey(role),who.c_str(),request,method,web.uri().c_str(),body.c_str(),body.length(),signature.c_str(),millis())){
    sendError(401,"Camera authentication required.");return -1;
  }
  cameraExchange=request;cameraResponseRole=role;acquireState();return role;
}

void DashboardServer::addSecurityHeaders(bool immutable) {
  web.sendHeader("Cache-Control", immutable ? "public, max-age=31536000, immutable" : "no-store");
  web.sendHeader("X-Content-Type-Options", "nosniff");
  web.sendHeader("X-Frame-Options", "DENY");
  web.sendHeader("Referrer-Policy", "no-referrer");
  web.sendHeader("Content-Security-Policy", WEB_CONTENT_SECURITY_POLICY);
}

void DashboardServer::sendJson(JsonDocument& document, int statusCode) {
  releaseState();  // Network writes never hold the hardware-service mutex.
  addSecurityHeaders();

  String output;
  const size_t length=measureJson(document);
  ++responseHealth.responses;responseHealth.lastBodyBytes=length;
  responseHealth.maxBodyBytes=std::max(responseHealth.maxBodyBytes,length);
  responseHealth.freeBeforeEncode=ESP.getFreeHeap();responseHealth.largestBeforeEncode=ESP.getMaxAllocHeap();
  responseHealth.lastFailureStage=0;
  auto measured=[&](uint8_t stage){if(responseHealth.active)responseHealth.active->finish(length,stage,responseHealth.freeBeforeEncode,responseHealth.largestBeforeEncode);};
  if(document.overflowed()){
    measured(1);
    ++responseHealth.documentFailures;responseHealth.lastFailureStage=1;
    document.clear();
    web.send(503,"application/json","{\"error\":\"Response memory unavailable\"}");return;
  }
  if(!HttpMemoryBudget::available(length+1024)||!output.reserve(length)){
    measured(2);
    ++responseHealth.outputFailures;responseHealth.lastFailureStage=2;
    document.clear();
    web.send(503,"application/json","{\"error\":\"Response memory unavailable\"}");return;
  }
  serializeJson(document, output);
  // Callers finish with sendJson. Release the tree before TCP needs buffers.
  document.clear();
  if(output.length()!=length){
    measured(3);
    ++responseHealth.lengthFailures;responseHealth.lastFailureStage=3;
    web.send(503,"application/json","{\"error\":\"Response memory unavailable\"}");return;
  }
  measured(0);
  if(cameraResponseRole>=0){
    char signature[65];
    if(!CameraAuth::message(cameraKey(cameraResponseRole),cameraRole(cameraResponseRole),cameraExchange,
        web.method()==HTTP_POST?"POST":"GET",web.uri().c_str(),output.c_str(),output.length(),statusCode,signature)){
      web.send(503,"application/json","{\"error\":\"Camera authentication unavailable\"}");return;
    }
    web.sendHeader("X-SV-MAC",signature);
  }
  web.sendOwned(statusCode, "application/json", std::move(output));
}

void DashboardServer::sendSession(Security::Session* session,bool defaultPassword){
  acquireState();
  auto document=makeJson();
  document["csrf"]=session->csrf;
  document["role"]=Access::name(session->role);
  document["defaultPassword"]=defaultPassword;
  document["minPasswordLength"]=Config::RIDER_MIN_PASSWORD_LENGTH;
  document["riderSetupComplete"]=store.data.riderSetupComplete;
  sendJson(document);
}

void DashboardServer::sendError(int statusCode, const String& message) {
  auto document=makeJson();
  document["error"] = message;
  sendJson(document, statusCode);
}

// Literal errors avoid a temporary Arduino String. Still copy the message into
// JSON before sendJson releases shared state, including mutable char* callers.
void DashboardServer::sendError(int statusCode,const char* message){
  auto document=makeJson();document["error"]=JsonString(message,false);sendJson(document,statusCode);
}

bool DashboardServer::isCanonicalHost() {
  return network.allowedHost(web.hostHeader());
}

bool DashboardServer::isValidOrigin() {
  return network.validOrigin(web.header("Origin"), web.hostHeader());
}

Security::Session* DashboardServer::authorize(bool write,
                                              Access::Role required) {
  if (!isCanonicalHost()) {
    sendError(403, "Use the local dashboard address.");
    return nullptr;
  }

  Security::Session* session = security.current(web);
  if (!session) {
    sendError(401, "Please sign in.");
    return nullptr;
  }

  if (!Access::allowed(session->role, required)) {
    sendError(403, "This account cannot access that action.");
    return nullptr;
  }

  if (write) {
    const bool csrfOk =
        Security::equal(web.header("X-CSRF-Token"), session->csrf);

    if (!isValidOrigin() || !csrfOk) {
      sendError(403, "Request verification failed. Reload and sign in.");
      return nullptr;
    }
  }

  acquireState();
  return session;
}

bool DashboardServer::readJsonBody(JsonDocument& document) {
  if (!web.header("Content-Type").startsWith("application/json")) {
    sendError(415, "JSON required.");
    return false;
  }

  const String& raw = web.rawBody();
  if (raw.length() > 4096) {
    sendError(413, "Request too large.");
    return false;
  }

  static JsonRequestMemory memory;
  JsonDocument bounded(&memory);
  const auto error=deserializeJson(bounded,raw);
  if(error==DeserializationError::NoMemory){sendError(503,"Request memory unavailable. Please try again.");return false;}
  if (error || !bounded.is<JsonObject>()) {
    sendError(400, "Invalid JSON object.");
    return false;
  }
  swap(document,bounded);
  return true;
}

bool DashboardServer::sendConnectivityCheck() {
  if(!network.localDeviceMode())return false;
  const auto* reply=HotspotConnectivity::lookup(web.hostHeader().c_str(),web.uri().c_str());
  if(!reply)return false;
  web.sendHeader("Cache-Control","no-store");
  web.sendHeader("Connection","close");
  if(web.method()!=HTTP_GET && web.method()!=HTTP_HEAD){
    web.sendHeader("Allow","GET, HEAD");
    web.send(405,"text/plain","Method not allowed");return true;
  }
  web.send(reply->status,reply->type,web.method()==HTTP_HEAD?"":reply->body);
  return true;
}

void DashboardServer::redirectToDashboard() {
  web.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  web.sendHeader("Pragma", "no-cache");
  web.sendHeader("Expires", "0");
  web.sendHeader("Connection", "close");
  if (!network.captivePortalActive()) {
    sendError(403, "Use the device IP address or configured local hostname.");
    return;
  }
  web.sendHeader("Location", "http://192.168.4.1/", true);
  web.send(302, "text/plain", "Captive portal");
}

void DashboardServer::sendAndroidCaptivePage() {
  // Older and Samsung-modified Android captive-portal clients can ignore a
  // normal 302 from generate_204. Returning HTTP 200 with a real HTML body
  // reliably marks the network as captive, then moves the mini-browser to the
  // ESP32 dashboard.
  static const char page[] PROGMEM =
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<meta http-equiv=\"refresh\" content=\"0;url=http://192.168.4.1/\">"
      "<title>SmartVest</title></head><body>"
      "<p><a href=\"http://192.168.4.1/\">Open SmartVest dashboard</a></p>"
      "</body></html>";

  web.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  web.sendHeader("Pragma", "no-cache");
  web.sendHeader("Expires", "0");
  web.sendHeader("Connection", "close");
  web.send_P(200, "text/html", page);
}

void DashboardServer::sendPortalPage() {
  if (!isCanonicalHost()) {
    sendError(403, "Use the device IP address or configured local hostname.");
    return;
  }
  sendWebAsset("/");
}

void DashboardServer::sendAdminPage() {
  sendWebAsset("/admin");
}

bool DashboardServer::begin() {
  // Initialize TCP/IP without advertising a joinable Wi-Fi network yet.
  if (!Network.begin()) return false;
  diagnosticEpoch=esp_random();if(!diagnosticEpoch)diagnosticEpoch=1;

  const char* collectedHeaders[] = {"Cookie",        "X-CSRF-Token",
                                    "Origin",        "Content-Type",
                                    "Authorization", "User-Agent", "X-SV-Camera", "X-SV-Client", "X-SV-Session", "X-SV-Seq", "X-SV-MAC"};
  web.collectHeaders(collectedHeaders, sizeof(collectedHeaders)/sizeof(collectedHeaders[0]));

  // ---------------------------------------------------------------------------
  // Captive portal detection
  // ---------------------------------------------------------------------------
  // Android 4.4+ historically probes generate_204. Some Samsung/Android builds
  // are more reliable when that probe receives HTTP 200 content instead of a
  // simple 302, so Android-style probes get a tiny meta-refresh page.
  const char* androidCaptivePaths[] = {
      "/generate_204", "/gen_204", "/generate204", "/connectivity-check.html",
      "/mobile/status.php"};

  // Apple, Windows, Firefox and generic probes are redirected to the canonical
  // AP address. Wildcard DNS makes their original hostnames resolve here.
  const char* redirectCaptivePaths[] = {"/redirect",
                                        "/hotspot-detect.html",
                                        "/library/test/success.html",
                                        "/success.html",
                                        "/success.txt",
                                        "/ncsi.txt",
                                        "/connecttest.txt",
                                        "/check_network_status.txt",
                                        "/fwlink",
                                        "/fwlink/",
                                        "/canonical.html",
                                        "/startpage",
                                        "/portal.html",
                                        "/kindle-wifi/wifistub.html"};

  if (Config::NETWORK_USES_AP && (Config::HOTSPOT_LOCAL_DEVICE_MODE || Config::ENABLE_CAPTIVE_PORTAL)) {
    for (const char* path : androidCaptivePaths) {
      web.on(path, HTTP_ANY, [this]() {
        if(sendConnectivityCheck())return;
        if(network.captivePortalActive())sendAndroidCaptivePage();
        else sendError(404,"Not found.");
      });
    }

    for (const char* path : redirectCaptivePaths) {
      web.on(path, HTTP_ANY, [this]() {
        if(sendConnectivityCheck())return;
        if(network.captivePortalActive())redirectToDashboard();
        else sendError(404,"Not found.");
      });
    }

    web.on("/wpad.dat", HTTP_ANY,
           [this]() { web.send(404, "text/plain", ""); });

    web.on("/favicon.ico", HTTP_ANY,
           [this]() { web.send(404, "text/plain", ""); });

    web.on("/portal", HTTP_ANY, [this]() {
      if (web.method() != HTTP_GET && web.method() != HTTP_HEAD) {
        redirectToDashboard();
        return;
      }
      sendPortalPage();
    });
  }

  // ---------------------------------------------------------------------------
  // Dashboard and API routes
  // ---------------------------------------------------------------------------
  web.on("/", HTTP_ANY, [this]() {
    if(sendConnectivityCheck())return;
    if (network.captivePortalActive() && !isCanonicalHost()) {
      const String userAgent = web.header("User-Agent");
      if (userAgent.indexOf("Android") >= 0) {
        sendAndroidCaptivePage();
      } else {
        redirectToDashboard();
      }
      return;
    }

    if (web.method() != HTTP_GET && web.method() != HTTP_HEAD) {
      web.send(405, "text/plain", "Method not allowed");
      return;
    }

    sendPortalPage();
  });

  web.on("/admin", HTTP_GET, [this]() {
    if (!isCanonicalHost()) {
      redirectToDashboard();
      return;
    }
    sendAdminPage();
  });
  web.on("/admin/", HTTP_GET, [this]() {
    if (!isCanonicalHost()) {
      redirectToDashboard();
      return;
    }
    sendAdminPage();
  });
  registerRiderRoutes();

  web.on("/api/v1/session", HTTP_GET, [this]() {
    Security::Session* session = authorize(false, Access::Role::Rider);
    if (!session) return;

    sendSession(session, store.data.defaultPassword);
  });

  bindRoute("/api/v1/login",HTTP_POST,&DashboardServer::beginLogin,Access::Role::Developer,false);

  web.on("/api/v1/logout", HTTP_POST, [this]() {
    Security::Session* session = authorize(true, Access::Role::Rider);
    if (!session) return;

    if(download.active()&&download.ownedBy(session->csrf.c_str()))download.state=6;
    *session = Security::Session();
    web.sendHeader("Set-Cookie",
                   "SVSID=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");

    auto document=makeJson();
    document["ok"] = true;
    sendJson(document);
  });

  bindRoute("/api/v1/status",HTTP_GET,&DashboardServer::sendStatus);
  bindRoute("/api/v1/rider/pose",HTTP_GET,&DashboardServer::sendRiderPose,Access::Role::Developer,false);
  bindRoute("/api/v1/rider/calibration",HTTP_POST,&DashboardServer::commandRiderCalibration,Access::Role::Developer,false);
  bindRoute("/api/v1/sensing/imu",HTTP_GET,&DashboardServer::sendImuLab,Access::Role::Developer,false);
  bindRoute("/api/v1/sensing/command",HTTP_POST,&DashboardServer::commandImuLab,Access::Role::Developer,false);
  bindRoute("/api/v1/sensing/piezo",HTTP_POST,&DashboardServer::commandPiezo,Access::Role::Developer,false);
  bindRoute("/api/v1/gsm/trace",HTTP_GET,&DashboardServer::sendModemTrace);

  web.on("/api/v1/power/led-test", HTTP_POST, [this]() {
    if (!authorize(true, Access::Role::Developer)) return;
    auto request=makeJson();
    if (!readJsonBody(request)) return;
    if (!request.is<JsonObject>() || request.size() != 1 || !request["mode"].is<const char*>()) {
      sendError(400, "Choose one LED test mode."); return;
    }
    const String mode = request["mode"].as<String>();
    if (mode != "stop" && (!Config::ENABLE_BENCH_TESTS || !Config::ENABLE_BATTERY_LED)) {
      sendError(403, "Enable bench tests and the battery LED in Config.h."); return;
    }
    if (!battery.startLedTest(mode.c_str())) { sendError(400, "Unknown LED test mode."); return; }
    auto reply=makeJson();
    reply["ok"] = true;
    battery.writeJson(reply["power"].to<JsonObject>());
    sendJson(reply);
  });

  bindRoute("/api/v1/settings",HTTP_GET,&DashboardServer::sendSettings);

  bindRoute("/api/v1/settings",HTTP_POST,&DashboardServer::saveSettings);

  web.on("/api/v1/storage/retry", HTTP_POST, [this]() {
    if (!authorize(true)) return;
    if (!store.commit(store.data)) {
      sendError(503, store.lastError);
      return;
    }
    auto response=makeJson();
    response["ok"] = true;
    sendJson(response);
  });

  bindRoute("/api/v1/messages/cancel",HTTP_POST,&DashboardServer::cancelMessages,Access::Role::Rider);

  bindRoute("/api/v1/jobs",HTTP_GET,&DashboardServer::sendJobs);

  bindRoute("/api/v1/messages/check",HTTP_POST,&DashboardServer::queueContactCheck);

  bindRoute("/api/v1/messages/introduction",HTTP_POST,&DashboardServer::queueIntroduction);
  web.on("/api/v1/messages/crash", HTTP_POST, [this]() {
    if (authorize(true)) queueManualMessage("manual_crash");
  });

  bindRoute("/api/v1/jobs/action",HTTP_POST,&DashboardServer::handleJobAction);

  bindRoute("/api/v1/security/password",HTTP_POST,&DashboardServer::changePassword);

  bindRoute("/api/v1/integrations",HTTP_GET,&DashboardServer::sendIntegrations);

  web.on("/api/v1/cameras/files", HTTP_GET, [this](){serveCameraFiles(false);});
  web.on("/api/v1/cameras/files", HTTP_POST, [this](){serveCameraFiles(true);});
  bindRoute("/api/v1/cameras/files/result",HTTP_POST,&DashboardServer::receiveCameraFiles,Access::Role::Developer,false);
  bindRoute("/api/v1/cameras/monitor",HTTP_GET,&DashboardServer::sendCameraMonitor,Access::Role::Developer,false);
  web.on("/api/v1/cameras/clock", HTTP_POST, [this](){
    if(!authorize(true))return;auto body=makeJson();if(!readJsonBody(body))return;
    const uint64_t epoch=body["utcMs"]|uint64_t(0);
    if(!body["utcMs"].is<uint64_t>() || !UtcClock::valid(epoch)){sendError(400,"Invalid UTC timestamp.");return;}
    namingClock.set(epoch,esp_timer_get_time()/1000ULL,2);
    auto response=makeJson();response["ok"]=true;writeClock(response["clock"].to<JsonObject>());sendJson(response);
  });
  bindRoute("/api/v1/cameras/session",HTTP_GET,&DashboardServer::cameraSession,Access::Role::Developer,false);
  bindRoute("/api/v1/cameras/heartbeat",HTTP_POST,&DashboardServer::cameraHeartbeat,Access::Role::Developer,false);

  web.on("/api/v1/cameras/commands", HTTP_GET, [this]() {
    const int camera = cameraIdentity();
    if (camera < 0) return;
    auto doc=makeJson();
    emergency->cameraCommand(camera, doc.to<JsonObject>());
    offerCameraFiles(camera,doc.as<JsonObject>());
    doc["apiVersion"] = 1;
    doc["id"] = camera ? "rear" : "front";
    ++cameras[camera].commands;
    sendJson(doc);
  });
  web.on("/api/v1/cameras/ack", HTTP_POST, [this]() {
    const int camera = cameraIdentity();
    if (camera < 0) return;
    auto body=makeJson();
    if (!readJsonBody(body)) return;
    if (!body["boot"].is<uint32_t>() || !body["sequence"].is<uint32_t>() ||
        !body["stage"].is<const char*>()) {
      sendError(400, "boot, sequence and stage required");
      return;
    }
    if (!emergency->cameraAck(camera, body["boot"], body["sequence"],
                              body["stage"].as<String>())) {
      sendError(409, "Unknown incident, invalid stage or flash unavailable");
      return;
    }
    ++cameras[camera].acknowledgements;
    auto response=makeJson();
    response["ok"] = true;
    sendJson(response);
  });
  web.on("/api/v1/incidents", HTTP_GET, [this]() {
    if (!authorize()) return;
    auto doc=makeJson();
    emergency->writeJson(doc.to<JsonObject>());
    writeMessageQueue(doc["messageQueue"].to<JsonObject>(),false);
    sendJson(doc);
  });
  web.on("/api/v1/cameras/test", HTTP_POST, [this]() {
    if (!authorize(true)) return;
    if (!Config::ENABLE_BENCH_TESTS) {
      sendError(403, "Bench testing disabled in Config.h"); return;
    }
    auto body=makeJson();
    if (!readJsonBody(body)) return;
    if (manualMessageSent && !Domain::elapsed(millis(), lastManualMessage,
                                             Config::MANUAL_SMS_COOLDOWN_MS)) {
      sendError(429, "Wait before another test"); return;
    }
    if (!testBoot) { testBoot = esp_random(); if (!testBoot) testBoot = 1; }
    CrashProtocol::Frame f;
    f.type = CrashProtocol::Type::Test; f.boot = testBoot;
    f.sequence = ++testSequence;
    if (!emergency->accept(f, true, 0, true)) {
      sendError(503, emergency->lastError); return;
    }
    manualMessageSent = true; lastManualMessage = millis();
    auto response=makeJson();
    response["ok"] = true; response["boot"] = f.boot;
    response["sequence"] = f.sequence; response["smsQueued"] = false;
    sendJson(response, 202);
  });
  web.on("/api/v1/crash/test", HTTP_POST, [this]() {
    if (!authorize(true)) return;
    if (!Config::ENABLE_BENCH_TESTS) {
      sendError(403, "Bench testing disabled in Config.h");
      return;
    }
    auto body=makeJson();
    if (!readJsonBody(body)) return;
    if (manualMessageSent && !Domain::elapsed(millis(), lastManualMessage,
                                              Config::MANUAL_SMS_COOLDOWN_MS)) {
      sendError(429, "Wait before another test");
      return;
    }
    size_t contacts = 0;
    for (const auto& c : store.data.contacts)
      if (c.phone.length()) ++contacts;
    if (!contacts || !Config::ENABLE_SIM800L) {
      sendError(409, "Enable GSM and save at least one contact first");
      return;
    }
    if (!testBoot) {
      testBoot = esp_random();
      if (!testBoot) testBoot = 1;
    }
    CrashProtocol::Frame f;
    f.type = CrashProtocol::Type::Test;
    f.boot = testBoot;
    f.sequence = ++testSequence;
    if (!emergency->accept(f, true, 0)) {
      sendError(503, emergency->lastError);
      return;
    }
    manualMessageSent = true;
    lastManualMessage = millis();
    auto response=makeJson();
    response["ok"] = true;
    response["boot"] = f.boot;
    response["sequence"] = f.sequence;
    sendJson(response, 202);
  });

  web.on("/api/v1/crash", HTTP_POST, [this]() {
    if (authorize(true)) {
      sendError(405,
                "Real crash events come from the sensing UART. Use "
                "/api/v1/crash/test for labelled bench testing.");
    }
  });

  web.onNotFound([this]() {
    // Static/page routes share one flash lookup instead of per-route heap objects.
    if (sendWebAsset(web.uri().c_str())) return;
    if (web.uri().startsWith("/api/")) {
      sendError(404, "Unknown API endpoint.");
      return;
    }

    if(sendConnectivityCheck())return;

    if (network.captivePortalActive()) {
      const String userAgent = web.header("User-Agent");
      if (userAgent.indexOf("Android") >= 0) {
        sendAndroidCaptivePage();
      } else {
        redirectToDashboard();
      }
      return;
    }

    sendError(404, "Not found.");
  });

  web.begin();
  return web.listening();
}

void DashboardServer::tick() {
  // This task already owns a listening server and complete route table before
  // association/DHCP can invite a client's first connectivity probe.
  if(!networkStarted){
    const uint32_t now=millis();
    if(networkRetryAt&&int32_t(now-networkRetryAt)<0)return;
    networkRetryAt=now+Config::WIFI_RETRY_INITIAL_MS;
    if(!startNetwork())return;
  }
  cameraResponseRole=-1;download.tick(millis());
  network.tick();
  for(auto& camera:cameras)camera.expire(millis(),Config::CAMERA_OFFLINE_MS);
  if (uint32_t(millis()-lastRuntimeSnapshot) >= 1000) {
    lastRuntimeSnapshot = millis();
    RuntimeTelemetry next;
    next.webReady = true;next.capturedAt=millis();
    next.httpRequests=web.admitted;next.httpExpired=web.expired;
    next.httpRejected=web.rejected;next.maxHttpMs=web.maxHandleMs;
    next.httpWriteAborts=web.writeAborts;next.httpFlashCompleted=web.flashCompleted;
    next.maxFlashMs=web.maxFlashMs;next.httpSending=web.sending();
    next.httpRequestMemoryFailures=web.requestMemoryFailures;next.httpResponseMemoryFailures=web.responseMemoryFailures;
    next.httpDocumentFailures=responseHealth.documentFailures;next.httpOutputFailures=responseHealth.outputFailures;
    next.httpLengthFailures=responseHealth.lengthFailures;next.httpQueuedBytes=web.queuedBytes();next.httpMaxQueuedBytes=web.maxQueuedBytes;
    next.apStarts=network.apStarts();next.apStops=network.apStops();
    next.clientJoins=network.clientJoins();next.clientLeaves=network.clientLeaves();
    next.clients=network.hotspotClients();
    next.radio=network.radioState();
    next.networkOnline = network.stationConnected() || network.apActive();
    next.connecting = network.connecting(); next.hotspot = network.apActive();
    next.mdns = network.mdnsActive(); next.attempts = network.attempts(); next.retryInMs = network.retryInMs();
    const String ip = network.localIp();
    snprintf(next.ip, sizeof(next.ip), "%s", ip.c_str());
    acquireState();
    for (int i=0; i<2; ++i) {
      const auto& c = cameras[i];
      const bool online=c.online(millis(),Config::CAMERA_OFFLINE_MS);
      next.cameras[i] = {online,
                         online && c.recording, online && c.sdReady, c.heartbeats, c.commands, c.acknowledgements,
                         c.present ? uint32_t(millis()-c.seen) : UINT32_MAX};
    }
    runtime = next;
    releaseState();
  }
  finishLogin();
  web.handleClient();
  network.observeService(web.admitted,cameras[0].present&&cameras[1].present);
  releaseState();  // Also covers early returns.
}

void DashboardServer::sendSettings() {
  auto document=makeJson();
  StateStore::writeSettingsJson(store.data, document.to<JsonObject>());
  document["maxContacts"] = Config::MAX_CONTACTS;
  document["settingsAvailable"] = store.healthy && store.writable;
  sendJson(document);
}

void DashboardServer::sendModemTrace() {
  // authorize() holds the service mutex. Copy at most eight rows into an owning
  // JSON tree, then sendJson releases the mutex before any network output.
  DiagnosticPaging::Query query;
  if(!query.read(web)){sendError(400,"Invalid diagnostic cursor.");return;}
  const bool failurePage=web.arg("section")=="failure";
  if(web.hasArg("section")&&!failurePage){sendError(400,"Unknown trace section.");return;}
  ResponseMemory::Allocator memory(responseHealth,ResponseMemory::Endpoint::Trace);JsonDocument document(&memory);
  const auto& snapshot=sim800l.traceView();
  document["format"]="smartvest-modem-trace-v1";
  document["firmware"]=Firmware::VERSION;document["capturedAtMs"]=millis();
  document["totalEntries"]=snapshot.total;document["capacity"]=SIM800LService::TRACE_CAPACITY;
  document["note"]="Paged RAM history; body content omitted. Main reboot clears history. Export includes retained pages and reports overwritten entries.";
  auto appendEntry=[](JsonArray entries,const SIM800LService::TraceEntry& entry,uint32_t serial) __attribute__((noinline)) {
    auto row=entries.add<JsonObject>();row["serial"]=serial;row["atMs"]=entry.at;row["jobId"]=entry.jobId;
    row["direction"]=entry.direction=='>'?"TX":entry.direction=='<'?"RX":"EVENT";
    row["text"]=JsonString(entry.text,false);row["truncated"]=entry.truncated;
  };
  auto entries=document["entries"].to<JsonArray>();
  DiagnosticPaging::Window window(snapshot.total,snapshot.count,query,diagnosticEpoch);
  if(!failurePage){
    window.write(document["page"].to<JsonObject>(),diagnosticEpoch);
    for(size_t i=0;i<window.count;++i){
      const size_t newest=window.offset+i;
      const size_t index=(snapshot.next+SIM800LService::TRACE_CAPACITY-1-newest)%SIM800LService::TRACE_CAPACITY;
      appendEntry(entries,snapshot.entries[index],snapshot.total-uint32_t(newest));
    }
  }
  const auto& failure=snapshot.lastFailure;
  if(failure.present){
    auto last=document["lastFailure"].to<JsonObject>();
    last["revision"]=failure.revision;last["atMs"]=failure.at;last["jobId"]=failure.jobId;
    last["bodySent"]=failure.bodySent;last["gotReference"]=failure.gotReference;
    last["modemReference"]=failure.modemReference;last["signalCsq"]=failure.signalCsq;
    last["commandElapsedMs"]=failure.commandElapsedMs;
    if(failure.rxAgeMs==UINT32_MAX)last["rxAgeMs"]=nullptr;else last["rxAgeMs"]=failure.rxAgeMs;
    last["stage"]=JsonString(failure.stage,false);last["command"]=JsonString(failure.command,false);last["reason"]=JsonString(failure.reason,false);
    last["entryCount"]=failure.count;
    auto failedEntries=last["entries"].to<JsonArray>();
    if(failurePage){
      const bool changed=query.hasRevision&&query.revision!=failure.revision;
      DiagnosticPaging::Window failed(uint32_t(failure.count),failure.count,query,diagnosticEpoch,changed);
      failed.write(document["page"].to<JsonObject>(),diagnosticEpoch);
      document["page"]["revision"]=failure.revision;
      for(size_t i=0;i<failed.count;++i){const size_t index=failure.count-1-failed.offset-i;appendEntry(failedEntries,failure.entries[index],uint32_t(index+1));}
    }
  }else{
    document["lastFailure"]=nullptr;
    if(failurePage){DiagnosticPaging::Window empty(0,0,query,diagnosticEpoch,true);empty.write(document["page"].to<JsonObject>(),diagnosticEpoch);}
  }
  sendJson(document);
}

void DashboardServer::sendStatus() {
  ResponseMemory::Allocator memory(responseHealth,ResponseMemory::Endpoint::Status);JsonDocument document(&memory);

  const String view=web.arg("view");
  const bool full=view.length()==0,ride=view=="ride";
  const bool overview=view=="overview",system=view=="system",tools=view=="tools";
  const bool gsm=full||ride||view=="gsm",gps=full||ride||overview||view=="gps";
  const bool power=full||ride||overview||tools||view=="power",networkView=full||ride||overview||view=="network";
  if(!full&&!overview&&!system&&!gsm&&!gps&&!power&&!networkView){
    document["error"]="Unknown status view";sendJson(document,400);return;
  }
  // Share the live summary across diagnostics and the smaller page responses.
  const auto writeGsm=[this](JsonObject g) __attribute__((noinline)) {
    g["enabled"]=Config::ENABLE_SIM800L;g["online"]=sim800l.online();
    g["simReady"]=sim800l.simReady();g["readyToSend"]=sim800l.readyToSend();
    g["simStatus"]=sim800l.simStatusText();g["registrationText"]=sim800l.registrationText();
    g["registration"]=sim800l.simReady()&&sim800l.registrationCurrent()?sim800l.registration:-1;
    g["rxAgeMs"]=nullptr;if(sim800l.rxAgeMs()!=UINT32_MAX)g["rxAgeMs"]=sim800l.rxAgeMs();
  };
  document["product"] = Config::PRODUCT_NAME;
  document["firmwareVersion"] = Firmware::VERSION;
  document["releaseDate"] = Firmware::RELEASE_DATE;
  document["resetReason"] = unsigned(esp_reset_reason());
  if(full||system){
  auto httpHealth=document["http"].to<JsonObject>();
  responseHealth.write(httpHealth["responses"].to<JsonObject>());
  httpHealth["requests"]=web.admitted;httpHealth["rejected"]=web.rejected;
  httpHealth["expired"]=web.expired;httpHealth["waiting"]=web.waiting();
  httpHealth["maxHandleMs"]=web.maxHandleMs;
  httpHealth["loginPending"]=login.busy();httpHealth["lastLoginMs"]=login.lastMs;httpHealth["maxLoginMs"]=login.maxMs;
  httpHealth["requestBodyLimitBytes"]=HttpRequest::MAX_BODY;
  httpHealth["requestHeaderLimitBytes"]=HttpRequest::MAX_HEADERS;
  httpHealth["malformedRequests"]=web.malformedRequests;
  httpHealth["oversizedRequests"]=web.oversizedRequests;
  httpHealth["requestMemoryFailures"]=web.requestMemoryFailures;
  httpHealth["responseMemoryFailures"]=web.responseMemoryFailures;
  httpHealth["headerTimeouts"]=web.headerTimeouts;
  httpHealth["bodyTimeouts"]=web.bodyTimeouts;
  httpHealth["peerDisconnects"]=web.peerDisconnects;
  httpHealth["responseCompleted"]=web.responseCompleted;
  httpHealth["queuedResponseBytes"]=web.queuedBytes();
  httpHealth["maxQueuedResponseBytes"]=web.maxQueuedBytes;
  auto radioHealth=document["radio"].to<JsonObject>();
  radioHealth["apStarts"]=network.apStarts();radioHealth["apStops"]=network.apStops();
  radioHealth["clientJoins"]=network.clientJoins();radioHealth["clientLeaves"]=network.clientLeaves();
  radioHealth["clients"]=network.hotspotClients();radioHealth["apActive"]=network.apActive();
  const auto ap=network.radioState();
  radioHealth["configured"]=ap.configured;radioHealth["ssidVisible"]=ap.visible;
  radioHealth["dhcpRunning"]=ap.dhcp;radioHealth["channel"]=ap.channel;
  radioHealth["protocolBitmap"]=ap.protocol;radioHealth["clientLimit"]=ap.limit;
  radioHealth["driverError"]=ap.error;radioHealth["recoveryAttempts"]=ap.recoveryAttempts;
  radioHealth["lastApStartMs"]=ap.lastStartMs;radioHealth["lastApStopMs"]=ap.lastStopMs;
  radioHealth["lastDisconnectReason"]=ap.disconnectReason;
  radioHealth["serviceRecoveries"]=ap.serviceRecoveries;radioHealth["authenticationFailures"]=ap.authFailures;
  }
  if(full||system||view=="gps"){
  JsonObject console = document["serialMonitor"].to<JsonObject>();
  console["enabled"] = Config::ENABLE_USB_DEBUG; console["baud"] = Config::DEBUG_BAUD;
  console["uart"] = 1; console["txPin"] = Config::DEBUG_TX_PIN;
  console["droppedLines"] = Console::droppedLines(); console["txBytes"] = Console::transmittedBytes();
  console["changesOnly"] = true;
  }
  if(networkView){
  document["wifiName"] = network.networkName();
  // Read-only telemetry; no network configuration endpoint or credentials.
  JsonObject wifi = document["network"].to<JsonObject>();
  wifi["mode"] = network.modeName();
  wifi["stationConnected"] = network.stationConnected();
  wifi["hotspotActive"] = network.apActive();
  wifi["dashboardUrl"] = network.dashboardUrl();
  wifi["ip"] = network.localIp(); wifi["hostname"] = Config::LOCAL_HOSTNAME;
  wifi["mdnsActive"] = network.mdnsActive(); wifi["mdnsEnabled"] = Config::ENABLE_MDNS;
  wifi["captivePortalActive"] = network.captivePortalActive();
  wifi["localDeviceMode"] = network.localDeviceMode();
  wifi["connecting"] = network.connecting(); wifi["attempts"] = network.attempts();
  wifi["retryInMs"] = network.retryInMs(); wifi["error"] = network.error();
  wifi["httpSending"] = web.sending();wifi["httpWriteAborts"] = web.writeAborts;
  wifi["httpFlashCompleted"] = web.flashCompleted;wifi["maxFlashMs"] = web.maxFlashMs;
  wifi["signalDbm"] = nullptr;
  if (network.stationConnected()) wifi["signalDbm"] = network.signalDbm();
  }
  document["uptimeSeconds"] = millis() / 1000;
  document["storageHealthy"] = store.healthy;
  document["storageWritable"] = store.writable;
  document["storageError"] = store.lastError;
  document["defaultPassword"] = store.data.defaultPassword;
  document["stations"] = network.hotspotClients();
  document["freeHeapBytes"] = ESP.getFreeHeap();
  document["minimumFreeHeapBytes"] = ESP.getMinFreeHeap();
  document["largestHeapBlockBytes"] = ESP.getMaxAllocHeap();

  // Readiness telemetry is separate from validated classification and actual SD
  // protection.
  bool recipientsConfigured = false;
  for (const auto& c : store.data.contacts)
    if (c.phone.length()) recipientsConfigured = true;
  document["alertRecipientsConfigured"] = recipientsConfigured;
  document["crashDetection"] = sensorLink && sensorLink->armedAndHealthy() &&
                               emergency && emergency->writable &&
                               store.healthy && recipientsConfigured;
  if ((full||ride||overview)&&sensorLink) sensorLink->writeJson(document["sensing"].to<JsonObject>());
  if (full&&emergency) emergency->writeJson(document["emergency"].to<JsonObject>());
  else if(system&&emergency){
    auto e=document["emergency"].to<JsonObject>();
    e["loaded"]=emergency->isLoaded();e["writable"]=emergency->writable;e["error"]=emergency->lastError;
  }
  document["benchTestsEnabled"] = Config::ENABLE_BENCH_TESTS;
  document["videoLock"] = false;
  document["automaticCrashDetectionEnabled"]=Config::ARM_CRASH_DETECTION;

  if(overview||tools){
    auto g=document["gsm"].to<JsonObject>();
    writeGsm(g);
    if(tools){
      g["simCarrier"]=sim800l.simReady()?sim800l.simCarrierName:String("Unavailable");
      g["simAgeMs"]=nullptr;if(sim800l.simAgeMs()!=UINT32_MAX)g["simAgeMs"]=sim800l.simAgeMs();
    }
  }
  if(gsm||tools){
  if(gsm){
  JsonObject gsmJson = document["gsm"].to<JsonObject>();
  writeGsm(gsmJson);
  gsmJson["uart"] = 2;
  sim800l.uartHealth.writeJson(gsmJson["uartErrors"].to<JsonObject>());
  gsmJson["rxBytes"] = sim800l.rxBytes; gsmJson["txBytes"] = sim800l.txBytes;
  gsmJson["receivedLines"] = sim800l.receivedLines; gsmJson["commandsSent"] = sim800l.commandsSent;
  gsmJson["repliesReceived"] = sim800l.repliesReceived;
  gsmJson["txAgeMs"] = nullptr;
  if (sim800l.txAgeMs() != UINT32_MAX) gsmJson["txAgeMs"] = sim800l.txAgeMs();

  if (Config::ENABLE_SIM800L) {
    gsmJson["sim"] = sim800l.sim;
    gsmJson["simDetected"] = sim800l.simDetected();
    gsmJson["simDetail"] = sim800l.simStatusDetail();
    gsmJson["simAgeMs"] = nullptr;
    if (sim800l.simAgeMs() != UINT32_MAX) gsmJson["simAgeMs"] = sim800l.simAgeMs();
    gsmJson["registrationAgeMs"] = nullptr;
    if (sim800l.registrationAgeMs() != UINT32_MAX) gsmJson["registrationAgeMs"] = sim800l.registrationAgeMs();
    gsmJson["signalAgeMs"] = nullptr;
    if (sim800l.signalAgeMs() != UINT32_MAX) gsmJson["signalAgeMs"] = sim800l.signalAgeMs();
    gsmJson["registered"] = sim800l.registered();
    gsmJson["configured"] = sim800l.configured();
    gsmJson["recovering"] = sim800l.recovering();
    gsmJson["sending"] = sim800l.transactionActive();
    gsmJson["operator"] = sim800l.registered() ? sim800l.operatorName : String("Not registered");
    gsmJson["operatorCode"] = sim800l.registered() ? sim800l.operatorCode : String();
    gsmJson["accessTechnology"] = sim800l.accessTechnology;
    gsmJson["simCarrier"] = sim800l.simReady() ? sim800l.simCarrierName : String("Unavailable");
    gsmJson["simCarrierCode"] = sim800l.simReady() ? sim800l.simCarrierCode : String();
    gsmJson["subscriberNumber"] = sim800l.simReady() ? sim800l.subscriberNumber : String();
    gsmJson["subscriberNumberStatus"] = sim800l.simReady() ? sim800l.subscriberNumberStatus : String("Unavailable");
    gsmJson["iccidStatus"] = sim800l.iccidStatus;
    gsmJson["iccidLast4"] = sim800l.simReady() && sim800l.iccid.length() >= 4
                                ? sim800l.iccid.substring(sim800l.iccid.length()-4)
                                : String();
    gsmJson["rxPin"] = Config::SIM800L_RX_PIN;
    gsmJson["txPin"] = Config::SIM800L_TX_PIN;
    gsmJson["csq"] = sim800l.signalCurrent() ? sim800l.csq : -1;
    gsmJson["baud"] = sim800l.baud;
    gsmJson["lastError"] = sim800l.lastError;
    gsmJson["lastCommand"] = sim800l.lastCommand;
    gsmJson["lastErrorCommand"] = sim800l.lastErrorCommand;
    gsmJson["lastSmsError"] = sim800l.lastSmsError;
    gsmJson["lastSmsErrorCommand"] = sim800l.lastSmsErrorCommand;
    gsmJson["lastPowerEvent"] = sim800l.lastPowerEvent;
    gsmJson["lastModemLine"] = sim800l.lastModemLine;
    gsmJson["serviceCenter"] = sim800l.serviceCenter;
    gsmJson["synchronizationRecoveries"] = sim800l.synchronizationRecoveries;
    gsmJson["unexpectedPrompts"] = sim800l.unexpectedPrompts;
    gsmJson["modemRestarts"] = sim800l.modemRestarts;
    gsmJson["smsReadyEvents"] = sim800l.smsReadyEvents;
    gsmJson["deliveryReportsConfigured"] = sim800l.deliveryReportsConfigured;
    gsmJson["outgoingSmsSupported"] = sim800l.outgoingSmsSupported;
    gsmJson["smsServiceMode"] = sim800l.smsServiceMode;
    gsmJson["stage"] = sim800l.stage();
    gsmJson["commandPending"] = sim800l.commandPending();
    gsmJson["commandAgeMs"] = sim800l.commandAgeMs();
    gsmJson["commandRemainingMs"] = sim800l.commandRemainingMs();
    gsmJson["deliveryReportsReceived"] = sim800l.deliveryReportsReceived;
    gsmJson["deliveryReportsMatched"] = sim800l.deliveryReportsMatched;
    gsmJson["deliveryReportsRejected"] = sim800l.deliveryReportsRejected;
    gsmJson["lastDeliveryReport"] = sim800l.lastDeliveryReport;
    if (sim800l.responseAgeMs() != UINT32_MAX)
      gsmJson["responseAgeMs"] = sim800l.responseAgeMs();
    else
      gsmJson["responseAgeMs"] = nullptr;
    gsmJson["lastRegistrationResponse"] = sim800l.lastRegistrationResponse;
    gsmJson["recoveryStatus"] = sim800l.recoveryStatus;
    gsmJson["commandTimeouts"] = sim800l.commandTimeouts;
    gsmJson["maxServiceGapMs"] = sim800l.maxServiceGapMs;

    if (sim800l.signalCurrent()) {
      gsmJson["dbm"] = -113 + 2 * sim800l.csq;
    } else {
      gsmJson["dbm"] = nullptr;
    }
  } else {
    gsmJson["online"] = false;
    gsmJson["sim"] = "Disabled";
    gsmJson["simDetected"] = false;
    gsmJson["simReady"] = false;
    gsmJson["simStatus"] = "Disabled";
    gsmJson["simDetail"] = "Cellular module is disabled in Config.h.";
    gsmJson["registered"] = false;
    gsmJson["registration"] = nullptr;
    gsmJson["registrationText"] = "Disabled";
    gsmJson["readyToSend"] = false;
    gsmJson["sending"] = false;
    gsmJson["operator"] = "Disabled";
    gsmJson["operatorCode"] = "";
    gsmJson["accessTechnology"] = nullptr;
    gsmJson["simCarrier"] = "Disabled";
    gsmJson["simCarrierCode"] = "";
    gsmJson["subscriberNumber"] = "";
    gsmJson["subscriberNumberStatus"] = "Unavailable";
    gsmJson["iccidStatus"] = "Unavailable";
    gsmJson["iccidLast4"] = "";
    gsmJson["rxPin"] = Config::SIM800L_RX_PIN;
    gsmJson["txPin"] = Config::SIM800L_TX_PIN;
    gsmJson["csq"] = nullptr;
    gsmJson["baud"] = nullptr;
    gsmJson["lastError"] = "";
    gsmJson["dbm"] = nullptr;
  }

  }
  if(!ride){
  JsonObject sms = document["sms"].to<JsonObject>();
  unsigned waitingCount = 0, sendingCount = 0;
  for (const auto& job : store.data.jobs) {
    if (!job.id) continue;
    if (job.state == "queued" || job.state == "retry") ++waitingCount;
    if (job.state == "sending") ++sendingCount;
  }
  sms["waiting"] = waitingCount; sms["sending"] = sendingCount;
  sms["stage"] = sim800l.stage();
  JsonArray recent = sms["recent"].to<JsonArray>();
  uint32_t before = UINT32_MAX;
  for (unsigned count = 0; count < 5; ++count) {
    const SmsJob* found = nullptr;
    for (const auto& job : store.data.jobs)
      if (job.id && job.id < before && (!found || job.id > found->id)) found = &job;
    if (!found) break;
    before = found->id;
    JsonObject item = recent.add<JsonObject>();
    item["id"] = found->id; item["kind"] = found->kind;
    item["test"] = found->kind == "check" ||
        found->text.startsWith(String(Config::SMS_TEST_HEADER) + "\n") ||
        found->text.startsWith("TEST CRASH:") || found->text.startsWith("TEST:");
    item["phone"] = found->phone; item["state"] = found->state;
    item["detail"] = found->detail; item["attempts"] = found->attempts;
  }

  }
  }
  if(gps){
  JsonObject gpsJson = document["gps"].to<JsonObject>();
  neo6m.writeJson(gpsJson);

  }
  if(power){
  JsonObject powerJson = document["power"].to<JsonObject>();
  if(tools)battery.writeIndicatorJson(powerJson);else battery.writeJson(powerJson);

  }
  if(full||ride||overview){
  JsonArray cameraArray = document["cameras"].to<JsonArray>();
  const uint32_t cameraNow = millis();
  for (int i = 0; i < 2; ++i) {
    JsonObject cameraJson = cameraArray.add<JsonObject>();
    writeCameraSummary(cameraJson,cameras[i],i?"rear":"front",cameraNow);
  }

  }
  sendJson(document);
}

void DashboardServer::saveSettings() {
  auto body=makeJson();
  if (!readJsonBody(body)) return;

  if (!body["revision"].is<uint32_t>() ||
      body["revision"].as<uint32_t>() != store.data.revision) {
    sendError(
        409,
        "Settings changed elsewhere. Reload saved settings before applying.");
    return;
  }

  if (!body["rider"].is<String>() || !body["notifyAdded"].is<bool>() ||
      !body["contacts"].is<JsonArray>()) {
    sendError(400, "Rider, contacts and notification preference are required.");
    return;
  }

  String rider = body["rider"].as<String>();
  rider.trim();

  if (!Domain::validName(rider.c_str())) {
    sendError(400,
              "Rider name: 1-40 English letters, numbers, spaces, apostrophes, "
              "dots or hyphens.");
    return;
  }

  JsonArray contacts = body["contacts"].as<JsonArray>();
  if ((!store.data.riderSetupComplete && contacts.size() < 1) || contacts.size() > Config::MAX_CONTACTS) {
    sendError(400, String("Add between one and ") +
                       String(Config::MAX_CONTACTS) + " emergency contacts.");
    return;
  }

  DeviceState next = store.data;
  const bool riderChanged = rider != next.rider;
  const bool notifyWasEnabled = next.notifyAdded;
  next.rider = rider;
  next.notifyAdded = body["notifyAdded"].as<bool>();
  const bool notifyTurnedOn = next.notifyAdded && !notifyWasEnabled;

  for (Contact& contact : next.contacts) {
    contact = Contact();
  }

  size_t index = 0;
  for (JsonObject contactJson : contacts) {
    String name = contactJson["name"] | "";
    const String rawPhone = contactJson["phone"] | "";
    name.trim();

    const String phone = Domain::normalizePhone(rawPhone.c_str()).c_str();

    if (!Domain::validName(name.c_str()) || !phone.length()) {
      sendError(400,
                "Every contact needs a name and a valid Philippine mobile "
                "number (09... or +639...).");
      return;
    }

    for (size_t previous = 0; previous < index; ++previous) {
      if (next.contacts[previous].phone == phone) {
        sendError(400, "A mobile number can only be added once.");
        return;
      }
    }

    next.contacts[index++] = {name, phone};
  }

  // Do not alter settings while a modem transmission is already in progress.
  for (const SmsJob& job : next.jobs) {
    if (job.state == "sending") {
      sendError(409,
                "A message is being sent. Apply changes after it finishes.");
      return;
    }
  }

  // Cancel queued jobs that no longer match the newly applied settings.
  for (SmsJob& job : next.jobs) {
    if (job.eventBoot || (job.state != "queued" && job.state != "retry")) {
      continue;
    }

    bool retained = false;
    for (const Contact& contact : next.contacts) {
      if (contact.phone == job.phone) {
        retained = true;
      }
    }

    if (!retained || riderChanged ||
        (job.kind == "addition" && !next.notifyAdded)) {
      job.state = "cancelled";
      job.detail = "Cancelled by settings change";
    }
  }

  int notificationsQueued = 0;

  if (next.notifyAdded && Config::ENABLE_SIM800L) {
    for (const Contact& contact : next.contacts) {
      if (!contact.phone.length()) {
        continue;
      }

      bool existed = false;
      for (const Contact& oldContact : store.data.contacts) {
        if (oldContact.phone == contact.phone) {
          existed = true;
        }
      }

      if (!existed || riderChanged || notifyTurnedOn) {
        const String message = Domain::invitationMessage(rider.c_str()).c_str();

        if (!StateStore::enqueueSms(next, contact.phone, message, "addition")) {
          sendError(409, "Message queue is full. Wait before applying.");
          return;
        }

        notificationsQueued++;
      }
    }
  }

  next.revision++;

  if (!store.commit(next)) {
    sendError(503, "Could not save settings. No new messages were queued.");
    return;
  }

  auto response=makeJson();
  response["ok"] = true;
  response["revision"] = next.revision;
  response["notificationsQueued"] = notificationsQueued;
  sendJson(response);
}

void DashboardServer::sendJobs() {
  auto document=makeJson();
  writeMessageQueue(document["messageQueue"].to<JsonObject>());
  JsonArray jobs = document["jobs"].to<JsonArray>();

  for (const SmsJob& job : store.data.jobs) {
    if (!job.id) continue;

    JsonObject item = jobs.add<JsonObject>();
    item["id"] = job.id;
    item["eventBoot"] = job.eventBoot;
    item["eventSequence"] = job.eventSequence;
    item["phone"] = job.phone;
    item["kind"] = job.kind;
    item["test"] = job.kind == "check" || (job.kind == "crash" &&
        (job.text.startsWith(String(Config::SMS_TEST_HEADER) + "\n") ||
         job.text.startsWith("TEST CRASH:") || job.text.startsWith("TEST:")));
    item["state"] = job.state;
    item["detail"] = job.detail;
    item["attempts"] = job.attempts;
    item["text"] = job.text;
    item["modemReference"] = job.modemReference;
    item["deliveryRequested"] = job.deliveryRequested;
    item["deliveryStatus"] = job.deliveryStatus;
    item["carrierReport"] = job.carrierReport;
  }

  sendJson(document);
}

void DashboardServer::queueContactCheck() { queueManualMessage("check"); }

void DashboardServer::queueIntroduction() { queueManualMessage("addition"); }

void DashboardServer::queueManualMessage(const String& kind) {
  if (!Config::ENABLE_SIM800L) {
    sendError(503, "SIM800L is disabled in Config.h.");
    return;
  }

  auto body=makeJson();
  if (!readJsonBody(body)) return;

  if (manualMessageSent && !Domain::elapsed(millis(), lastManualMessage,
                                            Config::MANUAL_SMS_COOLDOWN_MS)) {
    sendError(429, String("Wait ") +
                       String(Config::MANUAL_SMS_COOLDOWN_MS / 1000) +
                       " seconds before sending another manual message.");
    return;
  }

  const String phone = body["phone"] | "";
  bool savedContact = false;
  for (const Contact& contact : store.data.contacts) {
    if (contact.phone.length() && contact.phone == phone) {
      savedContact = true;
      break;
    }
  }

  if (!savedContact || !store.data.rider.length()) {
    sendError(400, "Save this contact first.");
    return;
  }

  for (const SmsJob& job : store.data.jobs) {
    const bool pending =
        job.state == "queued" || job.state == "retry" || job.state == "sending";

    if (job.phone == phone && pending) {
      sendError(409, "A message for this contact is already pending.");
      return;
    }
  }

  DeviceState next = store.data;
  const uint32_t jobId = next.nextJob;
  String message;
  bool locationAttached = false;
  bool locationFresh = false;
  uint32_t locationAgeMs = 0;

  if (kind == "addition") {
    message = Domain::invitationMessage(next.rider.c_str()).c_str();
  } else {
    // Snapshot location at queue time. Manual SMS has no incident/camera side
    // effects and keeps ordinary manual-message capacity/priority limits.
    double latitude = 0.0;
    double longitude = 0.0;
    if (neo6m.latestLocation(latitude, longitude, locationAgeMs,
                             locationFresh) &&
        locationAgeMs <= Config::GPS_LAST_LOCATION_MAX_MS) {
      locationAttached = true;
    }

    if (kind == "manual_crash") {
      message = Domain::crashMessage(next.rider.c_str(),
                    locationAttached ? Domain::smsLocation(latitude, longitude) : std::string(),
                    locationFresh).c_str();
    } else {
      message = Domain::crashTestMessage(next.rider.c_str(), latitude, longitude,
                                         locationAttached, locationFresh).c_str();
    }
  }

  if (!StateStore::enqueueSms(next, phone, message, kind)) {
    sendError(409, "Message queue full.");
    return;
  }

  if (!store.commit(next)) {
    sendError(503, "Storage unavailable.");
    return;
  }

  manualMessageSent = true;
  lastManualMessage = millis();

  auto response=makeJson();
  response["ok"] = true;
  response["kind"] = kind;
  response["jobId"] = jobId;
  response["state"] = "queued";
  response["networkRegistered"] = sim800l.registered();
  response["registration"] = sim800l.registrationText();
  if (kind == "check" || kind == "manual_crash") {
    response["locationAttached"] = locationAttached;
    response["locationFresh"] = locationFresh;
    response["locationAgeMs"] = locationAttached ? locationAgeMs : 0;
  }
  sendJson(response, 202);
}

void DashboardServer::handleJobAction() {
  auto body=makeJson();
  if (!readJsonBody(body)) return;

  const uint32_t id = body["id"] | 0U;
  const String action = body["action"] | "";
  DeviceState next = store.data;

  if (action == "clear_history") {
    size_t cleared = 0;
    for (SmsJob& job : next.jobs) {
      if (job.id && Domain::isTerminalJobState(job.state.c_str())) {
        job = SmsJob();
        ++cleared;
      }
    }
    if (cleared && !store.commit(next)) {
      sendError(503, store.lastError);
      return;
    }
    auto response=makeJson();
    response["ok"] = true;
    response["cleared"] = cleared;
    sendJson(response);
    return;
  }

  if(action=="cancel"){
    auto selection=makeJson();selection["id"]=id;
    cancelMessageSelection(selection.as<JsonObjectConst>());return;
  }
  if(action=="cancel_all"){
    auto selection=makeJson();selection["all"]=true;
    cancelMessageSelection(selection.as<JsonObjectConst>());return;
  }
  sendError(400,"Unknown message action.");
}

void DashboardServer::changePassword() {
  auto body=makeJson();
  if (!readJsonBody(body)) return;

  if (security.throttled()) {
    sendError(429, String("Wait ") + String(Config::LOGIN_THROTTLE_MS / 1000) +
                       " seconds.");
    return;
  }

  const String currentPassword = body["current"] | "";
  const String newPassword = body["password"] | "";
  const DeviceState account = store.data;
  releaseState();

  const bool currentPasswordOk =
      Security::verifyRiderPassword(currentPassword, account);

  if (!currentPasswordOk) {
    security.failures++;
    security.lastFailure = millis();
    sendError(403, "Current password is incorrect.");
    return;
  }

  if (!Security::validNewRiderPassword(newPassword)) {
    sendError(400, String("Use a private password of ") + Config::RIDER_MIN_PASSWORD_LENGTH + "-64 characters, different from either factory password.");
    return;
  }

  const String newSalt = Security::randomHex(16),
               newHash = Security::hashPassword(newPassword, newSalt);
  acquireState();
  DeviceState next = store.data;
  next.salt = newSalt;
  next.passwordHash = newHash;
  next.defaultPassword = false;
  ++next.revision;

  if (next.passwordHash.length() != 64 || !store.commit(next)) {
    sendError(503, "Could not save password.");
    return;
  }

  security.clearRiderSessions();
  web.sendHeader("Set-Cookie",
                 "SVSID=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");

  auto response=makeJson();
  response["ok"] = true;
  sendJson(response);
}

void DashboardServer::sendIntegrations() {
  auto document=makeJson();
  document["apiVersion"] = 1;
  document["frontKey"] = store.data.cameraKeys[0];
  document["rearKey"] = store.data.cameraKeys[1];
  document["cameraLockEnabled"] = true;
  document["preSeconds"] =
      Config::CAMERA_PRE_CLIPS * Config::CAMERA_SEGMENT_SECONDS;
  document["postSeconds"] =
      Config::CAMERA_POST_CLIPS * Config::CAMERA_SEGMENT_SECONDS;
  document["sensorLinkEnabled"] = Config::ENABLE_SENSOR_LINK;
  sendJson(document);
}

void DashboardServer::cameraHeartbeat() {
  const int cameraIndex=cameraIdentity();if(cameraIndex<0)return;
  auto body=makeJson();
  if (!readJsonBody(body)) return;

  const String firmware = body["firmware"] | "";
  if (firmware.length() > 32 || !body["recording"].is<bool>() ||
      !body["sdReady"].is<bool>()) {
    sendError(400, "Invalid camera status.");
    return;
  }

  if(!body["telemetry"].isNull() && (!body["telemetry"].is<JsonObject>() || !body["events"].is<JsonArray>() ||
      !CameraMonitor::validate(body["telemetry"].as<JsonObjectConst>(),body["events"].as<JsonArrayConst>()))){sendError(400,"Invalid bounded camera telemetry.");return;}
  CameraStatus& camera = cameras[cameraIndex];
  ++camera.heartbeats;
  camera.heartbeat(millis(),Config::CAMERA_OFFLINE_MS);
  camera.firmware = firmware;camera.address=web.client().remoteIP();
  camera.recording = body["recording"];
  camera.sdReady = body["sdReady"];
  camera.lockSupported = body["lockSupported"] | false;
  if(!body["telemetry"].is<JsonObject>())camera.telemetry.clear();
  if(body["telemetry"].is<JsonObject>()){
    auto t=body["telemetry"].as<JsonObjectConst>();camera.telemetry.clear();
    CameraMonitor::copyStatus(camera.telemetry.to<JsonObject>(),t);
    const uint32_t stream=t["stream"];
    if(camera.cursor.stream!=stream){camera.cursor.stream=stream;camera.cursor.seq=0;camera.cursor.missing=0;}
    for(JsonObjectConst e:body["events"].as<JsonArrayConst>()){
      if(camera.cursor.accept(stream,e["seq"]))cameraLogs.add(cameraIndex,stream,t["session"],millis(),e);
    }
  }


  observeDownload(cameraIndex);
  // Telemetry and events have been copied; do not retain the incoming tree
  // while allocating the heartbeat reply and Wi-Fi transmit buffers.
  body.clear();
  auto response=makeJson();
  response["ok"] = true;
  response["id"] = cameraIndex ? "rear" : "front";
  response["lockEnabled"] = true;
  response["logStream"]=camera.cursor.stream;response["logAck"]=camera.cursor.seq;
  writeClock(response["clock"].to<JsonObject>());
  sendJson(response);
}

void DashboardServer::writeClock(JsonObject out){
  const uint64_t now=esp_timer_get_time()/1000ULL;uint64_t gpsUtc=0;
  const bool fresh=neo6m.latestUtcMs(gpsUtc);
  if(fresh)namingClock.set(gpsUtc,now,1);
  const uint64_t value=namingClock.get(now);
  if(value)out["utcMs"]=value;else out["utcMs"]=nullptr;
  out["source"]=!value?"unsynced":namingClock.source==2?"device":fresh?"gps":"gps_holdover";
  out["offsetMinutes"]=480;out["validForMs"]=value?UtcClock::HOLD_MS-(now-namingClock.at):0;
}
void DashboardServer::sendCameraMonitor(){
  auto* session=authorize();if(!session)return;
  DiagnosticPaging::Query query;if(!query.read(web)){sendError(400,"Invalid diagnostic cursor.");return;}
  const bool summary=web.arg("view")=="summary";
  const bool events=web.arg("view")=="events";
  if(web.hasArg("view")&&!summary&&!events){sendError(400,"Unknown camera diagnostic view.");return;}
  ResponseMemory::Allocator memory(responseHealth,ResponseMemory::Endpoint::Cameras);JsonDocument d(&memory);d["schema"]=1;d["mainUptimeMs"]=millis();writeDownload(d["download"].to<JsonObject>(),session);
  d["statusFreshMs"]=Config::CAMERA_STATUS_FRESH_MS;
  d["offlineMs"]=Config::CAMERA_OFFLINE_MS;
  d["frameFreshAtReportMs"]=Config::CAMERA_FRAME_FRESH_AT_REPORT_MS;
  d["retention"]["segmentSeconds"]=Config::CAMERA_SEGMENT_SECONDS;
  d["retention"]["normalClips"]=Config::CAMERA_NORMAL_CLIPS;
  d["retention"]["preClips"]=Config::CAMERA_PRE_CLIPS;d["retention"]["postClips"]=Config::CAMERA_POST_CLIPS;
  d["logCapacity"]=CameraMonitor::Journal::CAPACITY;d["logOverwritten"]=cameraLogs.overwritten;
  writeClock(d["clock"].to<JsonObject>());
  auto a=d["cameras"].to<JsonArray>();
  const uint32_t cameraNow=millis();
  for(int i=0;!events&&i<2;++i){const auto& c=cameras[i];auto o=a.add<JsonObject>();
    const uint32_t reportAge=writeCameraSummary(o,c,i?"rear":"front",cameraNow);
    o["missingLogEvents"]=c.cursor.missing;
    if(c.telemetry.as<JsonObjectConst>()["schema"]==1){
      auto src=c.telemetry.as<JsonObjectConst>();auto telemetry=o["telemetry"].to<JsonObject>();CameraMonitor::copyStatus(telemetry,src);
      telemetry["reportAgeMs"]=c.present?reportAge:0;
      const uint64_t rawFrameAge=src["frameAgeMs"]|uint64_t(0);
      telemetry["effectiveFrameAgeMs"]=c.present?rawFrameAge+uint64_t(reportAge):rawFrameAge;
    }else o["telemetry"]=c.telemetry.as<JsonObjectConst>();
  }
  auto logs=d["logs"].to<JsonArray>();
  DiagnosticPaging::Window window(cameraLogs.total,cameraLogs.count,query,diagnosticEpoch);
  auto page=d["page"].to<JsonObject>();window.write(page,diagnosticEpoch);page["omitted"]=summary;
  if(summary){page["more"]=false;page["nextBefore"]=0;}
  for(size_t i=0;!summary&&i<window.count;++i){const auto& e=cameraLogs.newest(window.offset+i);auto o=logs.add<JsonObject>();
    o["serial"]=cameraLogs.total-uint32_t(window.offset+i);
    o["role"]=e.role?"rear":"front";o["stream"]=e.stream;o["session"]=e.session;o["seq"]=e.seq;
    o["receivedAtMainMs"]=e.received;o["cameraUptimeMs"]=e.at;o["type"]=JsonString(e.type,false);o["clip"]=JsonString(e.clip,false);
    o["detail"]=JsonString(e.detail,false);o["boot"]=e.boot;o["incident"]=e.incident;
  }
  sendJson(d);
}

void DashboardServer::refreshIncidentContext(){
  if(!emergency)return;
  Incidents::Evidence value;value.captured=1;
  const uint64_t now=esp_timer_get_time()/1000ULL;uint64_t gpsUtc=0;
  if(neo6m.latestUtcMs(gpsUtc))namingClock.set(gpsUtc,now,1);
  value.utcMs=namingClock.get(now);value.timeSource=value.utcMs?namingClock.source:0;
  const auto gps=neo6m.telemetry();value.gps=gps.decoding?2:1;
  value.modem=sim800l.online()?2:1;
  value.mobileService=sim800l.readyToSend()?4:!sim800l.simDetected()?1:!sim800l.simReady()?2:3;
  for(int c=0;c<2;++c)value.camera[c]=cameras[c].online(millis(),Config::CAMERA_OFFLINE_MS)?2:1;
  const auto power=battery.telemetry();value.battery=power.responding?2:1;
  if(power.valid){value.batteryPercent=int8_t(battery.batteryPercent());value.voltageMv=uint16_t(std::max(0.0f,std::min(65.535f,power.voltage))*1000.0f);}
  emergency->setEvidenceContext(value);
}
