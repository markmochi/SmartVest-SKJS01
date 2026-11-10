#include "SIM800LService.h"

#include "../../Config.h"
#include "../core/Domain.h"
#include "SimIdentity.h"
#include <cstring>
#include <cstdlib>
#include "../diagnostics/Console.h"

namespace {
const uint32_t kProbeBaudRates[] = {9600, 115200, 57600, 38400, 19200};
constexpr size_t kProbeBaudCount =
    sizeof(kProbeBaudRates) / sizeof(kProbeBaudRates[0]);
// SIMCom allows five seconds for CPIN. Keep one deadline for startup,
// background polls and SMS preflight, with a small UART/service margin.
constexpr uint32_t kSimQueryTimeoutMs = 5500;

struct CommandStep {
  const char* command;
  uint32_t timeoutMs;
};

// Registration URCs are parsed independently of the active command.
const CommandStep kInitCommands[] = {
    {"ATE0", 1200},
    {"AT+CMEE=1", 1200},   // Numeric errors: classify CMS results consistently.
    {"AT+CSCLK=0", 1500},  // DTR is not wired on this EVB; stay awake.
    {"AT+IFC=0,0", 1500},  // Only TX/RX are wired; no RTS/CTS flow control.
    {"AT+CSMS?", 2000},  // Inspect service; do not rewrite it during recovery.
    {"AT+CMGF=1", 2000},
    {"AT+CREG=1", 1500},
    {"AT+CSCS=\"IRA\"",
     2000},  // Firmware bodies use ASCII; modem encodes DCS 0.
    {"AT+CSMP=49,167,0,0", 2000},
    {"AT+CNMI=2,1,0,1,0", 2000},
    {"AT+CSCA?",
     3000},  // Diagnose the SIM's SMS service centre; never invent it.
    {"AT+CSMINS=1", 1500},
};
constexpr int kSmsServiceStep = 4;
constexpr int kDeliveryFormatStep = 8;
constexpr int kDeliveryUrcStep = 9;
constexpr int kRequiredInitSteps = 8;
constexpr size_t kInitCommandCount =
    sizeof(kInitCommands) / sizeof(kInitCommands[0]);

// Short health commands only. AT+COPS? is intentionally excluded because it
// can take many seconds on some networks.
const CommandStep kPollCommands[] = {
    {"AT", 1200},
    {"AT+CREG?", 1800},
    {"AT+CSQ", 1800},
};
constexpr size_t kPollCommandCount =
    sizeof(kPollCommands) / sizeof(kPollCommands[0]);
}  // namespace

void SIM800LService::trace(char direction, const char* text) {
  TraceEntry& entry = traceData.entries[traceData.next];
  entry.at = millis();
  entry.jobId = activeJob >= 0 ? store.data.jobs[activeJob].id
                               : (pendingJob >= 0 ? pendingId : 0);
  entry.direction = direction;
  const size_t length = std::strlen(text);
  const size_t kept =
      length < sizeof(entry.text) - 1 ? length : sizeof(entry.text) - 1;
  std::memcpy(entry.text, text, kept);
  entry.text[kept] = '\0';
  entry.truncated = kept != length;
  traceData.next = (traceData.next + 1) % TRACE_CAPACITY;
  if (traceData.count < TRACE_CAPACITY) ++traceData.count;
  if(++traceData.total==0){traceData.count=1;traceData.total=1;}
}

void SIM800LService::begin() {
  if (!Config::ENABLE_SIM800L) {
    baud = 0;
    registration = -1;
    sim = "Disabled";
    operatorName = "Disabled";
    lastError = "";
    return;
  }

  // Large RX buffer protects modem responses while the dashboard/GPS gets a
  // time slice. UART2 is owned only by this service.
  uart.setRxBufferSize(4096);
  uart.setTxBufferSize(512);
  line.reserve(512);
  lastCommand.reserve(96);
  lastErrorCommand.reserve(96);
  lastModemLine.reserve(160);
  serviceCenter.reserve(96);
  operatorName.reserve(32);
  operatorCode.reserve(8);
  simCarrierName.reserve(32);
  simCarrierCode.reserve(8);
  subscriberNumber.reserve(20);
  iccid.reserve(24);
  lastDeliveryReport.reserve(384);
  restartProbe();
}

void SIM800LService::restartProbe() {
  trace('!', "Reacquiring UART; ESC sent");
  reinitializeAfterReady = false;
  waiting = false;
  baud = 0;
  registrationAt = 0;
  registrationWake = false;
  // Identity belongs to the currently confirmed SIM, never a previous boot.
  smsConfigured = false;
  outgoingSmsSupported = false;
  smsServiceMode = -1;
  reportPduLength = -1;
  phase = Phase::ProbeQuiet;
  clearSimIdentity();
  simObserved = false;
  sim = "Unknown";
  registrationObserved = false;
  signalObserved = false;
  lastSimQueryAt = millis() - Config::SIM800L_SIM_POLL_MS;
  lastIccidQueryAt = 0;
  iccidQueried = false;
  imsiQueried = false;
  operatorQueried = false;
  subscriberNumberQueried = false;
  operatorQueriedAt = 0;
  identityQuery = IdentityQuery::None;
  rateIndex = knownRateIndex;
  deliveryReportsConfigured = false;
  probeTry = 0;
  initStep = 0;
  pollStep = 0;
  missedResponses = 0;
  activeJob = -1;
  pendingJob = -1;

  gotOK = false;
  gotError = false;
  gotPrompt = false;
  gotReference = false;
  gotSimStatus = false;
  gotRegistration = false;
  gotSignal = false;
  gotIdentity = false;
  gotSmsService = false;
  gotIdentityValue = false;
  unexpectedPrompt = false;

  uart.end();
  uart.begin(kProbeBaudRates[rateIndex], SERIAL_8N1, Config::SIM800L_RX_PIN,
             Config::SIM800L_TX_PIN);
  uart.onReceiveError([this](hardwareSerial_error_t error) { uartHealth.observe(error); });

  nextAt = millis() + 350;
  line = "";
  droppingLine = false;
  // Recover a modem still accepting an abandoned SMS body, including after
  // an ESP32-only reboot. ESC is never Ctrl+Z and cannot submit that body.
  txBytes += uart.write(27);
  txBytes += uart.print('\r');
  lastTxAt = millis();
}

bool SIM800LService::recovering() const {
  return phase == Phase::ProbeQuiet || phase == Phase::Probe ||
         phase == Phase::SyncQuiet || phase == Phase::SyncAT ||
         phase == Phase::SyncIdentity || phase == Phase::Init;
}

void SIM800LService::escapeToSync() {
  waiting = false;
  pendingJob = -1;
  unexpectedPrompt = false;
  smsConfigured = false;
  phase = Phase::SyncQuiet;
  line = "";
  droppingLine = false;
  txBytes += uart.write(27);
  txBytes += uart.print('\r');
  lastTxAt = millis();
  nextAt = millis() + Config::SIM800L_SYNC_QUIET_MS;
}

void SIM800LService::startSynchronization(const String& reason) {
  trace('!', reason.c_str());
  ++synchronizationRecoveries;
  recoveryStatus = reason;
  syncAttempt = 0;
  escapeToSync();
}

void SIM800LService::synchronizationFailed() {
  if (++syncAttempt < Config::SIM800L_SYNC_ATTEMPTS) {
    escapeToSync();
    return;
  }
  recoveryStatus =
      "No verified AT/ATI response; checking UART baud. Check VDD and supply "
      "if this persists.";
  restartProbe();
  nextAt = millis() + Config::SIM800L_REACQUIRE_BACKOFF_MS;
}

void SIM800LService::beginInitialization() {
  reinitializeAfterReady = false;
  initStep = 0;
  smsConfigured = false;
  deliveryReportsConfigured = false;
  outgoingSmsSupported = false;
  smsServiceMode = -1;
  phase = Phase::Init;
  recoveryStatus = "Configuring awake, two-wire UART and SMS mode";
  sendCommand(kInitCommands[0].command, kInitCommands[0].timeoutMs);
}

