#pragma once

#include <HardwareSerial.h>
#include "../diagnostics/UartHealth.h"

#include "../storage/StateStore.h"
#include "SmsCodec.h"

/*
  SIM800L module
  --------------
  Owns UART2 and all AT-command traffic.

  Modem waits are cooperative: GPS, dashboard and other modules continue
  to run while the modem is registering or sending an SMS.  A queued SMS gets
  a fresh network-registration check immediately before AT+CMGS, and a send is
  only marked submitted after BOTH +CMGS and the final OK are received.
  Handset delivery requires a separately matched carrier status report.
*/
class SIM800LService {
 public:
  UartHealth uartHealth;
  explicit SIM800LService(StateStore& stateStore)
      : store(stateStore), uart(2) {}

  void begin();
  void tick();
  bool canCancel(uint32_t jobId) const;
  void cancellationCommitted(uint32_t jobId);
  using SubmissionGuard = bool (*)(void*, const SmsJob&);
  void setSubmissionGuard(SubmissionGuard callback, void* context) {
    guard = callback;
    guardContext = context;
  }

  bool online() const;
  bool registered() const;
  bool simReady() const;
  bool simDetected() const;
  bool registrationCurrent() const;
  bool signalCurrent() const;
  uint32_t simAgeMs() const { return simObserved ? uint32_t(millis()-simAt) : UINT32_MAX; }
  uint32_t registrationAgeMs() const { return registrationObserved ? uint32_t(millis()-registrationAt) : UINT32_MAX; }
  uint32_t signalAgeMs() const { return signalObserved ? uint32_t(millis()-signalAt) : UINT32_MAX; }
  String simStatusText() const;
  String simStatusDetail() const;
  bool transactionActive() const;
  bool configured() const { return smsConfigured; }
  bool recovering() const;
  bool readyToSend() const {
    return configured() && !recovering() && simReady() && registered();
  }
  String registrationText() const;
  const char* stage() const;
  bool commandPending() const { return waiting; }
  uint32_t commandAgeMs() const {
    return waiting ? uint32_t(millis() - sentAt) : 0;
  }
  uint32_t commandRemainingMs() const {
    const uint32_t age = commandAgeMs();
    return waiting && age < timeout ? timeout - age : 0;
  }
  uint32_t rxAgeMs() const {
    return rxBytes ? uint32_t(millis() - lastRxAt) : UINT32_MAX;
  }

  uint32_t rxBytes = 0, txBytes = 0, receivedLines = 0, commandsSent = 0, repliesReceived = 0;
  uint32_t txAgeMs() const { return txBytes ? uint32_t(millis()-lastTxAt) : UINT32_MAX; }
  int csq = 99;
  int registration = -1;
  String sim = "Unknown";
  String operatorName = "Not queried";
  String operatorCode;
  int accessTechnology = -1;
  String simCarrierName = "Not queried";
  String simCarrierCode;
  String subscriberNumber;
  String subscriberNumberStatus = "Not queried";
  String iccid;
  String iccidStatus = "Not queried";
  String lastError;
  uint32_t baud = 0;
  String lastRegistrationResponse;
  String recoveryStatus = "Starting modem";
  uint32_t maxServiceGapMs = 0;
  uint32_t commandTimeouts = 0;
  uint32_t synchronizationRecoveries = 0;
  uint32_t unexpectedPrompts = 0;
  uint32_t modemRestarts = 0;
  uint32_t smsReadyEvents = 0;
  String lastCommand;
  String lastErrorCommand;
  String lastSmsError;
  String lastSmsErrorCommand;
  String lastPowerEvent;
  String lastModemLine;
  String serviceCenter = "Not queried";
  bool deliveryReportsConfigured = false;
  bool outgoingSmsSupported = false;
  int smsServiceMode = -1;
  uint32_t deliveryReportsReceived = 0;
  uint32_t deliveryReportsMatched = 0;
  uint32_t deliveryReportsRejected = 0;
  String lastDeliveryReport;
  // Fixed storage: no allocation, flash or network output on the UART path.
  static constexpr size_t TRACE_CAPACITY = 48;
  struct TraceEntry {
    uint32_t at = 0, jobId = 0;
    char direction = ' ';
    bool truncated = false;
    char text[160] = {};
  };
  static constexpr size_t FAILURE_TRACE_CAPACITY = 16;
  struct FailureSnapshot {
    uint32_t revision=0;
    bool present = false, bodySent = false, gotReference = false;
    uint32_t at = 0, jobId = 0, commandElapsedMs = 0, rxAgeMs = 0;
    int modemReference = -1, signalCsq = 99;
    char stage[64] = {}, command[96] = {}, reason[160] = {};
    TraceEntry entries[FAILURE_TRACE_CAPACITY];
    size_t count = 0;
  };
  struct TraceSnapshot {
    TraceEntry entries[TRACE_CAPACITY];
    size_t count = 0, next = 0;
    uint32_t total = 0;
    // Successes and routine polling cannot erase the last failed transaction.
    FailureSnapshot lastFailure;
  };
  void copyTrace(TraceSnapshot& out) const { out = traceData; }
  // Only inspect while holding the hardware-service mutex; copy JSON strings
  // before releasing it. Avoid allocating/copying the entire trace per request.
  const TraceSnapshot& traceView() const {return traceData;}
  uint32_t responseAgeMs() const {
    return goodReplySeen ? uint32_t(millis() - lastGood) : UINT32_MAX;
  }

