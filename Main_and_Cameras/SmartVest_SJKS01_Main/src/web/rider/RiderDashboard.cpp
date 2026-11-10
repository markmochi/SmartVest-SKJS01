#include "../DashboardServer.h"
#include "../../core/Version.h"
#include "../../core/Domain.h"

namespace {
bool riderCameraFrameFresh(const JsonDocument& telemetry) {
  const JsonObjectConst t=telemetry.as<JsonObjectConst>();
  return t["schema"]==1 && t["currentFrames"].is<uint64_t>() &&
         t["currentFrames"].as<uint64_t>()>0 && t["frameAgeMs"].is<uint64_t>() &&
         t["frameAgeMs"].as<uint64_t>()<=Config::CAMERA_FRAME_FRESH_AT_REPORT_MS;
}
}

// Rider routes expose a deliberately small view of hardware state.
// Authentication/HTTP transport are shared; diagnostics remain developer-only.
void DashboardServer::registerRiderRoutes() {
  bindRoute("/api/v1/rider/status",HTTP_GET,&DashboardServer::sendRiderStatus,Access::Role::Rider);
  bindRoute("/api/v1/rider/settings",HTTP_GET,&DashboardServer::sendSettings,Access::Role::Rider);
  bindRoute("/api/v1/rider/settings",HTTP_POST,&DashboardServer::saveSettings,Access::Role::Rider);
  web.on("/api/v1/rider/setup", HTTP_POST, [this]() {
    auto* s=authorize(true,Access::Role::Rider);
    if(!s)return;
    if(s->role!=Access::Role::Rider){sendError(403,"Use the rider dashboard.");return;}
    completeRiderSetup();
  });
  const auto account=[this](bool write,Access::Role role){
    auto* session=authorize(write,role);if(!session)return;
    if(session->role!=role){sendError(403,"Use your own account page.");return;}
    if(write)changeRiderAccount(role==Access::Role::Developer);
    else sendRiderAccount(role==Access::Role::Developer);
  };
  web.on("/api/v1/rider/account",HTTP_GET,[account](){account(false,Access::Role::Rider);});
  web.on("/api/v1/rider/account",HTTP_POST,[account](){account(true,Access::Role::Rider);});
  web.on("/api/v1/admin/account",HTTP_GET,[account](){account(false,Access::Role::Developer);});
  web.on("/api/v1/admin/account",HTTP_POST,[account](){account(true,Access::Role::Developer);});
  bindRoute("/api/v1/admin/reset-data",HTTP_POST,&DashboardServer::resetData);
}

