#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <atomic>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif
#include "../../Config.h"
#include "CrashProtocol.h"
#include "ImuLabProtocol.h"
#include "../power/BatteryProtocol.h"

static_assert(SensorConfig::MAX_PENDING_EVENTS == CrashProtocol::SENSOR_OUTBOX_CAPACITY,
              "Change outbox schema deliberately");
class EventLink {
 public:
  bool begin();
  // Caller supplies live readings; Durable, IDs, pending and uptime are owned here.
  void tick(CrashProtocol::Frame telemetry);
  // Caller supplies detection uptime. Success includes RAM-only queueing;
  // writable must also be checked to know the event survived a power loss.
  bool enqueue(CrashProtocol::Frame frame);
  uint8_t pending() const { return journal.count; }
  bool writable = false, corrupt = false, started = false;
  std::atomic<uint32_t> fifoOverflow{0}, rxBufferFull{0}, framingErrors{0}, parityErrors{0}, breaks{0};
  std::atomic<uint32_t> labTxDeferred{0}, labTxDropped{0};
  uint32_t overflow = 0, rejectedEvents = 0, txFrames = 0, txDeferred = 0;
  uint32_t rxBytes = 0; // loop-task owned: independent of sensor-health flags
  bool labActive() const { return labSessionCurrent(labSession.load()); }
  bool automaticPeerCurrent() const {return automaticPeer.load() && labActive();}
  bool automaticEventReady() const {return automaticPeerCurrent() && deliveryReady.load();}
  uint32_t lastEnqueuedSequence=0, lastAckSequence=0, lastAckDecisionAgeMs=UINT32_MAX;
  uint32_t heartbeatTransmissions = 0;
  uint32_t acknowledgements = 0, ignoredAcks = 0, saveFailures = 0;
  uint32_t rejectedFrames() const { return parser.rejected; }
  uint32_t bootId() const { return journal.boot ? journal.boot : 1; }
  using LabHandler = void (*)(void*, const ImuLabProtocol::Frame&);
  void attachLab(void* context, LabHandler handler) { labContext = context; labHandler = handler; }
  bool labSessionCurrent(uint32_t session)const {return session && session==labSession.load() && uint32_t(millis()-labHelloAt.load())<2500;}
  bool sendLab(const ImuLabProtocol::Frame& frame);
  bool sendBattery(const BatteryProtocol::Reading& reading);
  bool replayBlocked() const {
    return journal.count &&
      ((journal.events[0].type == CrashProtocol::Type::Test && !SensorConfig::ENABLE_SYSTEM_UART_TEST) ||
       (journal.events[0].type == CrashProtocol::Type::AutomaticCrash &&
        (!SensorConfig::ARM_CRASH_DETECTION || !automaticPeerCurrent())));
  }

 private:
  struct Journal {
    uint32_t boot = 0, nextSequence = 1;
    uint8_t count = 0;
    CrashProtocol::Frame events[SensorConfig::MAX_PENDING_EVENTS];
  } journal;
  HardwareSerial uart{2};
  Preferences preferences;
  ImuLabProtocol::Parser parser;
  void* labContext = nullptr;
  LabHandler labHandler = nullptr;
  bool opened = false, heartbeatSent = false;
  uint32_t lastHeartbeat = 0, lastEvent = 0, lastSave = 0;
  static constexpr size_t JOURNAL_SIZE =
      12 + SensorConfig::MAX_PENDING_EVENTS * CrashProtocol::FRAME_SIZE + 2;
  std::atomic<uint32_t> labSession{0}, labHelloAt{0};
  std::atomic<bool> automaticPeer{false};
  std::atomic<bool> deliveryReady{false};
#ifdef ARDUINO_ARCH_ESP32
  struct QueuedLab {ImuLabProtocol::Frame frame;uint32_t at;};
  QueueHandle_t labTx=nullptr;
#endif
  void pumpLab();
  bool save();
  bool send(const CrashProtocol::Frame& frame);
};