bool SIM800LService::online() const {
  if (!Config::ENABLE_SIM800L || !baud || !goodReplySeen) {
    return false;
  }

  return !Domain::elapsed(millis(), lastGood, Config::SIM800L_STATUS_STALE_MS);
}

bool SIM800LService::registered() const {
  if (!Config::ENABLE_SIM800L || !online()) {
    return false;
  }

  return simReady() && registrationCurrent() && (registration == 1 || registration == 5);
}

bool SIM800LService::simReady() const {
  if (!Config::ENABLE_SIM800L || !online()) {
    return false;
  }

  return simObserved && simAgeMs() < Config::SIM800L_SIM_STALE_MS && sim == "READY";
}

void SIM800LService::clearSimIdentity() {
  iccid = ""; iccidStatus = "Unavailable";
  simCarrierName = "Unavailable"; simCarrierCode = "";
  subscriberNumber = ""; subscriberNumberStatus = "Unavailable";
  operatorName = "Not registered"; operatorCode = ""; accessTechnology = -1;
  iccidQueried = imsiQueried = subscriberNumberQueried = operatorQueried = false;
  registrationObserved = false; signalObserved = false; registration = -1; csq = 99;
}

void SIM800LService::observeSim(const String& state) {
  const bool changed = !simObserved || sim != state;
  if(simObserved&&sim=="READY"&&state!="READY")simSetupPending=true;
  if (changed || state != "READY") clearSimIdentity();
  sim = state; sim.trim(); simObserved = true; simAt = millis();
  if (changed && sim == "READY") {
    iccidStatus = "Checking"; simCarrierName = "Checking SIM";
    subscriberNumberStatus = "Checking";
  }
  gotSimStatus = true;
}

bool SIM800LService::registrationCurrent() const {
  return online() && registrationObserved &&
         registrationAgeMs() < Config::SIM800L_REGISTRATION_STALE_MS;
}

bool SIM800LService::signalCurrent() const {
  return online() && signalObserved &&
         signalAgeMs() < Config::SIM800L_SIGNAL_STALE_MS && csq >= 0 && csq <= 31;
}

bool SIM800LService::simDetected() const {
  if (!online() || !simObserved || simAgeMs() >= Config::SIM800L_SIM_STALE_MS) return false;
  return sim == "READY" || sim == "SIM PIN" || sim == "SIM PUK" ||
         sim == "SIM PIN2" || sim == "SIM PUK2" || sim.startsWith("PH-");
}

String SIM800LService::simStatusText() const {
  if (!Config::ENABLE_SIM800L) return "Disabled";
  if (!online()) return "Modem not responding";
  if (!simObserved || simAgeMs() >= Config::SIM800L_SIM_STALE_MS) return "Checking SIM card";
  if (sim == "READY") return "Detected and ready";
  if (sim == "SIM PIN") return "Detected; PIN required";
  if (sim == "SIM PUK") return "Detected; PUK required";
  if (sim.startsWith("PH-")) return "Detected; network lock active";
  if (sim == "NOT INSERTED") return "SIM card not detected";
  if (sim == "SIM BUSY") return "SIM card busy";
  if (sim == "SIM FAILURE") return "SIM card read failed";
  return sim.length() && sim != "Unknown" ? "Detected; not ready"
                                            : "SIM status unavailable";
}

String SIM800LService::simStatusDetail() const {
  if (!Config::ENABLE_SIM800L) return "Cellular module is disabled in Config.h.";
  if (!online()) return "No recent verified AT response from the modem.";
  if (!simObserved || simAgeMs() >= Config::SIM800L_SIM_STALE_MS) return transactionActive() ? "SIM check paused during SMS submission; current state is unconfirmed." : "Waiting for a fresh SIM status reply.";
  if (sim == "READY") return "The modem can access the SIM and no PIN or PUK is required.";
  if (sim == "SIM PIN") return "The card is present but locked by its SIM PIN.";
  if (sim == "SIM PUK") return "The card is present and requires its PUK code.";
  if (sim.startsWith("PH-")) return "The card is present but a modem or network lock blocks service.";
  if (sim == "NOT INSERTED") return "The modem reports that it cannot see a SIM card.";
  if (sim == "SIM BUSY") return "The modem reports that the SIM is temporarily busy.";
  if (sim == "SIM FAILURE") return "The modem could not read the SIM card.";
  return "The modem returned SIM state: " + sim + ".";
}

bool SIM800LService::transactionActive() const {
  return activeJob >= 0 || phase == Phase::SmsSetup || phase == Phase::Prompt ||
         phase == Phase::Result;
}

const char* SIM800LService::stage() const {
  switch (phase) {
    case Phase::Prompt:
      return "Waiting for SMS prompt; body not sent";
    case Phase::Result:
      return "Body sent; waiting for network result";
    case Phase::SmsSetup:
      return "Preparing SMS text mode";
    case Phase::Cooldown:
      return "Settling after submission";
    case Phase::SyncQuiet:
      return "Recovery quiet period";
    case Phase::SyncAT:
      return "Verifying AT response";
    case Phase::SyncIdentity:
      return "Verifying modem identity";
    case Phase::Init:
      return "Configuring modem";
    case Phase::AwaitSim:
      return "Waiting for a readable SIM card";
    case Phase::SimRadio:
    case Phase::EnableSimRadio:
      return "Checking SIM power and radio mode";
    case Phase::PreflightSim:
      return "Checking SIM";
    case Phase::PreflightRegistration:
      return "Checking current registration";
    case Phase::Recovery:
      return "Checking cellular service";
    case Phase::ProbeQuiet:
    case Phase::Probe:
      return "Finding UART baud";
    case Phase::Poll:
      return "Background health query";
    case Phase::Identity:
      return "Reading SIM/network identity";
    default:
      return "Idle";
  }
}

String SIM800LService::registrationText() const {
  if (!Config::ENABLE_SIM800L) return "Disabled";
  if (!online()) return "Modem unavailable";
  if (!simReady()) return "Not confirmed";
  if (!registrationCurrent()) return "Checking registration";
  switch (registration) {
    case 0:
      return "Not registered";
    case 1:
      return "Registered (home)";
    case 2:
      return "Searching";
    case 3:
      return "Registration denied";
    case 4:
      return "Registration unknown";
    case 5:
      return "Registered (roaming)";
    default:
      return "Unknown";
  }
}

void SIM800LService::sendCommand(const String& command, uint32_t waitMs) {
  // tick() already consumes input; never discard asynchronous registration
  // data.
  trace('>', command.c_str());
  gotOK = false;
  gotError = false;
  gotPrompt = false;
  gotReference = false;
  gotSimStatus = false;
  gotRegistration = false;
  gotSignal = false;
  gotIdentity = false;
  gotSmsService = false;
  gotIdentityValue = false;
  lastCommand = command;
  if (command == "AT+CPIN?") lastSimQueryAt = millis();
  if (command == "AT+CCID") lastIccidQueryAt = millis();
  commandCmsError = -1;

  txBytes += uart.print(command);
  txBytes += uart.print('\r');
  ++commandsSent;
  lastTxAt = millis();

  sentAt = millis();
  timeout = waitMs;
  waiting = true;
}

void SIM800LService::parseRegistration(const String& text) {
  int first = -1, second = -1;
  const int fields = sscanf(text.c_str(), "+CREG: %d,%d", &first, &second);
  const int status = fields == 2 ? second : first;
  if (fields < 1 || status < 0 || status > 5) return;
  const bool wasRegistered = registrationObserved &&
                             (registration == 1 || registration == 5);
  registration = status;
  registrationObserved = true;
  gotRegistration = true;
  registrationAt = millis();
  lastGood = millis(); goodReplySeen = true;
  lastRegistrationResponse = text;
  if (status == 1 || status == 5) {
    registrationWake = true;
    if (!wasRegistered) operatorQueried = false;
  } else {
    operatorName = "Not registered";
    operatorCode = "";
    accessTechnology = -1;
    operatorQueried = false;
  }
}