void DashboardServer::sendRiderStatus() {
  ResponseMemory::Allocator memory(responseHealth,ResponseMemory::Endpoint::Rider);JsonDocument doc(&memory);
  const String view=web.arg("view");
  const bool full=!view.length(),overview=view=="overview",equipment=view=="equipment",activity=view=="activity";
  if(!full&&!overview&&!equipment&&!activity){doc["error"]="Unknown rider status view";sendJson(doc,400);return;}
  doc["product"] = Config::PRODUCT_NAME;
  doc["uptimeSeconds"] = millis()/1000;
  doc["rider"] = store.data.rider;
  doc["connectionMode"] = network.modeName();
  bool hasContacts = false;
  for (const auto& contact : store.data.contacts)
    hasContacts |= contact.phone.length() > 0;
  const bool storageReady =
      store.healthy && emergency && emergency->readyForCrash();
  const bool monitoring = sensorLink && sensorLink->armedAndHealthy();
  const bool textsReady = sim800l.readyToSend();
  doc["alertsReady"] = hasContacts && storageReady && monitoring && textsReady;
  doc["contactsConfigured"] = hasContacts;
  if(full||overview){
  auto contactList = doc["contacts"].to<JsonArray>();
  for (const auto& contact : store.data.contacts) {
    if (!contact.phone.length()) continue;
    auto item = contactList.add<JsonObject>();
    item["name"] = contact.name;
    item["phone"] = contact.phone;
  }
  }
  doc["settingsAvailable"] = store.healthy && store.writable;
  doc["settingsRevision"] = store.data.revision;
  auto crash = doc["monitoring"].to<JsonObject>();
  doc["storageAvailable"] = storageReady;
  battery.writeRiderBattery(doc["battery"].to<JsonObject>());
  if(!activity){
  crash["available"] = monitoring && storageReady;
  crash["label"] = monitoring && storageReady ? "Monitoring active"
                                              : "Monitoring unavailable";
  crash["help"] = monitoring && storageReady
                      ? "Your vest is monitoring for a possible crash."
                      : "Crash monitoring is unavailable. Have the vest checked before riding.";
  auto cellular = doc["cellular"].to<JsonObject>();
  if(full||equipment){
  cellular["enabled"] = Config::ENABLE_SIM800L;
  cellular["modemOnline"] = sim800l.online();
  cellular["simDetected"] = sim800l.simDetected();
  cellular["simReady"] = sim800l.simReady();
  cellular["simStatus"] = sim800l.simStatusText();
  cellular["simDetail"] = sim800l.simStatusDetail();
  cellular["registered"] = sim800l.registered();
  cellular["registration"] = sim800l.registrationText();
  cellular["carrier"] = sim800l.registered() ? sim800l.operatorName : String("Not registered");
  cellular["carrierCode"] = sim800l.registered() ? sim800l.operatorCode : String();
  cellular["simCarrier"] = sim800l.simReady() ? sim800l.simCarrierName : String("Unavailable");
  cellular["simCarrierCode"] = sim800l.simReady() ? sim800l.simCarrierCode : String();
  cellular["phoneNumber"] = sim800l.simReady() ? sim800l.subscriberNumber : String();
  cellular["phoneNumberStatus"] = sim800l.simReady() ? sim800l.subscriberNumberStatus : String("Unavailable");
  cellular["signalCsq"] = sim800l.signalCurrent() ? sim800l.csq : -1;
  cellular["available"] = textsReady;
  cellular["label"] =
      textsReady ? "Text alerts available" : "Text alerts unavailable";
  cellular["help"] = textsReady
                         ? "Your vest can send alerts to your saved contacts."
                         : "Alerts may be delayed. Check cellular coverage and your vest.";
  }
  cellular["available"] = textsReady;

  // Read the three rider indicators directly; a full GPS diagnostic tree is unnecessary here.
  const auto position=neo6m.telemetry();
  double lat = 0, lng = 0;
  uint32_t age = 0;
  bool fresh = false;
  const bool location = Config::ENABLE_NEO6M &&
                        neo6m.latestLocation(lat, lng, age, fresh) &&
                        age <= Config::GPS_LAST_LOCATION_MAX_MS;
  auto gps = doc["location"].to<JsonObject>();
  gps["enabled"] = Config::ENABLE_NEO6M;
  gps["receiving"] = position.receiving;
  gps["decoding"] = position.decoding;
  gps["state"] = location ? (fresh ? "current" : "last_known") : "unavailable";
  gps["label"] =
      location ? (fresh ? "Location available" : "Last location available")
               : "Location unavailable";
  gps["help"] =
      location ? (fresh ? "Your latest location can be included in an alert."
                        : "Alerts can include your last known location. A "
                          "current position isn't available.")
               : "Move outdoors for a clearer sky view. Alerts identify an unavailable location.";
  gps["ageMs"] = nullptr;
  if (location) {
    gps["ageMs"] = age;
    gps["latitude"] = lat;
    gps["longitude"] = lng;
  } else {
    gps["latitude"] = nullptr;
    gps["longitude"] = nullptr;
  }
  // Do not label HDOP as accuracy in metres.
  if (location && fresh && neo6m.weakGeometry())
    gps["help"] =
        "Location may be less precise. Move outdoors if possible.";

  if(full||equipment){
  auto cameraList = doc["cameras"].to<JsonArray>();
  const uint32_t cameraNow=millis();
  for (int i = 0; i < 2; ++i) {
    const bool connected =
        cameras[i].online(cameraNow,Config::CAMERA_OFFLINE_MS);
    const uint32_t reportAge=cameras[i].present?uint32_t(cameraNow-cameras[i].seen):UINT32_MAX;
    const bool statusFresh=connected && reportAge<=Config::CAMERA_STATUS_FRESH_MS;
    const bool recording =
        statusFresh && cameras[i].recording && cameras[i].sdReady &&
        riderCameraFrameFresh(cameras[i].telemetry);
    auto camera = cameraList.add<JsonObject>();
    camera["name"] = i ? "Rear camera" : "Front camera";
    camera["available"] = recording;
    const unsigned transfer=cameras[i].telemetry["downloadState"]|0U;
    camera["downloadActive"]=statusFresh&&(transfer==1||transfer==2);
    camera["statusFresh"] = statusFresh;
    camera["heartbeatAgeMs"] = nullptr;
    if(cameras[i].present)camera["heartbeatAgeMs"] = reportAge;
    camera["connectionState"] = cameras[i].state(cameraNow,Config::CAMERA_OFFLINE_MS);
    camera["label"] = recording   ? "Recording"
                      : connected && !statusFresh ? "Status delayed"
                      : connected && !cameras[i].sdReady ? "SD unavailable"
                      : connected ? "Recording unavailable"
                      : !cameras[i].present ? "Waiting for camera"
                                  : "Connection lost";
    camera["help"] = recording ? "Fresh camera status confirms recording, SD readiness and recent frames."
                      : connected && !statusFresh ? "Camera connected; status is stale. Recording and SD state are unconfirmed."
                      : connected ? "Recording is unconfirmed. Check the SD card and camera diagnostics."
                      : "Waiting for camera status. Cameras retry automatically; check power and WiFi if this continues.";
  }
  }
  }
  if(full||activity){
    writeMessageQueue(doc["messageQueue"].to<JsonObject>(),false);
    auto records=doc["activity"].to<JsonObject>();
    if(emergency)emergency->writeJson(records,false);
    else{records["loaded"]=false;records["writable"]=false;records["incidents"].to<JsonArray>();}
  }
  sendJson(doc);
}

