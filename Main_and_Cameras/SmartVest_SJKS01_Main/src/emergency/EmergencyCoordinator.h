#pragma once
#include "../gps/NEO6MService.h"
#include "../storage/StateStore.h"
#include "IncidentJournal.h"
#include "../power/CrashLedSignal.h"
static_assert(Config::MAX_CONTACTS == Incidents::CONTACTS,
              "Incident schema supports exactly five contacts");
static_assert(Config::SMS_MAX_LENGTH <= 160,
              "Incident schema stores one 160-character SMS");
class EmergencyCoordinator {
 public:
  EmergencyCoordinator(StateStore& s, NEO6MService& g) : store(s), gps(g) {}
  void begin();
  void tick();
  bool accept(const CrashProtocol::Frame& frame, bool ageKnown = false,
              uint32_t ageMs = 0, bool cameraOnly = false);
  bool submissionGuard(const SmsJob& job);
  void writeJson(JsonObject output, bool detailed = true);
  void setEvidenceContext(const Incidents::Evidence& value) { context = value; }
  void observeImpact(uint32_t boot, uint32_t sensorAt, uint8_t banks);
  void writePendingTexts(JsonArray output, bool compact = false) const;
  uint32_t pendingTextMask(uint32_t boot, uint32_t sequence, uint8_t recipient) const;
  bool cancelPendingTexts(uint32_t mask); // Caller first excludes modem submissions already handed off.
  void cameraCommand(int camera, JsonObject output);
  bool cameraAck(int camera, uint32_t boot, uint32_t sequence,
                 const String& stage);
  bool readyForCrash(bool automatic = true) const;
  bool canResetData() const;
  bool canDownloadFrom(int camera) const;
  bool finishDataReset();
  bool isLoaded() const { return loaded; }
  bool writable = false;
  String lastError;
  CrashLedSignal crashLight;

 private:
  StateStore& store;
  NEO6MService& gps;
  struct Timing {
    uint32_t boot=0,sequence=0,receivedAt=0;
    bool automaticFrame=false;
    uint32_t cameraOffered[2]={UINT32_MAX,UINT32_MAX};
    uint32_t cameraReceived[2]={UINT32_MAX,UINT32_MAX};
    uint32_t cameraProtected[2]={UINT32_MAX,UINT32_MAX};
    uint32_t smsQueued[5]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    uint32_t smsSubmitted[5]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    uint32_t smsResult[5]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
  } timings[Incidents::LIMIT];
  Timing* timing(const Incidents::Incident& e);
  Preferences preferences;
  Incidents::Journal journal;
  Incidents::Evidence context;
  uint8_t cameraCursor[2]={0,0};
  bool loaded = false, dirty = false;
  uint32_t retryAt = 0;
  bool save();
  uint8_t pendingAutomatic() const;
  int find(uint32_t boot, uint32_t sequence) const;
  uint8_t outcome(const String& state) const;
};