void SIM800LService::preflightFailed() {
  if (gotOK || gotError)
    missedResponses = 0;
  else {
    ++missedResponses;
    // A status line alone is not a terminal reply. Reacquire ownership before
    // another query, just as health polling does after a genuine timeout.
    startSynchronization(
        "No final command response; verifying AT/ATI before retry");
  }
}

void SIM800LService::startRecovery() {
  // Inspect first. Do not restart a radio that is already searching normally.
  recoveryStep = 0;
  radioMode = -1;
  operatorMode = -1;
  lastRecovery = millis();
  phase = Phase::Recovery;
  recoveryStatus = "Checking SIM, signal, radio and operator mode";
  sendCommand("AT+CPIN?", kSimQueryTimeoutMs);
}

void SIM800LService::advanceRecovery(bool success) {
  if (recoveryStep == 0 && !gotSimStatus) observeSim("Unknown");
  if (!success) {
    recoveryStatus = gotError ? lastError : "Recovery query timed out";
    phase = Phase::Idle;
    nextAt = millis() + Config::SMS_NETWORK_RECHECK_MS;
    preflightFailed();
    return;
  }
  missedResponses = 0;
  if (registered() && simReady()) {
    phase = Phase::Idle;
    nextAt = millis();
    recoveryStatus = "Registered";
    return;
  }
  if (recoveryStep == 0) {
    recoveryStep = 1;
    sendCommand("AT+CSQ", 3000);
  } else if (recoveryStep == 1) {
    recoveryStep = 2;
    sendCommand("AT+CFUN?", 10000);
  } else if (recoveryStep == 2 && (radioMode == 0 || radioMode == 4)) {
    recoveryStep = 3;
    recoveryStatus = "Enabling radio: modem reported RF disabled";
    sendCommand("AT+CFUN=1", 10000);
  } else if (recoveryStep == 2 || recoveryStep == 3) {
    recoveryStep = 4;
    sendCommand("AT+COPS?", 10000);
  } else if (recoveryStep == 4 && (operatorMode == 1 || operatorMode == 2)) {
    recoveryStep = 5;
    recoveryStatus = "Restoring automatic operator selection";
    sendCommand("AT+COPS=0", 120000);
  } else {
    recoveryStatus = "Monitoring registration; radio search is not restarted";
    phase = Phase::Idle;
    nextAt = millis();
  }
}

