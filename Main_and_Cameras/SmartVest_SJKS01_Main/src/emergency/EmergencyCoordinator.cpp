#include "EmergencyCoordinator.h"

#include <esp_system.h>

#include "../core/Domain.h"
#include "../gps/UtcClock.h"
#include <cmath>
#include <algorithm>

EmergencyCoordinator::Timing* EmergencyCoordinator::timing(const Incidents::Incident& e){
  const int slot=find(e.boot,e.sequence);
  if(slot<0 || timings[slot].boot!=e.boot || timings[slot].sequence!=e.sequence)return nullptr;
  return &timings[slot];
}
namespace {
void latencyJson(JsonObject out,const char* key,uint32_t value){
  if(value==UINT32_MAX)out[key]=nullptr;else out[key]=value;
}
}
bool EmergencyCoordinator::save() {
  if (!loaded) return false;
  uint8_t bytes[Incidents::ENCODED_SIZE];
  Incidents::encode(journal, bytes);
  writable =
      preferences.putBytes("journal", bytes, sizeof(bytes)) == sizeof(bytes);
  dirty = !writable;
  lastError = writable ? ""
                       : "Incident flash write failed; UART acknowledgement "
                         "withheld; SMS submission paused.";
  retryAt = millis() + 5000;
  return writable;
}
void EmergencyCoordinator::begin() {
  if (!preferences.begin("sv_incident", false)) {
    lastError = "Cannot open incident NVS";
    return;
  }
  const size_t size = preferences.getBytesLength("journal");
  if (size) {
    uint8_t bytes[Incidents::ENCODED_SIZE];
    if ((size != sizeof(bytes) && size != Incidents::LEGACY_ENCODED_SIZE) ||
        preferences.getBytes("journal", bytes, size) != size ||
        !Incidents::decode(bytes, journal, size)) {
      lastError =
          "Incident journal corrupt; retain NVS for diagnosis. No automatic "
          "replay/reset.";
      return;
    }
  }
  loaded = true;
  if(store.data.dataResetPending){finishDataReset();return;}
  // Old history is informational; the incident journal alone owns reboot
  // replay.
  for (auto& job : store.data.jobs)
    if (job.eventBoot) {
      job.eventBoot = 0;
      job.eventSequence = 0;
    }
  // Intent may have reached the carrier before power failed: never replay it.
  for (auto& e : journal.entries) {
    e.ageKnown = false;  // Uptime clocks restarted; do not invent a historical
                         // trigger time.
    e.detectedUptime = millis();  // A pending camera gets a new bounded retry
                                  // period after reboot.
    for (size_t i = 0; i < e.count; ++i) {
      auto& r = e.recipients[i];
      if (r.outcome == Incidents::Sending) r.outcome = Incidents::Unknown;
      r.jobId = 0;
    }
  }
  save();
}
int EmergencyCoordinator::find(uint32_t boot, uint32_t sequence) const {
  for (size_t i = 0; i < Incidents::LIMIT; ++i)
    if (journal.entries[i].boot == boot &&
        journal.entries[i].sequence == sequence)
      return int(i);
  return -1;
}
uint8_t EmergencyCoordinator::pendingAutomatic() const {
  uint8_t count=0;
  for(const auto& e:journal.entries)
    if(e.boot&&e.test&&(!Incidents::terminal(e)||e.camera[0]<2||e.camera[1]<2))++count;
  return count;
}
bool EmergencyCoordinator::canResetData() const {
  if(!loaded || !writable || !store.healthy || !store.writable)return false;
  for(const auto& e:journal.entries)if(e.boot&&(!Incidents::terminal(e)||e.camera[0]<2||e.camera[1]<2))return false;
  return true;
}
bool EmergencyCoordinator::canDownloadFrom(int camera) const {
  if(camera<0 || camera>1 || !loaded || !writable || !store.healthy || !store.writable || store.data.dataResetPending)return false;
  // Reading a saved clip does not reset incident data. SMS retries and the
  // other camera must not block it; this camera's unfinished coverage must.
  for(const auto& e:journal.entries)if(e.boot&&e.camera[camera]<2)return false;
  return true;
}
bool EmergencyCoordinator::finishDataReset() {
  if(!loaded || !store.data.dataResetPending)return false;
  // Keep event tombstones: clearing displayed history must never replay a crash.
  for(auto& e:journal.entries){
    if(e.boot){auto& n=journal.retiredNext;journal.retiredBoot[n]=e.boot;journal.retiredSequence[n]=e.sequence;n=(n+1)%Incidents::TOMBSTONES;}
    e=Incidents::Incident{};
  }
  if(!save())return false;
  for(auto& t:timings)t=Timing{};
  DeviceState next=store.data;next.dataResetPending=false;
  if(!store.commit(next)){retryAt=millis()+5000;return false;}
  return true;
}
bool EmergencyCoordinator::readyForCrash(bool automatic) const {
  if(store.data.dataResetPending)return false;
  if (!loaded || !writable || !store.healthy || !store.writable || (automatic && pendingAutomatic()>=Incidents::LIMIT-1)) return false;
  for (const auto& entry : journal.entries)
    if (!entry.boot ||
        (Incidents::terminal(entry) &&
         (entry.camera[0] >= 2 && entry.camera[1] >= 2)))
      return true;
  return false;
}