void DashboardServer::sendRiderAccount(bool administrator) {
  auto doc=makeJson();
  doc["username"] = administrator ? store.data.adminUsername : store.data.riderUsername;
  doc["resetPending"] = store.data.dataResetPending;
  doc["defaultPassword"] = administrator ? !store.data.adminHash.length() : store.data.defaultPassword;
  doc["minPasswordLength"] = Config::RIDER_MIN_PASSWORD_LENGTH;
  sendJson(doc);
}

void DashboardServer::changeRiderAccount(bool administrator) {
  if(store.data.dataResetPending){sendError(409,"Data reset is still finishing.");return;}
  auto body=makeJson();
  if (!readJsonBody(body)) return;
  if (security.throttled()) {
    sendError(429, "Please wait a minute before trying again.");
    return;
  }
  if (!body["username"].is<String>() || !body["current"].is<String>() ||
      !body["password"].is<String>()) {
    sendError(400, "Username and password fields are required.");
    return;
  }
  String username = body["username"] | "";
  username.trim();
  const String current = body["current"] | "", password = body["password"] | "";
  if (!Access::validRiderUsername(username.c_str(),administrator) || username==(administrator?store.data.riderUsername:store.data.adminUsername) || (administrator&&username==Config::DEFAULT_RIDER_USERNAME)) {
    sendError(400,
              "Use 3-32 letters, numbers, dots, hyphens or underscores for "
              "your username.");
    return;
  }
  if (password.length() && !Security::validNewRiderPassword(password)) {
    sendError(400, String("Choose a private password of ") + Config::RIDER_MIN_PASSWORD_LENGTH + "-64 characters, different from either factory password.");
    return;
  }
  if (username == (administrator?store.data.adminUsername:store.data.riderUsername) && !password.length()) {
    sendError(400, "Enter a new username or password first.");
    return;
  }
  const RiderCredentials account = store.data;
  const uint32_t revision=store.data.revision;
  const String salt = administrator?account.adminSalt:account.salt, hash = administrator?account.adminHash:account.passwordHash;
  releaseState();
  if (!(administrator?Security::verifyAdminPassword(current,account):Security::verifyRiderPassword(current, account))) {
    ++security.failures;
    security.lastFailure = millis();
    sendError(403, "Your current password isn't correct.");
    return;
  }
  const bool rehash=password.length() || !hash.length();
  const String nextSalt = rehash ? Security::randomHex(16) : salt;
  const String nextHash =
      rehash ? Security::hashPassword(password.length()?password:current, nextSalt) : hash;
  if (nextHash.length() != 64) {
    sendError(503, "Your sign-in details couldn't be saved. Please try again.");
    return;
  }
  acquireState();
  if(revision!=store.data.revision){sendError(409,"Account settings changed. Refresh and try again.");return;}
  DeviceState next = store.data;
  if(administrator){next.adminUsername=username;next.adminSalt=nextSalt;next.adminHash=nextHash;}
  else {next.riderUsername=username;next.salt=nextSalt;next.passwordHash=nextHash;if(password.length())next.defaultPassword=false;}
  ++next.revision;
  if (!store.commit(next)) {
    sendError(503,
              "Your sign-in details couldn't be saved. Your previous details "
              "still work.");
    return;
  }
  if(administrator){for(auto& s:security.sessions)if(s.role==Access::Role::Developer)s=Security::Session();}
  else security.clearRiderSessions();
  web.sendHeader("Set-Cookie",
                 "SVSID=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
  auto response=makeJson();
  response["ok"] = true;
  sendJson(response);
}

void DashboardServer::resetData(){
  auto body=makeJson();if(!readJsonBody(body))return;
  if(security.throttled()){sendError(429,"Please wait before trying again.");return;}
  if(!body["current"].is<String>() || body["current"].as<String>().length()>64 || body["confirm"]!="RESET"){
    sendError(400,"Enter your administrator password and RESET.");return;
  }
  const RiderCredentials credentials=store.data;const uint32_t revision=store.data.revision;
  releaseState();
  if(!Security::verifyAdminPassword(body["current"].as<String>(),credentials)){
    ++security.failures;security.lastFailure=millis();sendError(403,"Your current password isn't correct.");return;
  }
  const String salt=Security::randomHex(16),hash=Security::hashPassword(Config::DEFAULT_RIDER_PASSWORD,salt);
  acquireState();
  if(revision!=store.data.revision || !emergency || !emergency->canResetData() || download.active() || sim800l.transactionActive()){
    sendError(409,"Finish downloads and pending alerts, then retry.");return;
  }
  for(const auto& job:store.data.jobs)if(job.id&&!Domain::isTerminalJobState(job.state.c_str())){
    sendError(409,"Finish or cancel pending messages before resetting data.");return;
  }
  if(hash.length()!=64){sendError(503,"Cannot prepare reset credentials.");return;}
  DeviceState next;
  next.adminUsername=credentials.adminUsername;next.adminSalt=credentials.adminSalt;next.adminHash=credentials.adminHash;
  next.salt=salt;next.passwordHash=hash;next.riderUsername=Config::DEFAULT_RIDER_USERNAME;
  next.riderAccountResetRevision=Config::RIDER_ACCOUNT_RESET_REVISION;
  next.revision=revision+1;next.nextJob=store.data.nextJob;next.dataResetPending=true;
  if(!store.commit(next)){sendError(503,"Data reset could not be saved. Check saved data before retrying.");return;}
  security.clearRiderSessions();
  cameraLogs.head=cameraLogs.count=0;cameraLogs.overwritten=cameraLogs.total=0;
  for(auto& row:cameraLogs.rows)row=CameraMonitor::Record{};
  if(++diagnosticEpoch==0)++diagnosticEpoch;
  const bool complete=emergency->finishDataReset();
  auto result=makeJson();result["ok"]=complete;result["resetPending"]=!complete;
  result["riderUsername"]=Config::DEFAULT_RIDER_USERNAME;sendJson(result,complete?200:202);
}

void DashboardServer::completeRiderSetup(){
  auto body=makeJson();if(!readJsonBody(body))return;
  if(store.data.dataResetPending || !body["revision"].is<uint32_t>() || body["revision"].as<uint32_t>()!=store.data.revision){sendError(409,"Saved details changed. Review your contacts and try again.");return;}
  bool contacts=false;for(const auto& c:store.data.contacts)contacts|=c.phone.length()>0;
  if(!contacts){sendError(400,"Add an emergency contact before finishing setup.");return;}
  if(!store.data.riderSetupComplete){DeviceState next=store.data;next.riderSetupComplete=true;++next.revision;if(!store.commit(next)){sendError(503,"Setup could not be saved. Please try again.");return;}}
  auto result=makeJson();result["ok"]=true;sendJson(result);
}