void SIM800LService::parseLine(const String& text) {
  if (!text.length()) {
    return;
  }

  ++receivedLines;
  if (waiting && (text == "OK" || text == "ERROR" || text.startsWith("+CMS ERROR:") || text.startsWith("+CME ERROR:"))) ++repliesReceived;
  const bool cnumResponse = text.startsWith("+CNUM:");
  const bool ccidResponse = text.startsWith("+CCID:") ||
      (phase == Phase::Identity && identityQuery == IdentityQuery::Iccid &&
       SimIdentity::digits(text, 15, 22));
  const bool imsiResponse = phase == Phase::Identity &&
      identityQuery == IdentityQuery::Imsi && SimIdentity::digits(text, 14, 16);
  trace('<', cnumResponse ? "[CNUM response; number hidden]"
                          : ccidResponse ? "[CCID response; identity hidden]"
                          : imsiResponse ? "[CIMI response; identity hidden]"
                                         : text.c_str());
  if (reportPduLength >= 0) {
    const unsigned expected = unsigned(reportPduLength);
    reportPduLength = -1;
    SmsCodec::Report report;
    if (!Domain::elapsed(millis(), reportPduAt,
                         Config::SMS_REPORT_PDU_TIMEOUT_MS) &&
        SmsCodec::pduReport(text.c_str(), text.length(), expected, report)) {
      lastDeliveryReport = text;
      ++deliveryReportsReceived;
      applyDeliveryReport(report);
      return;
    }
    ++deliveryReportsRejected;
    // A missing PDU must not swallow the next command's OK/error/URC.
  }
  lastModemLine = cnumResponse ? "[CNUM response; number hidden]"
                  : ccidResponse ? "[CCID response; identity hidden]"
                  : imsiResponse ? "[CIMI response; identity hidden]"
                                 : text.substring(0, 160);
  // An ERROR is still a response from a live modem, not evidence of lost UART.
  if (text == "OK" || text == "ERROR" || text.startsWith("+CMS ERROR:") ||
      text.startsWith("+CME ERROR:") || text.startsWith("+CPIN:") ||
      text.startsWith("+CSQ:") || text.startsWith("+CSCA:") ||
      text.startsWith("+COPS:") || text.startsWith("+CNUM:") ||
      text.startsWith("+CCID:") ||
      text.indexOf("SIM800") >= 0) {
    lastGood = millis(); goodReplySeen = true;
  }
  if (phase == Phase::SyncIdentity && waiting && text.indexOf("SIM800") >= 0)
    gotIdentity = true;
  if (text.startsWith("+CSCA:")) {
    serviceCenter = text.substring(6);
    serviceCenter.trim();
  }
  if (text.startsWith("+CSMS:")) {
    int service = -1, mt = -1, mo = -1, bm = -1;
    char extra = 0;
    if (phase == Phase::Init && initStep == kSmsServiceStep && waiting &&
        sscanf(text.c_str(), "+CSMS: %d,%d,%d,%d %c", &service, &mt, &mo, &bm,
               &extra) == 4 &&
        service >= 0 && service <= 1 && mt >= 0 && mt <= 1 && mo >= 0 &&
        mo <= 1 && bm >= 0 && bm <= 1) {
      gotSmsService = true;
      smsServiceMode = service;
      outgoingSmsSupported = mo == 1;
      lastGood = millis(); goodReplySeen = true;
    }
  }

  // These messages are useful evidence of a supply problem. Preserve them in
  // the dashboard instead of hiding them behind a generic network status.
  if (text.indexOf("UNDER-VOLTAGE") >= 0 || text.indexOf("OVER-VOLTAGE") >= 0) {
    lastError = text;
    lastErrorCommand = lastCommand;
    lastPowerEvent = text;
  }

  if (text == "SMS Ready") {
    ++smsReadyEvents;
    lastGood = millis(); goodReplySeen = true;
    // SMS initialization can finish after command setup. A readiness URC
    // alone must not discard CMGS/OK or inject ESC into the active send.
    // Revalidate settings once the transaction has ended, including when
    // this was the only visible indication of a real autobaud restart.
    if (smsConfigured && !recovering()) reinitializeAfterReady = true;
    return;
  }
  if (text == "RDY" || text == "NORMAL POWER DOWN" ||
      text == "UNDER-VOLTAGE POWER DOWN" || text == "OVER-VOLTAGE POWER DOWN") {
    ++modemRestarts;
    if (activeJob >= 0) {
      const bool retry =
          store.data.jobs[activeJob].attempts < Config::MAX_SMS_ATTEMPTS;
      finishActiveJob(
          phase == Phase::Result ? "unknown" : (retry ? "retry" : "failed"),
          phase == Phase::Result
              ? "SIM800L restarted after SMS submission began; check recipient "
                "before resending."
              : (retry ? "SIM800L restarted before SMS body; queued for retry."
                       : "SIM800L repeatedly restarted before SMS body; "
                         "attempt limit reached."));
    }
    lastError = text;
    lastErrorCommand = lastCommand;
    recoveryStatus = "Modem restart/power-down observed; reacquiring UART";
    lastPowerEvent = text;
    restartProbe();
    return;
  }

  if (text.startsWith("+CFUN:")) sscanf(text.c_str(), "+CFUN: %d", &radioMode);
  if (text.startsWith("+COPS:")) {
    String parsedName, parsedCode;
    int parsedMode = -1, parsedAccess = -1;
    if (SimIdentity::parseOperator(text, parsedMode, parsedName, parsedCode,
                                   parsedAccess)) {
      operatorMode = parsedMode;
      if (simReady() && registrationCurrent() && (registration == 1 || registration == 5)) {
        operatorName = parsedName; operatorCode = parsedCode; accessTechnology = parsedAccess;
      }
      gotIdentityValue = true;
      lastGood = millis(); goodReplySeen = true;
    }
  }
  if (simReady() && phase == Phase::Identity &&
      identityQuery == IdentityQuery::SubscriberNumber && text.startsWith("+CNUM:")) {
    String parsedNumber;
    if (SimIdentity::parseSubscriberNumber(text, parsedNumber)) {
      subscriberNumber = parsedNumber;
      subscriberNumberStatus = "Available from SIM";
      gotIdentityValue = true;
      lastGood = millis(); goodReplySeen = true;
    }
  }
  if (simReady() && phase == Phase::Identity && identityQuery == IdentityQuery::Iccid) {
    String parsedIccid;
    if (SimIdentity::parseIccid(text, true, parsedIccid)) {
      if (iccid.length() && iccid != parsedIccid) {
        observeSim("Unknown");
        lastSimQueryAt = millis() - Config::SIM800L_SIM_POLL_MS;
      } else {
        iccid = parsedIccid; iccidStatus = "Read successfully";
        gotIdentityValue = true;
        lastGood = millis(); goodReplySeen = true;
      }
    }
  }
  if (simReady() && phase == Phase::Identity && identityQuery == IdentityQuery::Imsi) {
    String parsedCarrier, parsedCode;
    if (SimIdentity::parseImsi(text, true, parsedCarrier, parsedCode)) {
      simCarrierName = parsedCarrier;
      simCarrierCode = parsedCode;
      gotIdentityValue = true;
      lastGood = millis(); goodReplySeen = true;
    }
  }

  if (text == "OK" && waiting) {
    if (phase != Phase::Result || gotReference) gotOK = true;
    lastGood = millis(); goodReplySeen = true;
  }

  if (text == "ERROR" || text.startsWith("+CMS ERROR:") ||
      text.startsWith("+CME ERROR:")) {
    gotError = waiting;
    lastError = text;
    lastErrorCommand = lastCommand;
    int simError = SimIdentity::cmeCode(text);
    if (text.startsWith("+CMS ERROR:")) {
      int cms = -1;
      if (sscanf(text.c_str(), "+CMS ERROR: %d", &cms) == 1) {
        if (cms == 310) simError = 10;
        else if (cms == 311) simError = 11;
        else if (cms == 313 || cms == 315) simError = 13;
        else if (cms == 314) simError = 14;
        else if (cms == 316) simError = 12;
      }
    }
    if (text == "+CME ERROR: SIM not inserted") simError = 10;
    if (text == "+CME ERROR: SIM PIN required") simError = 11;
    if (text == "+CME ERROR: SIM PUK required") simError = 12;
    if (simError == 10) observeSim("NOT INSERTED");
    else if (simError == 11) observeSim("SIM PIN");
    else if (simError == 12) observeSim("SIM PUK");
    else if (simError == 13 || simError == 15) observeSim("SIM FAILURE");
    else if (simError == 14) observeSim("SIM BUSY");
    else if (lastCommand == "AT+CPIN?" && waiting) observeSim("Unknown");
    if (text.startsWith("+CMS ERROR:")) {
      lastSmsError = text;
      lastSmsErrorCommand = lastCommand;
      int code = -1;
      char extra = 0;
      if (sscanf(text.c_str(), "+CMS ERROR: %d %c", &code, &extra) == 1) {
        commandCmsError = code;
      } else {
        String description = text.substring(11);
        description.trim();
        if (description == "no network service") commandCmsError = 331;
        if (description == "short message transfer rejected")
          commandCmsError = 21;
        if (description == "operation not allowed") commandCmsError = 302;
      }
    }
  }

  if (text.startsWith("+CMGS:") && phase == Phase::Result) {
    int reference = -1;
    if (activeJob >= 0 && !gotReference &&
        SmsCodec::submissionReference(text.c_str(), reference)) {
      gotReference = true;
      const unsigned byte = unsigned(reference) / 8;
      const uint8_t mask = uint8_t(1U << (unsigned(reference) % 8));
      if (usedReferences[byte] & mask) ambiguousReferences[byte] |= mask;
      usedReferences[byte] |= mask;
      store.data.jobs[activeJob].modemReference = reference;
      store.data.jobs[activeJob].reportEligible = true;
    }
    lastGood = millis(); goodReplySeen = true;
  }

  if (text.startsWith("+CDS:")) parseDeliveryReport(text);

  if (text.startsWith("+CPIN:")) {
    String state = text.substring(6); state.trim();
    observeSim(state);
  }
  if (text.startsWith("+CSMINS:")) {
    lastGood = millis(); goodReplySeen = true;
    int enabled = -1, inserted = -1;
    if (sscanf(text.c_str(), "+CSMINS: %d,%d", &enabled, &inserted) == 2) {
      if ((enabled==0||enabled==1)&&(inserted==0||inserted==1)) {
        observeSim("Unknown");
        lastSimQueryAt = millis() - Config::SIM800L_SIM_POLL_MS;
        if(!waiting&&(phase==Phase::Idle||phase==Phase::AwaitSim))nextAt=millis();
      }
    }
  }

  if (text.startsWith("+CREG:")) {
    parseRegistration(text);
  }

  if (text.startsWith("+CSQ:")) {
    int bitErrorRate = 0;
    int parsedCsq = 99;
    if (sscanf(text.c_str(), "+CSQ: %d,%d", &parsedCsq, &bitErrorRate) == 2) {
      csq = parsedCsq;
      gotSignal = true; signalObserved = true; signalAt = millis();
    }
  }
}

void SIM800LService::consumeUart() {
  // Drain the modem first whenever tick() runs. The 4096-byte UART buffer plus
  // bounded budget leaves time for GPS/dashboard work under continuous RX.
  int budget = 512;

  while (uart.available() && budget-- > 0) {
    const char c = uart.read();
    ++rxBytes;
    lastRxAt = millis();

    bool whitespaceOnly = true;
    if (c == '>') {
      for (size_t i = 0; i < line.length(); ++i)
        if (line.c_str()[i] != ' ' && line.c_str()[i] != '\t') {
          whitespaceOnly = false;
          break;
        }
    }
    if (c == '>' && !droppingLine && whitespaceOnly) {
      trace('<', "[SMS prompt >]");
      line = "";
      if (phase == Phase::Prompt && waiting) {
        gotPrompt = true;
      } else {
        ++unexpectedPrompts;
        unexpectedPrompt = true;
      }
      continue;
    }

    if (c == '\n') {
      if (droppingLine) {
        droppingLine = false;
        line = "";
        continue;
      }
      String completedLine = line;
      completedLine.trim();
      line = "";
      parseLine(completedLine);
      continue;
    }

    if (c != '\r') {
      if (droppingLine) continue;
      if (line.length() < 512) {
        line += c;
      } else {
        trace('!', "Oversized UART line discarded");
        line = "";
        droppingLine = true;
      }
    }
  }
}

void SIM800LService::parseDeliveryReport(const String& text) {
  lastDeliveryReport = text.substring(0, 384);
  SmsCodec::Report report;
  if (SmsCodec::textReport(text.c_str(), report)) {
    ++deliveryReportsReceived;
    applyDeliveryReport(report);
    return;
  }
  // Decode both documented report forms without issuing an AT read.
  unsigned length = 0;
  char extra = 0;
  if (sscanf(text.c_str(), "+CDS: %u %c", &length, &extra) == 1 &&
      length >= 19 && length <= 191) {
    reportPduLength = int(length);
    reportPduAt = millis();
    return;
  }
  ++deliveryReportsRejected;
}