 private:
  enum class Phase {
    ProbeQuiet,
    Probe,
    SyncQuiet,
    SyncAT,
    SyncIdentity,
    Init,
    AwaitSim,
    SimRadio,
    EnableSimRadio,
    Idle,
    Poll,
    Identity,
    Recovery,
    PreflightSim,
    PreflightRegistration,
    SmsSetup,
    Prompt,
    Result,
    Cooldown
  };

  StateStore& store;
  HardwareSerial uart;
  Phase phase = Phase::Probe;

  bool goodReplySeen = false;
  uint32_t lastTxAt = 0;
  String line;
  TraceSnapshot traceData;
  void trace(char direction, const char* text);
  void captureFailure(const SmsJob& job, const String& detail);
  bool reinitializeAfterReady = false;
  bool simSetupPending = false;
  uint32_t lastSimRadioAt = 0;
  // An MR is only 8 bits. Remember reuse even after history is cleared or
  // the modem reboots; ambiguous references cannot assert delivery this boot.
  uint8_t usedReferences[32] = {};
  uint8_t ambiguousReferences[32] = {};
  bool droppingLine = false;
  SubmissionGuard guard = nullptr;
  void* guardContext = nullptr;
  uint32_t pendingId = 0;

  bool waiting = false;
  bool gotOK = false;
  bool gotError = false;
  bool gotPrompt = false;
  bool gotReference = false;
  bool gotSimStatus = false;
  bool gotRegistration = false;
  bool gotSignal = false;
  bool gotIdentity = false;
  bool gotSmsService = false;
  bool gotIdentityValue = false;
  bool unexpectedPrompt = false;
  bool smsConfigured = false;
  bool simObserved = false;
  bool registrationObserved = false;
  bool signalObserved = false;
  uint32_t simAt = 0, signalAt = 0, lastSimQueryAt = 0, lastIccidQueryAt = 0;
  void clearSimIdentity();
  void observeSim(const String& state);
  int commandCmsError = -1;
  uint8_t syncAttempt = 0;

  uint32_t sentAt = 0;
  uint32_t timeout = 0;
  uint32_t nextAt = 0;
  uint32_t lastGood = 0;
  uint32_t lastRxAt = 0;
  uint32_t lastSubmissionAt = 0;

  int rateIndex = 0;
  int knownRateIndex = 0;
  bool historyDirty = false;
  uint32_t historyRetryAt = 0;
  int reportPduLength = -1;
  uint32_t reportPduAt = 0;
  void parseDeliveryReport(const String& text);
  void applyDeliveryReport(const SmsCodec::Report& report);
  int probeTry = 0;
  int initStep = 0;
  int pollStep = 0;
  int activeJob = -1;
  int pendingJob = -1;
  int missedResponses = 0;
  int recoveryStep = 0;
  enum class IdentityQuery { None, Iccid, Imsi, Operator, SubscriberNumber };
  IdentityQuery identityQuery = IdentityQuery::None;
  bool iccidQueried = false, imsiQueried = false, operatorQueried = false,
       subscriberNumberQueried = false;
  uint32_t operatorQueriedAt = 0;
  int radioMode = -1;
  int operatorMode = -1;
  uint32_t lastRecovery = 0;
  uint32_t lastTick = 0;
  uint32_t lastScheduledJob = 0;
  uint32_t registrationAt = 0;
  bool registrationWake = false;
  void startRecovery();
  void advanceRecovery(bool success);
  void preflightFailed();

  void sendCommand(const String& command, uint32_t waitMs = 2500);
  void consumeUart();
  void parseLine(const String& text);
  void parseRegistration(const String& text);

  void finishActiveJob(const String& state, const String& detail);
  void setWaitingDetail(int jobIndex, const String& detail);
  void restartProbe();
  void startSynchronization(const String& reason);
  void escapeToSync();
  void synchronizationFailed();
  void beginInitialization();
  void beginRegistrationPreflight(int jobIndex);
  void beginSmsSend(int jobIndex, uint32_t now);
  void startSmsPrompt();
  int selectPendingJob(uint32_t now);
};