bool EmergencyCoordinator::accept(const CrashProtocol::Frame& frame,
                                  bool ageKnown, uint32_t ageMs, bool cameraOnly) {
  if(store.data.dataResetPending)return false;
  if (cameraOnly && frame.type != CrashProtocol::Type::Test) return false;
  if (!loaded || !frame.boot || !frame.sequence) return false;
  if (find(frame.boot, frame.sequence) >= 0) return !dirty && writable;
  for (size_t i = 0; i < Incidents::TOMBSTONES; ++i)
    if (journal.retiredBoot[i] == frame.boot &&
        journal.retiredSequence[i] == frame.sequence)
      return !dirty && writable;
  int slot = -1;
  if ((frame.type == CrashProtocol::Type::Test || frame.type == CrashProtocol::Type::AutomaticCrash) &&
      pendingAutomatic() >= Incidents::LIMIT - 1) {
    lastError =
        "Three earlier events are still pending; waiting for an incident "
        "slot to become available.";
    return false;
  }
  for (size_t i = 0; i < Incidents::LIMIT; ++i) {
    if (!journal.entries[i].boot) {
      slot = int(i);
      break;
    }
    if (Incidents::terminal(journal.entries[i]) &&
        (journal.entries[i].camera[0] >= 2 &&
         journal.entries[i].camera[1] >= 2))
      slot = int(i);
  }
  if (slot < 0) {
    lastError =
        "All four incident slots are pending; sensing retains/retries the "
        "event.";
    return false;
  }
  Incidents::Incident event;
  event.boot = frame.boot;
  event.sequence = frame.sequence;
  event.detectedUptime = millis();
  event.sensorUptime = frame.uptimeMs;
  event.ageKnown = ageKnown;
  event.ageAtReceipt = ageMs;
  event.evidence = context;
  auto& evidence=event.evidence;
  evidence.captured=1;
  evidence.motion=bool(frame.flags & CrashProtocol::ImuReady);
  evidence.accelerationMg=frame.accelerationMg;
  evidence.rotationDps=frame.rotationDps;
  evidence.piezo=(frame.flags & CrashProtocol::PiezoReady)?2:1;
  evidence.impactKnown=0;evidence.impactBanks=0;
  if(UtcClock::valid(evidence.utcMs) && ageKnown && ageMs<=UtcClock::HOLD_MS &&
     UtcClock::valid(evidence.utcMs-ageMs)){
    evidence.utcMs-=ageMs;evidence.timeEstimated=1;
  }else evidence.timeEstimated=0;
  // Keep the historical type-5 journal flag for quota/replay compatibility.
  // Automatic detections use standard alert wording; only manual type 4 is labelled.
  event.test = frame.type == CrashProtocol::Type::Test || frame.type == CrashProtocol::Type::AutomaticCrash;
  double lat = 0, lng = 0;
  uint32_t age = 0;
  bool fresh = false;
  std::string location;
  if (Config::ENABLE_NEO6M && gps.latestLocation(lat, lng, age, fresh) &&
      age <= Config::GPS_LAST_LOCATION_MAX_MS) {
    location = Domain::smsLocation(lat, lng);
    if(evidence.gps!=1){
      evidence.gps=fresh?3:4;evidence.latitudeE7=int32_t(std::lround(lat*10000000.0));
      evidence.longitudeE7=int32_t(std::lround(lng*10000000.0));evidence.locationAgeMs=age;
    }
  }
  String message = Domain::incidentMessage(store.data.rider.length()
                                               ? store.data.rider.c_str()
                                               : "Unconfigured rider",
                                           location, fresh, frame.type == CrashProtocol::Type::Test)
                       .c_str();
  if (message.length() > Config::SMS_MAX_LENGTH) {
    lastError = "Alert wording exceeds one SMS; shorten rider/config text.";
    return false;
  }
  message.toCharArray(event.message, sizeof(event.message));
  for (const auto& c : store.data.contacts)
    if (!cameraOnly && !Domain::normalizePhone(c.phone.c_str()).empty()) {
      auto& recipient=event.recipients[event.count++];
      c.phone.toCharArray(recipient.phone,14);
      size_t count=std::min(size_t(40),size_t(c.name.length()));
      while(count && (uint8_t(c.name.c_str()[count])&0xc0)==0x80)--count;
      std::memcpy(recipient.name,c.name.c_str(),count);recipient.name[count]=0;
    }
  if (journal.entries[slot].boot) {
    auto& next = journal.retiredNext;
    journal.retiredBoot[next] = journal.entries[slot].boot;
    journal.retiredSequence[next] = journal.entries[slot].sequence;
    next = uint8_t((next + 1) % Incidents::TOMBSTONES);
  }
  journal.entries[slot] = event;
  timings[slot]=Timing();timings[slot].boot=event.boot;timings[slot].sequence=event.sequence;
  timings[slot].receivedAt=event.detectedUptime;timings[slot].automaticFrame=frame.type==CrashProtocol::Type::AutomaticCrash;
  if (frame.type == CrashProtocol::Type::Crash || frame.type == CrashProtocol::Type::AutomaticCrash)
    crashLight.detected(event.boot, event.sequence, millis());
  // If storage fails keep RAM snapshot and withhold ACK until the entire
  // journal saves.
  return save();
}
uint8_t EmergencyCoordinator::outcome(const String& s) const {
  if (s == "sending") return Incidents::Sending;
  if (s == "accepted") return Incidents::Accepted;
  if (s == "unknown") return Incidents::Unknown;
  if (s == "failed") return Incidents::Failed;
  if (s == "cancelled") return Incidents::Cancelled;
  if (s == "delivered") return Incidents::Delivered;
  if (s == "undelivered") return Incidents::Undelivered;
  return Incidents::Waiting;
}
bool EmergencyCoordinator::submissionGuard(const SmsJob& job) {
  if (!job.eventBoot) return true;
  const int slot = find(job.eventBoot, job.eventSequence);
  if (slot < 0 || job.recipientIndex >= journal.entries[slot].count || dirty ||
      !writable)
    return false;
  auto& recipient = journal.entries[slot].recipients[job.recipientIndex];
  if (recipient.outcome != Incidents::Waiting) return false;
  recipient.outcome = Incidents::Sending;
  if (save()) {
    auto* t=timing(journal.entries[slot]);
    if(t && t->smsSubmitted[job.recipientIndex]==UINT32_MAX)t->smsSubmitted[job.recipientIndex]=uint32_t(millis()-t->receivedAt);
    return true;
  }
  // Intent was not committed: no modem submission. Retry saving a waiting
  // state.
  recipient.outcome = Incidents::Waiting;
  dirty = true;
  return false;
}
void EmergencyCoordinator::tick() {
  if (!loaded) return;
  const uint32_t now = millis();
  if(store.data.dataResetPending){if(int32_t(now-retryAt)>=0)finishDataReset();return;}
  if (dirty) {
    if (int32_t(now - retryAt) >= 0) save();
    return;
  }
  bool changed = false;
  for (auto& e : journal.entries) {
    if (!e.boot) continue;
    for (size_t i = 0; i < e.count; ++i) {
      auto& r = e.recipients[i];
      SmsJob* job = nullptr;
      for (auto& j : store.data.jobs)
        if (j.id && j.eventBoot == e.boot && j.eventSequence == e.sequence &&
            j.recipientIndex == i) {
          job = &j;
          break;
        }
      if (job) {
        // A committed cancellation owns this recipient even if an older job
        // snapshot is still in ordinary history. Never recreate that work.
        if(r.outcome==Incidents::Cancelled){
          if(job->state=="queued"||job->state=="retry"||job->state=="sending"){
            job->state="cancelled";job->detail="Cancelled from dashboard before submission.";job->changed=now;
          }
          continue;
        }
        const uint8_t actual = outcome(job->state);
        if (actual != r.outcome) {
          r.outcome = actual;
          if (actual == Incidents::Accepted || actual == Incidents::Delivered)
            crashLight.sent(e.boot, e.sequence, now);
          auto* t=timing(e);
          if(t && actual!=Incidents::Waiting && actual!=Incidents::Sending && t->smsResult[i]==UINT32_MAX)t->smsResult[i]=uint32_t(millis()-t->receivedAt);
          changed = true;
        }
        continue;
      }
      if (r.outcome != Incidents::Waiting || !store.healthy ||
          !Config::ENABLE_SIM800L)
        continue;
      if (!StateStore::enqueueSms(store.data, r.phone, e.message, "crash"))
        continue;
      for (auto& j : store.data.jobs)
        if (j.id == store.data.nextJob - 1) {
          j.eventBoot = e.boot;
          j.eventSequence = e.sequence;
          j.recipientIndex = uint8_t(i);
          r.jobId = j.id;
          auto* t=timing(e);if(t && t->smsQueued[i]==UINT32_MAX)t->smsQueued[i]=uint32_t(millis()-t->receivedAt);
          break;
        }
    }
  }
  if (changed) save();
}
void EmergencyCoordinator::writePendingTexts(JsonArray output, bool compact) const {
  for(const auto& e:journal.entries)if(e.boot)for(size_t i=0;i<e.count;++i){
    const auto& r=e.recipients[i];
    if(r.outcome!=Incidents::Waiting&&r.outcome!=Incidents::Sending)continue;
    if(compact){
      auto ref=output.add<JsonArray>();ref.add(e.boot);ref.add(e.sequence);ref.add(i);ref.add(r.outcome==Incidents::Waiting);
      // Older diagnostic recipients are absent from rider incident cards.
      if(std::strncmp(e.message,Config::SMS_TEST_HEADER,std::strlen(Config::SMS_TEST_HEADER))==0||std::strncmp(e.message,"TEST:",5)==0||std::strncmp(e.message,"TEST CRASH:",11)==0){ref.add(JsonString(r.name,false));ref.add(JsonString(r.phone,false));}
      continue;
    }
    auto item=output.add<JsonObject>();item["boot"]=e.boot;item["sequence"]=e.sequence;
    item["recipient"]=i;item["name"]=JsonString(r.name,false);item["phone"]=JsonString(r.phone,false);
    item["state"]=r.outcome==Incidents::Sending?"sending":"queued";
  }
}
uint32_t EmergencyCoordinator::pendingTextMask(uint32_t boot,uint32_t sequence,uint8_t recipient) const {
  const int slot=find(boot,sequence);
  if(slot<0||recipient>=journal.entries[slot].count)return 0;
  const auto state=journal.entries[slot].recipients[recipient].outcome;
  return state==Incidents::Waiting||state==Incidents::Sending?uint32_t(1)<<(size_t(slot)*Incidents::CONTACTS+recipient):0;
}
bool EmergencyCoordinator::cancelPendingTexts(uint32_t mask){
  if(!mask)return true;
  if(!loaded||!writable||dirty)return false;
  uint32_t changed=0,sending=0;
  for(size_t s=0;s<Incidents::LIMIT;++s)for(size_t i=0;i<journal.entries[s].count;++i){
    auto& r=journal.entries[s].recipients[i];const uint32_t bit=uint32_t(1)<<(s*Incidents::CONTACTS+i);
    if(!(mask&bit)||(r.outcome!=Incidents::Waiting&&r.outcome!=Incidents::Sending))continue;
    changed|=bit;if(r.outcome==Incidents::Sending)sending|=bit;r.outcome=Incidents::Cancelled;
  }
  if(!changed)return true;
  if(!save()){
    for(size_t s=0;s<Incidents::LIMIT;++s)for(size_t i=0;i<journal.entries[s].count;++i){
      const uint32_t bit=uint32_t(1)<<(s*Incidents::CONTACTS+i);
      if(changed&bit)journal.entries[s].recipients[i].outcome=(sending&bit)?Incidents::Sending:Incidents::Waiting;
    }
    return false;
  }
  for(auto& job:store.data.jobs){
    const int slot=job.eventBoot?find(job.eventBoot,job.eventSequence):-1;
    if(slot<0||job.recipientIndex>=Incidents::CONTACTS||!(changed&(uint32_t(1)<<(size_t(slot)*Incidents::CONTACTS+job.recipientIndex))))continue;
    job.state="cancelled";job.detail="Cancelled from dashboard before submission.";job.changed=millis();
  }
  return true;
}
void EmergencyCoordinator::cameraCommand(int camera, JsonObject out) {
  if (camera < 0 || camera > 1) return;
  out["enabled"] = true;
  for (uint8_t pass=0;pass<3;++pass) for (size_t offset=0;offset<Incidents::LIMIT;++offset) {
    const size_t slot=(cameraCursor[camera]+offset)%Incidents::LIMIT;
    const auto& e=journal.entries[slot];
    if (e.boot && e.camera[camera] == (pass==0?0:pass==1?1:3)) {
      cameraCursor[camera]=uint8_t((slot+1)%Incidents::LIMIT);
      JsonObject command = out["command"].to<JsonObject>();
      command["type"] = "LOCK";
      auto* t=timing(e);if(t && t->cameraOffered[camera]==UINT32_MAX)t->cameraOffered[camera]=uint32_t(millis()-t->receivedAt);
      command["boot"] = e.boot;
      command["sequence"] = e.sequence;
      command["test"] = e.test;
      command["triggerTimeKnown"] = e.ageKnown;
      command["sensorUptimeMs"] = e.sensorUptime;
      if (e.ageKnown)
        command["elapsedSinceDetectionMs"] =
            uint32_t(millis() - e.detectedUptime) + e.ageAtReceipt;
      else
        command["elapsedSinceDetectionMs"] = nullptr;
      command["preClips"] = Config::CAMERA_PRE_CLIPS;
      command["postClips"] = Config::CAMERA_POST_CLIPS;
      command["segmentSeconds"] = Config::CAMERA_SEGMENT_SECONDS;
      command["normalClips"] = Config::CAMERA_NORMAL_CLIPS;
      return;
    }
  }
  out["command"] = nullptr;
}
bool EmergencyCoordinator::cameraAck(int camera, uint32_t boot,
                                     uint32_t sequence, const String& stage) {
  const int slot = find(boot, sequence);
  if (slot < 0 || camera < 0 || camera > 1 || dirty) return false;
  const uint8_t status = stage == "received"    ? 1
                         : stage == "protected" ? 2
                         : stage == "failed"    ? 3
                                                : 0;
  if (!status || !loaded || !writable) return false;
  auto& current = journal.entries[slot].camera[camera];
  if (current==2 && status!=2) return false;
  if (current==3 && status!=2) return status==3;
  if (current==status) return true;
  current = status;
  const bool saved=save();
  if(saved){auto* t=timing(journal.entries[slot]);if(t){
    if(status==1 && t->cameraReceived[camera]==UINT32_MAX)t->cameraReceived[camera]=uint32_t(millis()-t->receivedAt);
    if(status==2 && t->cameraProtected[camera]==UINT32_MAX)t->cameraProtected[camera]=uint32_t(millis()-t->receivedAt);
  }}
  return saved;
}
void EmergencyCoordinator::observeImpact(uint32_t boot,uint32_t sensorAt,uint8_t banks){
  if(!loaded||!writable||dirty||!sensorAt||banks>3)return;
  for(auto& e:journal.entries){
    const int32_t delta=int32_t(sensorAt-e.sensorUptime);
    if(e.boot!=boot||delta< -25||delta>25||!e.evidence.captured)continue;
    const uint8_t combined=e.evidence.impactBanks|banks;
    if(e.evidence.impactKnown && combined==e.evidence.impactBanks)continue;
    e.evidence.impactKnown=1;e.evidence.impactBanks=combined;
    save();return;
  }
}
void EmergencyCoordinator::writeJson(JsonObject out, bool detailed) {
  out["automaticCrashDetectionEnabled"]=Config::ARM_CRASH_DETECTION;
  out["loaded"] = loaded;
  out["writable"] = writable;
  out["error"] = lastError;
  JsonArray list = out["incidents"].to<JsonArray>();
  const char* states[] = {"waiting", "sending",   "accepted",  "unknown",
                          "failed",  "cancelled", "delivered", "undelivered"};
  const char* cameras[] = {"pending", "received", "protected_reported",
                           "failed_or_timeout"};
  for (const auto& e : journal.entries)
    if (e.boot) {
      const bool diagnostic=std::strncmp(e.message,Config::SMS_TEST_HEADER,std::strlen(Config::SMS_TEST_HEADER))==0 ||
        std::strncmp(e.message,"TEST:",5)==0 || std::strncmp(e.message,"TEST CRASH:",11)==0;
      if(!detailed && diagnostic)continue;
      auto row = list.add<JsonObject>();
      row["boot"] = e.boot;
      row["sequence"] = e.sequence;
      row["test"] = e.test;
      row["diagnostic"] = diagnostic;
      const auto& v=e.evidence;
      if(v.utcMs)row["occurredAtUtcMs"]=v.utcMs;else row["occurredAtUtcMs"]=nullptr;
      row["timeSource"]=!v.utcMs?"unavailable":v.timeSource==1?"gps":"device";
      row["timeBasis"]=v.timeEstimated?"estimated_detection":"recorded";
      row["detailsRecorded"]=bool(v.captured);
      auto movement=row["movement"].to<JsonObject>();movement["recorded"]=bool(v.motion);
      if(v.motion){movement["accelerationG"]=v.accelerationMg/1000.0;movement["rotationDps"]=v.rotationDps;}
      auto location=row["location"].to<JsonObject>();
      const char* gpsStates[]={"not_recorded","not_detected","no_fix","current","last_known"};
      location["state"]=gpsStates[v.gps];
      if(v.gps>=3){location["latitude"]=v.latitudeE7/10000000.0;location["longitude"]=v.longitudeE7/10000000.0;location["ageMs"]=v.locationAgeMs;}
      const char* presence[]={"not_recorded","not_detected","detected"};
      auto mobile=row["mobile"].to<JsonObject>();mobile["state"]=presence[v.modem];
      const char* service[]={"not_recorded","sim_unavailable","sim_locked","not_registered","ready"};
      mobile["service"]=service[v.mobileService];
      auto power=row["battery"].to<JsonObject>();power["state"]=presence[v.battery];
      if(v.batteryPercent>=0)power["percent"]=v.batteryPercent;else power["percent"]=nullptr;
      if(v.voltageMv)power["voltage"]=v.voltageMv/1000.0;
      auto impact=row["impact"].to<JsonObject>();impact["state"]=v.piezo==2?"available":v.piezo==1?"unavailable":"not_recorded";
      impact["recorded"]=bool(v.impactKnown);impact["front"]=bool(v.impactBanks&1);impact["rear"]=bool(v.impactBanks&2);
      auto presenceAtAlert=row["cameraPresence"].to<JsonObject>();presenceAtAlert["front"]=presence[v.camera[0]];presenceAtAlert["rear"]=presence[v.camera[1]];
      if(detailed){
      row["sensorUptimeMs"] = e.sensorUptime;
      row["triggerTimeKnown"] = e.ageKnown;
      auto overdue=row["cameraOverdue"].to<JsonObject>();
      for(int c=0;c<2;++c)overdue[c?"rear":"front"]=e.ageKnown&&uint32_t(millis()-e.detectedUptime)>=Config::CAMERA_COMMAND_DEADLINE_MS&&e.camera[c]<2;
      auto latency=row["latency"].to<JsonObject>();auto* t=timing(e);
      latency["available"]=t!=nullptr;latency["scope"]="this Main boot; milliseconds from Main receipt";
      latencyJson(latency,"estimatedDetectionToMainMs",e.ageKnown?e.ageAtReceipt:UINT32_MAX);
      if(t){
        latency["automaticCrashFrame"]=t->automaticFrame;
        for(int c=0;c<2;++c){auto camera=latency[c?"rear":"front"].to<JsonObject>();
          latencyJson(camera,"firstOfferedMs",t->cameraOffered[c]);
          latencyJson(camera,"receivedAckMs",t->cameraReceived[c]);latencyJson(camera,"protectedAckMs",t->cameraProtected[c]);}
      }
      row["message"] = String(e.message);
      }
      row["front"] = cameras[e.camera[0]];
      row["rear"] = cameras[e.camera[1]];
      row["noRecipients"] = e.count == 0;
      auto recipients = row["recipients"].to<JsonArray>();
      for (size_t i = 0; i < e.count; ++i) {
        auto r = recipients.add<JsonObject>();
        r["phone"] = String(e.recipients[i].phone);
        r["name"] = String(e.recipients[i].name);
        r["state"] = states[e.recipients[i].outcome];
        auto* recipientTiming=detailed?timing(e):nullptr;
        if(recipientTiming){auto* t=recipientTiming;auto l=r["latency"].to<JsonObject>();latencyJson(l,"queuedMs",t->smsQueued[i]);
          latencyJson(l,"submissionIntentMs",t->smsSubmitted[i]);latencyJson(l,"firstResultMs",t->smsResult[i]);}
      }
    }
}