void SIM800LService::applyDeliveryReport(const SmsCodec::Report& report) {
  if (ambiguousReferences[report.reference / 8] &
      (1U << (report.reference % 8))) {
    ++deliveryReportsRejected;
    trace('!',
          "Report not assigned: modem reference reused during this ESP32 boot");
    return;
  }
  const std::string normalized = Domain::normalizePhone(report.recipient);
  if (normalized.empty()) {
    ++deliveryReportsRejected;
    return;
  }
  int matched = -1;
  for (size_t i = 0; i < Config::MAX_SMS_JOBS; ++i) {
    const SmsJob& job = store.data.jobs[i];
    if (job.reportEligible && job.modemReference == int(report.reference) &&
        job.phone == normalized.c_str()) {
      if (matched >= 0) {
        ++deliveryReportsRejected;
        return;
      }  // Never guess a job identity.
      matched = int(i);
    }
  }
  if (matched < 0) {
    ++deliveryReportsRejected;
    return;
  }
  ++deliveryReportsMatched;
  SmsJob& job = store.data.jobs[matched];
  if (job.state == "delivered" || job.state == "undelivered" ||
      job.deliveryStatus == 0 || job.deliveryStatus >= 64) return;
  job.deliveryStatus = int(report.status);
  job.carrierReport = lastDeliveryReport;
  if (report.status == 0) {
    // The active slot stays reserved until its modem transaction closes.
    if (matched != activeJob) job.state = "delivered";
    job.detail =
        "Carrier reports delivery (status 0); verify on recipient "
        "handset. This is not a read receipt.";
  } else if (report.status >= 64) {
    if (matched != activeJob) job.state = "undelivered";
    job.detail = "Carrier reports non-delivery (status " +
                 String(int(report.status)) + ").";
  } else {
    job.detail = "Carrier status " + String(int(report.status)) +
                 "; delivery is not yet confirmed.";
  }
  historyDirty = true;
  historyRetryAt = millis() + Config::SMS_HISTORY_FLUSH_MS;
}

void SIM800LService::captureFailure(const SmsJob& job, const String& detail) {
  auto& failure = traceData.lastFailure;
  if(++failure.revision==0)failure.revision=1;
  failure.present = true;
  failure.at = millis();
  failure.jobId = job.id;
  failure.bodySent = phase == Phase::Result;
  failure.gotReference = gotReference;
  failure.modemReference = job.modemReference;
  failure.signalCsq = csq;
  failure.commandElapsedMs = uint32_t(millis() - sentAt);
  failure.rxAgeMs = rxAgeMs();
  snprintf(failure.stage, sizeof(failure.stage), "%s", stage());
  snprintf(failure.command, sizeof(failure.command), "%s", lastCommand.c_str());
  snprintf(failure.reason, sizeof(failure.reason), "%s", detail.c_str());
  failure.count = traceData.count < FAILURE_TRACE_CAPACITY
                      ? traceData.count
                      : FAILURE_TRACE_CAPACITY;
  const size_t first =
      (traceData.next + TRACE_CAPACITY - failure.count) % TRACE_CAPACITY;
  for (size_t i = 0; i < failure.count; ++i)
    failure.entries[i] = traceData.entries[(first + i) % TRACE_CAPACITY];
}

void SIM800LService::finishActiveJob(const String& state,
                                     const String& detail) {
  if (activeJob < 0) {
    return;
  }

  SmsJob& job = store.data.jobs[activeJob];
  trace('!', (state + ": " + detail).c_str());
  // A carrier result may precede final OK, timeout or modem restart. Preserve
  // that result while releasing the slot only at this transaction boundary.
  if (job.reportEligible && job.deliveryStatus == 0) job.state = "delivered";
  else if (job.reportEligible && job.deliveryStatus >= 64) job.state = "undelivered";
  if (job.state != "delivered" && job.state != "undelivered") {
    if (state == "unknown" || state == "failed" || state == "retry")
      captureFailure(job, detail);
    job.state = state;
    job.detail = detail;
  }
  job.changed = millis();

  // Ordinary history is saved during idle service, outside prompt/result RX.
  // The emergency coordinator separately persists critical incident outcomes.
  historyDirty = true;
  historyRetryAt = millis() + Config::SMS_HISTORY_FLUSH_MS;
  activeJob = -1;
}

void SIM800LService::setWaitingDetail(int jobIndex, const String& detail) {
  if (jobIndex < 0 || jobIndex >= static_cast<int>(Config::MAX_SMS_JOBS)) {
    return;
  }

  SmsJob& job = store.data.jobs[jobIndex];
  if (job.detail == detail) {
    return;
  }

  // Waiting text is dashboard telemetry. Keep it in RAM instead of writing NVS
  // on every network-registration check.
  job.detail = detail;
}
bool SIM800LService::canCancel(uint32_t jobId) const {
  if(!jobId)return false;
  for(size_t i=0;i<Config::MAX_SMS_JOBS;++i){const auto& job=store.data.jobs[i];
    if(job.id!=jobId)continue;
    if(job.state=="queued"||job.state=="retry")return true;
    return job.state=="sending"&&activeJob==int(i)&&phase!=Phase::Result;
  }
  return false;
}
void SIM800LService::cancellationCommitted(uint32_t jobId){
  if(!jobId)return;
  for(const auto& job:store.data.jobs)if(job.id==jobId&&job.state=="cancelled"){
    if(activeJob>=0&&store.data.jobs[activeJob].id==jobId){
      // Ctrl-Z has not been written: ESC abandons the text-input transaction.
      // Once Result begins, cancellation is no longer offered or accepted.
      if(phase==Phase::Result)return;
      activeJob=-1;startSynchronization("SMS cancelled before submission; verifying command mode");
    }
    if(pendingJob>=0&&store.data.jobs[pendingJob].id==jobId){pendingJob=-1;pendingId=0;}
    historyDirty=true;historyRetryAt=millis()+Config::SMS_HISTORY_FLUSH_MS;
    return;
  }
}

void SIM800LService::beginRegistrationPreflight(int jobIndex) {
  pendingJob = jobIndex;
  pendingId = store.data.jobs[jobIndex].id;
  phase = Phase::PreflightRegistration;
  sendCommand("AT+CREG?", 5000);
}

void SIM800LService::beginSmsSend(int jobIndex, uint32_t now) {
  // The UI can cancel/reuse a slot while CPIN/CREG is pending. Identity and
  // priority must still match immediately before committing to a transaction.
  if (!simReady() || !registered() || jobIndex < 0 || jobIndex >= int(Config::MAX_SMS_JOBS) ||
      store.data.jobs[jobIndex].id != pendingId ||
      (store.data.jobs[jobIndex].state != "queued" &&
       store.data.jobs[jobIndex].state != "retry") ||
      selectPendingJob(now) != jobIndex) {
    pendingJob = -1;
    phase = Phase::Idle;
    nextAt = now;
    return;
  }
  SmsJob& job = store.data.jobs[jobIndex];
  if (!SmsCodec::validText(job.text.c_str(), job.text.length())) {
    activeJob = jobIndex;
    finishActiveJob("failed",
                    "Invalid SMS text or exceeds 160 GSM characters; "
                    "nothing submitted.");
    pendingJob = -1;
    phase = Phase::Idle;
    nextAt = now;
    return;
  }
  if (guard && !guard(guardContext, job)) {
    pendingJob = -1;
    phase = Phase::Idle;
    nextAt = now + 5000;
    job.detail = "Waiting for durable incident storage";
    return;
  }

  // Mark the job in RAM only. Avoid an NVS/flash write immediately before the
  // time-sensitive modem transaction. If power is lost here, startup already
  // cancels persisted queued jobs instead of blindly replaying them, so this
  // still avoids duplicate sends after reboot.
  job.state = "sending";
  job.detail = "Sending through SIM800L.";
  job.modemReference = -1;
  job.reportEligible = false;
  job.deliveryStatus = -1;
  job.carrierReport = "";
  job.deliveryRequested = deliveryReportsConfigured;
  job.attempts++;
  job.changed = now;

  activeJob = jobIndex;
  pendingJob = -1;
  phase = Phase::SmsSetup;

  sendCommand("AT+CMGF=1", 2500);
}

void SIM800LService::startSmsPrompt() {
  phase = Phase::Prompt;
  sendCommand("AT+CMGS=\"" + store.data.jobs[activeJob].phone + "\"",
              Config::SMS_PROMPT_TIMEOUT_MS);
}

int SIM800LService::selectPendingJob(uint32_t now) {
  if (!store.healthy) return -1;
  int selectedJob = -1;

  for (size_t i = 0; i < Config::MAX_SMS_JOBS; ++i) {
    SmsJob& job = store.data.jobs[i];

    if (!job.id || (job.state != "queued" && job.state != "retry")) {
      continue;
    }

    const bool emergency = job.kind == "check" || job.kind == "crash";
    if (!emergency &&
        Domain::elapsed(now, job.created, Config::SMS_JOB_TTL_MS)) {
      activeJob = static_cast<int>(i);
      finishActiveJob("failed", String("Expired after ") +
                                    String(Config::SMS_JOB_TTL_MS / 60000) +
                                    " minutes waiting for cellular service.");
      return -1;
    }

    if (job.state == "retry" &&
        !Domain::elapsed(now, job.changed, Config::SMS_RETRY_DELAY_MS)) {
      continue;
    }

    if (selectedJob < 0) {
      selectedJob = static_cast<int>(i);
      continue;
    }

    // Crash alerts outrank introduction messages. Within the same priority,
    // preserve FIFO order by job id.
    const int jobIsCrash = job.kind == "crash"   ? 2
                           : job.kind == "check" ? 1
                                                 : 0;
    const SmsJob& selected = store.data.jobs[selectedJob];
    const int selectedIsCrash = selected.kind == "crash"   ? 2
                                : selected.kind == "check" ? 1
                                                           : 0;

    if ((jobIsCrash > selectedIsCrash) ||
        (jobIsCrash == selectedIsCrash && job.id < selected.id)) {
      selectedJob = static_cast<int>(i);
    }
  }

  return selectedJob;
}

void SIM800LService::tick() {
  if (!Config::ENABLE_SIM800L) {
    return;
  }

  const uint32_t tickAt = millis();
  if (lastTick && uint32_t(tickAt - lastTick) > maxServiceGapMs)
    maxServiceGapMs = tickAt - lastTick;
  lastTick = tickAt;
  consumeUart();
  const uint32_t now = millis();
  if (simObserved && simAgeMs() >= Config::SIM800L_SIM_STALE_MS) {
    clearSimIdentity();
    simObserved = false;
    sim = "Unknown";
  }
  if (unexpectedPrompt) {
    if (activeJob >= 0) {
      const bool submitted = phase == Phase::Result;
      const bool retry =
          store.data.jobs[activeJob].attempts < Config::MAX_SMS_ATTEMPTS;
      finishActiveJob(submitted ? "unknown" : (retry ? "retry" : "failed"),
                      submitted ? "Unexpected SMS prompt after submission; "
                                  "check recipient before resending."
                                : "Unexpected SMS input state before body; "
                                  "recovering command mode.");
    }
    startSynchronization(
        "Late/unexpected SMS prompt; cancelling text input and verifying "
        "AT/ATI");
    return;
  }

  // A persistence failure must not freeze modem diagnostics or UART recovery.
  if (phase == Phase::Idle && !waiting && activeJob < 0 && historyDirty &&
      static_cast<int32_t>(now - historyRetryAt) >= 0) {
    historyDirty = !store.commit(store.data);
    historyRetryAt = now + 10000;
  }

  // ---------------------------------------------------------------------------
  // Finish the command currently owned by the modem state machine.
  // ---------------------------------------------------------------------------
  if (waiting) {
    if (phase == Phase::Prompt && gotPrompt && !gotError) {
      if (!simReady()) {
        const bool retry = store.data.jobs[activeJob].attempts < Config::MAX_SMS_ATTEMPTS;
        finishActiveJob(retry ? "retry" : "failed", "SIM readiness changed before SMS body; nothing submitted.");
        startSynchronization("SIM check required before SMS body; cancelling input");
        return;
      }

      // Do not run any other AT command between the prompt and Ctrl+Z.
      char bodyInfo[64];
      std::snprintf(bodyInfo, sizeof(bodyInfo),
                    "SMS body: %u bytes + Ctrl-Z (content omitted)",
                    unsigned(store.data.jobs[activeJob].text.length()));
      trace('>', bodyInfo);
      txBytes += uart.print(store.data.jobs[activeJob].text);
      txBytes += uart.write(26);
      lastTxAt = millis();

      phase = Phase::Result;
      gotOK = false;
      gotError = false;
      gotReference = false;
      line = "";
      sentAt = now;
      timeout = Config::SMS_RESULT_TIMEOUT_MS;
      return;
    }

    const bool expired = Domain::elapsed(now, sentAt, timeout);

    // Result is stricter than the old implementation: +CMGS alone is NOT
    // enough. A reliable submission requires +CMGS followed by the final OK.
    bool completedNormally = false;
    if (phase == Phase::Result) {
      completedNormally = gotReference && gotOK;
    } else if (phase == Phase::Prompt) {
      completedNormally = gotPrompt;
    } else {
      completedNormally = gotOK;
      if (phase == Phase::PreflightRegistration)
        completedNormally &= gotRegistration;
      if (phase == Phase::PreflightSim || phase == Phase::AwaitSim) completedNormally &= gotSimStatus;
      if (phase == Phase::SyncIdentity) completedNormally &= gotIdentity;
      if (phase == Phase::Init && initStep == kSmsServiceStep)
        completedNormally &= gotSmsService;
    }

    if (!gotError && !expired && !completedNormally) {
      return;
    }

    if (expired && !completedNormally && !gotError) {
      ++commandTimeouts;
      lastError = "Timeout waiting for " + lastCommand;
      lastErrorCommand = lastCommand;
      trace('!', lastError.c_str());
    }
    const bool commandSuccess = completedNormally && !gotError;
    waiting = false;

    switch (phase) {
      case Phase::Probe:
        if (commandSuccess) {
          knownRateIndex = rateIndex;
          baud = kProbeBaudRates[rateIndex];
          syncAttempt = 0;
          phase = Phase::SyncIdentity;
          sendCommand("ATI", 2000);
        } else {
          if (++probeTry >= 3) {
            probeTry = 0;
            rateIndex = (rateIndex + 1) % kProbeBaudCount;
            uart.end();
            uart.begin(kProbeBaudRates[rateIndex], SERIAL_8N1,
                       Config::SIM800L_RX_PIN, Config::SIM800L_TX_PIN);
          }
          // Every baud/attempt gets an escape and quiet window. A delayed
          // prompt cannot trap all subsequent AT probes in SMS body mode.
          phase = Phase::ProbeQuiet;
          line = "";
          droppingLine = false;
          txBytes += uart.write(27);
          txBytes += uart.print('\r');
          lastTxAt = millis();
          nextAt = now + ((rateIndex == knownRateIndex && probeTry == 0)
                              ? Config::SIM800L_REACQUIRE_BACKOFF_MS
                              : 350);
        }
        break;

      case Phase::SyncAT:
        if (commandSuccess) {
          phase = Phase::SyncIdentity;
          sendCommand("ATI", 2000);
        } else
          synchronizationFailed();
        break;

      case Phase::SyncIdentity:
        if (commandSuccess && gotIdentity)
          beginInitialization();
        else
          synchronizationFailed();
        break;

      case Phase::Init:
        if(!commandSuccess&&gotError&&initStep>=kSmsServiceStep&&simObserved&&sim!="READY"&&sim!="Unknown"){
          smsConfigured=false;simSetupPending=true;phase=Phase::AwaitSim;
          recoveryStatus="Waiting for a readable SIM before SMS setup";
          nextAt=now+Config::SIM800L_SIM_POLL_MS;break;
        }
        if (initStep == kDeliveryFormatStep)
          deliveryReportsConfigured = commandSuccess && smsServiceMode == 0;
        if (initStep == kDeliveryUrcStep)
          deliveryReportsConfigured &= commandSuccess;
        // An explicit rejection of this diagnostic query must not disable
        // working SMS. Leave support unknown and disable direct status URCs.
        // A valid response that explicitly says MO unsupported still blocks.
        if (((!commandSuccess && !(initStep == kSmsServiceStep && gotError)) ||
             (initStep == kSmsServiceStep && gotSmsService &&
              !outgoingSmsSupported)) &&
            initStep < kRequiredInitSteps) {
          startSynchronization("Required modem setup failed: " + lastCommand);
          nextAt = now + Config::SIM800L_REACQUIRE_BACKOFF_MS;
          break;
        }
        if (!commandSuccess && !gotError) {
          // Optional settings may be unsupported (ERROR), but a timeout
          // leaves command ownership uncertain; never pipeline past it.
          startSynchronization("Modem configuration response timed out");
          break;
        }

        if (++initStep == kSmsServiceStep) {
          phase=Phase::SimRadio;radioMode=-1;lastSimRadioAt=now;
          sendCommand("AT+CFUN?",2500);
        } else if (initStep < static_cast<int>(kInitCommandCount)) {
          const char* command = kInitCommands[initStep].command;
          // Phase 2+ may require TE acknowledgements. We do not implement
          // CNMA here: preserve outgoing SMS, disable direct report routing.
          if (smsServiceMode != 0 && initStep == kDeliveryFormatStep)
            command = "AT+CSMP=17,167,0,0";
          if (smsServiceMode != 0 && initStep == kDeliveryUrcStep)
            command = "AT+CNMI=2,1,0,0,0";
          sendCommand(command, kInitCommands[initStep].timeoutMs);
        } else {
          phase = Phase::Idle;
          nextAt = now;
          pollStep = 0;
          missedResponses = 0;
          smsConfigured = true;
          simSetupPending = false;
          recoveryStatus = "SMS/UART configured; monitoring registration";
        }
        break;

      case Phase::SimRadio:
        if(commandSuccess&&(radioMode==0||radioMode==4)){
          phase=Phase::EnableSimRadio;recoveryStatus="Restoring enabled SIM/radio functions";
          sendCommand("AT+CFUN=1",10000);
        }else if(commandSuccess||gotError){phase=Phase::AwaitSim;sendCommand("AT+CPIN?",kSimQueryTimeoutMs);}
        else startSynchronization("Radio-mode query timed out; verifying command mode");
        break;
      case Phase::EnableSimRadio:
        if(commandSuccess||gotError){phase=Phase::AwaitSim;nextAt=now+1000;}
        else startSynchronization("Radio enable timed out; verifying command mode");
        break;
      case Phase::AwaitSim:
        if(commandSuccess&&gotSimStatus&&simReady()){
          phase=Phase::Init;initStep=kSmsServiceStep;
          recoveryStatus="SIM ready; completing SMS setup";
          sendCommand(kInitCommands[initStep].command,kInitCommands[initStep].timeoutMs);
        }else if(gotError||gotOK){
          smsConfigured=false;phase=Phase::AwaitSim;
          recoveryStatus="Waiting for SIM: "+sim;
          nextAt=now+Config::SIM800L_SIM_POLL_MS;
        }else startSynchronization("SIM check timed out; verifying command mode");
        break;

      case Phase::Poll:
        if (lastCommand == "AT+CPIN?" && !gotSimStatus) observeSim("Unknown");
        if (!commandSuccess && !gotError && !gotOK) {
          startSynchronization("Health query timed out; verifying command mode");
          break;
        }
        if (commandSuccess || gotError) {
          missedResponses = 0;
        } else {
          // A failed SIM check is unconfirmed; other health values expire independently.
          missedResponses++;
        }

        if (lastCommand != "AT+CPIN?") pollStep = (pollStep + 1) % kPollCommandCount;

        if (missedResponses >= Config::SIM800L_MAX_MISSED_POLLS) {
          startSynchronization(
              "Repeated health-query timeouts; verifying command mode");
          break;
        }

        phase = Phase::Idle;
        nextAt = now + Config::SIM800L_POLL_INTERVAL_MS;
        break;

      case Phase::Identity:
        if (!commandSuccess && !gotError) {
          if (identityQuery == IdentityQuery::SubscriberNumber) subscriberNumberStatus = "Query timed out; retrying";
          identityQuery = IdentityQuery::None;
          startSynchronization("SIM identity query timed out; verifying command mode");
          break;
        }
        if (identityQuery == IdentityQuery::Iccid) {
          iccidQueried = true;
          if (!gotIdentityValue)
            iccidStatus = gotError ? "SIM identity query rejected"
                                   : "SIM identity unavailable";
        } else if (identityQuery == IdentityQuery::Imsi) {
          imsiQueried = true;
          if (!gotIdentityValue) {
            simCarrierName = gotError ? "Query unsupported or rejected"
                                      : "Not reported by SIM";
            simCarrierCode = "";
          }
        } else if (identityQuery == IdentityQuery::Operator) {
          operatorQueried = true;
          operatorQueriedAt = now;
          if (!gotIdentityValue) {
            operatorName = registered() ? "Not reported by modem"
                                        : "Not registered";
            operatorCode = "";
            accessTechnology = -1;
          }
        } else if (identityQuery == IdentityQuery::SubscriberNumber) {
          subscriberNumberQueried = true;
          if (!gotIdentityValue) {
            subscriberNumber = "";
            subscriberNumberStatus = gotError ? "Query unsupported or rejected"
                                               : "Not stored on SIM";
          }
        }
        identityQuery = IdentityQuery::None;
        phase = Phase::Idle;
        nextAt = now + 250;
        break;

      case Phase::Recovery:
        advanceRecovery(commandSuccess);
        break;

      case Phase::PreflightSim:
        if (!gotSimStatus) observeSim("Unknown");
        if (commandSuccess && gotSimStatus && sim == "READY") {
          if (pendingJob < 0 || store.data.jobs[pendingJob].id != pendingId ||
              (store.data.jobs[pendingJob].state != "queued" &&
               store.data.jobs[pendingJob].state != "retry")) {
            pendingJob = -1;
            phase = Phase::Idle;
            nextAt = now;
            break;
          }
          beginRegistrationPreflight(pendingJob);
          break;
        }

        if (pendingJob >= 0) {
          setWaitingDetail(
              pendingJob, gotSimStatus
                              ? "Waiting for SIM card: " + sim + "."
                              : "SIM800L did not return SIM status; retrying.");
        }

        pendingJob = -1;
        phase = Phase::Idle;
        nextAt = now + Config::SMS_NETWORK_RECHECK_MS;
        preflightFailed();
        break;

      case Phase::PreflightRegistration:
        if (commandSuccess && gotRegistration &&
            (registration == 1 || registration == 5)) {
          missedResponses = 0;
          beginSmsSend(pendingJob, now);
          break;
        }

        if (pendingJob >= 0) {
          if (gotRegistration) {
            setWaitingDetail(pendingJob, "Waiting for cellular registration: " +
                                             registrationText() + " (CREG " +
                                             String(registration) + ").");
          } else {
            setWaitingDetail(
                pendingJob,
                "Could not confirm cellular registration; retrying.");
          }
        }

        pendingJob = -1;
        phase = Phase::Idle;
        nextAt = now + Config::SMS_NETWORK_RECHECK_MS;
        preflightFailed();
        break;

      case Phase::SmsSetup:
        if (commandSuccess) {
          startSmsPrompt();
          break;
        }

        if (activeJob >= 0) {
          const bool retry =
              store.data.jobs[activeJob].attempts < Config::MAX_SMS_ATTEMPTS;
          finishActiveJob(
              retry ? "retry" : "failed",
              gotError ? lastError : "SIM800L did not enter SMS text mode.");
        }

        startSynchronization(
            "SMS setup failed; verifying command mode before retry");
        break;

      case Phase::Prompt:
        if (activeJob >= 0) {
          const bool retry =
              store.data.jobs[activeJob].attempts < Config::MAX_SMS_ATTEMPTS;
          finishActiveJob(retry ? "retry" : "failed",
                          gotError
                              ? lastError
                              : "SIM800L did not return the SMS prompt (>).");
        }

        startSynchronization(
            "SMS prompt failed; cancelling input and verifying AT/ATI before "
            "retry");
        break;

      case Phase::Result:
        if (commandSuccess) {
          lastError = "";
          lastSubmissionAt = now;
          finishActiveJob("accepted",
                          store.data.jobs[activeJob].deliveryRequested
                              ? "Submitted to network (+CMGS and OK); handset "
                                "delivery unconfirmed. Awaiting carrier report."
                              : "Submitted to network (+CMGS and OK); handset "
                                "delivery unconfirmed. Reports unavailable; "
                                "verify with recipient.");
        } else if (gotError && !gotReference) {
          // A generic modem error or network timeout after Ctrl+Z does not
          // prove non-submission. Retry only explicit transfer rejection or
          // no-network results; preserve other errors as uncertain outcomes.
          const bool rejected = commandCmsError == 21 || commandCmsError == 331;
          const bool retry =
              rejected && activeJob >= 0 &&
              store.data.jobs[activeJob].attempts < Config::MAX_SMS_ATTEMPTS;
          finishActiveJob(rejected ? (retry ? "retry" : "failed") : "unknown",
                          lastError);
        } else if (gotReference) {
          // +CMGS without the final OK is ambiguous. Never auto-retry because
          // the network may already have the message and a retry can duplicate
          // it.
          finishActiveJob(
              "unknown",
              "SIM800L returned +CMGS but no final OK. Submission is "
              "uncertain; check the recipient before resending.");
        } else {
          finishActiveJob("unknown",
                          "No complete SIM800L send confirmation was received. "
                          "Check the recipient before resending.");
        }

        if (commandSuccess) {
          phase = Phase::Cooldown;
          nextAt = now + Config::SMS_MODEM_SETTLE_MS;
        } else {
          startSynchronization(
              "SMS result incomplete/rejected; verifying command mode (no "
              "blind resend)");
          // Let the modem finish its SMS/network housekeeping before new
          // setup commands. This delay is cooperative and never resends the
          // uncertain job. Late responses remain diagnostics only.
          nextAt = now + Config::SMS_RESULT_RECOVERY_QUIET_MS;
        }
        break;

      default:
        break;
    }

    return;
  }

  if (phase == Phase::Cooldown) {
    if (static_cast<int32_t>(now - nextAt) < 0) {
      return;
    }
    phase = Phase::Idle;
    nextAt = now;
  }

  if(phase==Phase::Idle&&simSetupPending){
    if(simReady())beginInitialization();
    else{smsConfigured=false;phase=Phase::AwaitSim;nextAt=now;}
    return;
  }
  if (phase == Phase::Idle && reinitializeAfterReady) {
    reinitializeAfterReady = false;
    startSynchronization(
        "SMS Ready observed; revalidating settings after transaction");
    return;
  }
  if (phase == Phase::Idle && registrationWake) {
    registrationWake = false;
    nextAt = now;
  }
  // A newly queued job need not wait for the background diagnostic interval.
  if (phase == Phase::Idle && registered()) {
    const int queued = selectPendingJob(now);
    if (queued >= 0 && store.data.jobs[queued].id != lastScheduledJob) {
      lastScheduledJob = store.data.jobs[queued].id;
      nextAt = now;
    }
  }

  if (static_cast<int32_t>(now - nextAt) < 0) {
    return;
  }

  if (phase == Phase::ProbeQuiet) {
    phase = Phase::Probe;
    sendCommand("AT", 1000);
    return;
  }
  if (phase == Phase::SyncQuiet) {
    phase = Phase::SyncAT;
    sendCommand("AT", 1500);
    return;
  }

  if(phase==Phase::AwaitSim){
    if(uint32_t(now-lastSimRadioAt)>=Config::SIM800L_RECOVERY_CHECK_MS){phase=Phase::SimRadio;radioMode=-1;lastSimRadioAt=now;sendCommand("AT+CFUN?",2500);}
    else sendCommand("AT+CPIN?",kSimQueryTimeoutMs);
    return;
  }
  if (phase != Phase::Idle) {
    return;
  }

  // ---------------------------------------------------------------------------
  // Queued SMS always has priority over background diagnostics.
  // ---------------------------------------------------------------------------
  const int selectedJob = selectPendingJob(now);

  if (selectedJob >= 0) {
    // Back-to-back submissions are deliberately spaced. This only delays the
    // NEXT SMS; GPS/dashboard/other modules continue running normally.
    if (lastSubmissionAt &&
        !Domain::elapsed(now, lastSubmissionAt, Config::SMS_MIN_GAP_MS)) {
      const uint32_t remaining =
          Config::SMS_MIN_GAP_MS - uint32_t(now - lastSubmissionAt);
      setWaitingDetail(selectedJob, "SIM800L settling after previous SMS (" +
                                        String((remaining + 999) / 1000) +
                                        " s).");
      nextAt = lastSubmissionAt + Config::SMS_MIN_GAP_MS;
      return;
    }

    if (!online()) {
      setWaitingDetail(selectedJob,
                       "Waiting for SIM800L on RX2/TX2 (GPIO16/GPIO17).");
      phase = Phase::Poll;
      pollStep = 0;
      sendCommand(kPollCommands[0].command, kPollCommands[0].timeoutMs);
      return;
    }

    if (!registered() &&
        (!lastRecovery || Domain::elapsed(now, lastRecovery,
                                          Config::SIM800L_RECOVERY_CHECK_MS))) {
      startRecovery();
      return;
    }
    pendingJob = selectedJob;
    pendingId = store.data.jobs[selectedJob].id;

    phase = Phase::PreflightSim;
    sendCommand("AT+CPIN?", kSimQueryTimeoutMs);
    return;
  }

  if (Domain::elapsed(now, lastSimQueryAt, Config::SIM800L_SIM_POLL_MS)) {
    phase = Phase::Poll;
    sendCommand("AT+CPIN?", kSimQueryTimeoutMs);
    return;
  }

  // Maintain registration readiness even before an alert is queued.
  if (!registered() && online() &&
      (!lastRecovery ||
       Domain::elapsed(now, lastRecovery, Config::SIM800L_RECOVERY_CHECK_MS))) {
    startRecovery();
    return;
  }

  // Read identity only while no alert is waiting. These are bounded, one-at-a-
  // time queries. CNUM may legitimately return OK without a number because the
  // subscriber number is optional SIM data.
  if (simReady() && (!iccidQueried || Domain::elapsed(now, lastIccidQueryAt, Config::SIM800L_IDENTITY_RECHECK_MS))) {
    identityQuery = IdentityQuery::Iccid;
    phase = Phase::Identity;
    sendCommand("AT+CCID", 3000);
    return;
  }
  if (simReady() && !imsiQueried) {
    identityQuery = IdentityQuery::Imsi;
    phase = Phase::Identity;
    sendCommand("AT+CIMI", 3000);
    return;
  }
  if (registered() &&
      (!operatorQueried ||
       Domain::elapsed(now, operatorQueriedAt, 300000))) {
    identityQuery = IdentityQuery::Operator;
    phase = Phase::Identity;
    sendCommand("AT+COPS?", 5000);
    return;
  }
  if (simReady() && !subscriberNumberQueried) {
    identityQuery = IdentityQuery::SubscriberNumber;
    phase = Phase::Identity;
    sendCommand("AT+CNUM", 3000);
    return;
  }

  // No SMS is waiting: perform one short health query then leave the modem
  // quiet. A failed health poll does not immediately destroy known-good state.
  phase = Phase::Poll;
  sendCommand(kPollCommands[pollStep].command,
              kPollCommands[pollStep].timeoutMs);
}
