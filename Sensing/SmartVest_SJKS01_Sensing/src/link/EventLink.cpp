#include "EventLink.h"
#include <esp_system.h>
#include <cstring>

bool EventLink::save() {
  lastSave = millis();
  if (!opened) {deliveryReady=false;return false;}
  uint8_t bytes[JOURNAL_SIZE] = {}, readback[JOURNAL_SIZE] = {};
  bytes[0] = 'O'; bytes[1] = 'B'; bytes[2] = 1; bytes[3] = journal.count;
  CrashProtocol::put32(bytes + 4, journal.boot);
  CrashProtocol::put32(bytes + 8, journal.nextSequence);
  for (size_t i = 0; i < journal.count; ++i)
    CrashProtocol::encode(journal.events[i], bytes + 12 + i * CrashProtocol::FRAME_SIZE);
  CrashProtocol::put16(bytes + JOURNAL_SIZE - 2,
                       CrashProtocol::crc16(bytes, JOURNAL_SIZE - 2));
  writable = preferences.putBytes("outbox", bytes, sizeof(bytes)) == sizeof(bytes) &&
             preferences.getBytesLength("outbox") == sizeof(bytes) &&
             preferences.getBytes("outbox", readback, sizeof(readback)) == sizeof(readback) &&
             std::memcmp(bytes, readback, sizeof(bytes)) == 0;
  if (!writable) ++saveFailures;
  deliveryReady=writable&&journal.count<SensorConfig::MAX_PENDING_EVENTS;
  return writable;
}
bool EventLink::begin() {
  if (started) return opened && !corrupt;
  started = true;
#ifdef ARDUINO_ARCH_ESP32
  labTx=xQueueCreate(4,sizeof(QueuedLab));
#endif
  uart.setRxBufferSize(1024);
  uart.setTxBufferSize(512);
  uart.begin(SensorConfig::LINK_BAUD, SERIAL_8N1,
             SensorConfig::MAIN_RX_PIN, SensorConfig::MAIN_TX_PIN);
  uart.onReceiveError([this](hardwareSerial_error_t error) {
    switch (error) {
      case UART_FIFO_OVF_ERROR: ++fifoOverflow; break;
      case UART_BUFFER_FULL_ERROR: ++rxBufferFull; break;
      case UART_FRAME_ERROR: ++framingErrors; break;
      case UART_PARITY_ERROR: ++parityErrors; break;
      case UART_BREAK_ERROR: ++breaks; break;
      default: break;
    }
  });
  opened = preferences.begin("sv_sensor", false);
  if (!opened) return false;
  const size_t size = preferences.getBytesLength("outbox");
  uint8_t bytes[JOURNAL_SIZE] = {};
  bool valid = size == 0;
  uint32_t previousBoot = 0;
  if (size == sizeof(bytes) &&
      preferences.getBytes("outbox", bytes, sizeof(bytes)) == sizeof(bytes) &&
      bytes[0] == 'O' && bytes[1] == 'B' && bytes[2] == 1 &&
      bytes[3] <= SensorConfig::MAX_PENDING_EVENTS &&
      CrashProtocol::get16(bytes + JOURNAL_SIZE - 2) ==
          CrashProtocol::crc16(bytes, JOURNAL_SIZE - 2)) {
    valid = true;
    previousBoot = CrashProtocol::get32(bytes + 4);
    journal.count = bytes[3];
    for (size_t i = 0; i < journal.count; ++i) {
      auto& f = journal.events[i];
      if (!CrashProtocol::decode(bytes + 12 + i * CrashProtocol::FRAME_SIZE, f) ||
          !f.sequence || (f.type != CrashProtocol::Type::Crash && f.type != CrashProtocol::Type::Test && f.type != CrashProtocol::Type::AutomaticCrash))
        valid = false;
      for (size_t j = 0; j < i; ++j)
        if (journal.events[j].boot == f.boot && journal.events[j].sequence == f.sequence)
          valid = false;
    }
  }
  if (!valid) {
    corrupt = true; opened = false; journal = Journal();
    return false;  // Keep corrupt bytes intact for diagnosis. Never auto-erase.
  }
  // Avoid collision with this journal's previous boot and all retained events.
  // V1's 32-bit random boot identity is not a global uniqueness guarantee.
  for (unsigned tries = 0; tries < 16; ++tries) {
    uint32_t candidate = esp_random();
    bool collision = !candidate || candidate == previousBoot;
    for (size_t i = 0; i < journal.count; ++i)
      collision |= candidate == journal.events[i].boot;
    if (!collision) { journal.boot = candidate; break; }
  }
  if (!journal.boot) { opened = false; return false; }
  journal.nextSequence = 1;
  lastEvent = millis() - SensorConfig::EVENT_RETRY_MS;
  save();
  return true;  // A flash error remains visible in writable/saveFailures.
}
bool EventLink::send(const CrashProtocol::Frame& frame) {
  if (uart.availableForWrite() < int(CrashProtocol::FRAME_SIZE)) {
    ++txDeferred; return false;
  }
  uint8_t bytes[CrashProtocol::FRAME_SIZE];
  CrashProtocol::encode(frame, bytes);
  if (uart.write(bytes, sizeof(bytes)) != sizeof(bytes)) {
    ++txDeferred; return false;
  }
  ++txFrames;
  return true;
}
bool EventLink::enqueue(CrashProtocol::Frame frame) {
  const uint8_t required = CrashProtocol::ImuReady | CrashProtocol::PiezoReady | CrashProtocol::Armed;
  if ((frame.type != CrashProtocol::Type::Crash && frame.type != CrashProtocol::Type::Test && frame.type != CrashProtocol::Type::AutomaticCrash) ||
      (frame.flags & ~uint8_t(15)) ||
      (frame.type == CrashProtocol::Type::Test && !SensorConfig::ENABLE_SYSTEM_UART_TEST) ||
      (frame.type == CrashProtocol::Type::AutomaticCrash && (!SensorConfig::ARM_CRASH_DETECTION || !automaticPeerCurrent())) ||
      ((frame.type == CrashProtocol::Type::Crash || frame.type == CrashProtocol::Type::AutomaticCrash) && (frame.flags & required) != required)) {
    ++rejectedEvents; return false;
  }
  if (!started || !opened || !journal.boot || !journal.nextSequence ||
      journal.count >= SensorConfig::MAX_PENDING_EVENTS) {
    ++overflow; return false;
  }
  frame.boot = journal.boot;
  frame.sequence = journal.nextSequence++;
  journal.events[journal.count++] = frame;
  lastEnqueuedSequence=frame.sequence;
  save();  // Retain RAM copy if flash fails; do not claim durability.
  lastEvent = millis() - SensorConfig::EVENT_RETRY_MS;
  return true;
}
void EventLink::tick(CrashProtocol::Frame telemetry) {
  if (!started) return;
  CrashProtocol::Frame frame;
  ImuLabProtocol::Frame lab;
  if (opened && !writable && uint32_t(millis() - lastSave) >= 5000) save();
  int budget = 128;
  while (uart.available() && budget-- > 0) {
    const uint8_t incoming = uint8_t(uart.read());
    ++rxBytes;
    const uint8_t kind = parser.feed(incoming, millis(), frame, lab);
    if (kind == 2) {
      if(lab.boot!=bootId())continue;
      if(lab.type==ImuLabProtocol::Type::Hello && lab.length==0){automaticPeer=lab.token==ImuLabProtocol::AUTOMATIC_CRASH_HELLO_TOKEN;labSession=lab.session;labHelloAt=millis();continue;}
      if(lab.session==labSession.load() && uint32_t(millis()-labHelloAt.load())<2500 && labHandler)labHandler(labContext,lab);
      continue;
    }
    if (kind != 1) continue;
    if (frame.type != CrashProtocol::Type::Ack || !(frame.flags & CrashProtocol::Durable) ||
        !journal.count || frame.boot != journal.events[0].boot ||
        frame.sequence != journal.events[0].sequence) {
      ++ignoredAcks; continue;
    }
    Journal before = journal;
    for (uint8_t i = 1; i < journal.count; ++i) journal.events[i - 1] = journal.events[i];
    --journal.count;
    if (!save()) {
      // Only the ACK-mutated fields are rolled back. Boot identity is immutable
      // after begin() and is also read by the acquisition task.
      journal.count = before.count;
      for (uint8_t i = 0; i < before.count; ++i) journal.events[i] = before.events[i];
    }
    else {++acknowledgements;lastAckSequence=frame.sequence;
      lastAckDecisionAgeMs=frame.boot==journal.boot?uint32_t(millis()-before.events[0].uptimeMs):UINT32_MAX;}
    lastEvent = millis() - SensorConfig::EVENT_RETRY_MS;
  }
  const uint32_t now = millis();
  // Heartbeat first: main can estimate trigger age even for the first event.
  if (!heartbeatSent || uint32_t(now - lastHeartbeat) >= SensorConfig::HEARTBEAT_MS) {
    telemetry.type = CrashProtocol::Type::Heartbeat;
    telemetry.boot = journal.boot ? journal.boot : 1;
    telemetry.sequence = 0; telemetry.uptimeMs = now; telemetry.pending = journal.count;
    telemetry.flags &= uint8_t(CrashProtocol::ImuReady | CrashProtocol::PiezoReady | CrashProtocol::Armed);
    if (writable) telemetry.flags |= CrashProtocol::Durable;
    if (send(telemetry)) { heartbeatSent = true; lastHeartbeat = now; ++heartbeatTransmissions; }
  }
  if (heartbeatSent && journal.count && !replayBlocked() &&
      uint32_t(now - lastEvent) >= SensorConfig::EVENT_RETRY_MS)
    if (send(journal.events[0])) lastEvent = now;
  pumpLab();
}
bool EventLink::sendBattery(const BatteryProtocol::Reading& reading){
  // Loop-owned, lossy latest-value telemetry. Crash delivery always wins;
  // battery packets never enter the durable event journal or worker queue.
  if(!started||!labActive())return false;
  // Service a due incident first, but do not starve telemetry while its ACK
  // is delayed. Keep room to enqueue the next urgent incident frame.
  if(pending()&&uint32_t(millis()-lastEvent)>=SensorConfig::EVENT_RETRY_MS)return false;
  ImuLabProtocol::Frame f;f.type=ImuLabProtocol::Type::Battery;
  f.boot=bootId();f.session=labSession.load();f.length=BatteryProtocol::SIZE;
  BatteryProtocol::encode(reading,f.data);
  uint8_t bytes[ImuLabProtocol::MAX_FRAME];const size_t n=ImuLabProtocol::encode(f,bytes);
  const size_t reserve=pending()?CrashProtocol::FRAME_SIZE:0;
  return n&&uart.availableForWrite()>=int(n+reserve)&&uart.write(bytes,n)==n;
}
bool EventLink::sendLab(const ImuLabProtocol::Frame& frame) {
  const uint32_t session=labSession.load();
  if(frame.session && frame.session!=session)return false;
  if(!started || !session || uint32_t(millis()-labHelloAt.load())>=2500)return false;
  ImuLabProtocol::Frame out=frame;out.session=session;
#ifdef ARDUINO_ARCH_ESP32
  QueuedLab q{out,millis()};const bool queued=labTx && xQueueSend(labTx,&q,0)==pdTRUE;if(!queued)++labTxDeferred;return queued;
#else
  uint8_t bytes[ImuLabProtocol::MAX_FRAME];size_t size=ImuLabProtocol::encode(out,bytes);
  return size && uart.availableForWrite()>=int(size) && uart.write(bytes,size)==size;
#endif
}
void EventLink::pumpLab(){
#ifdef ARDUINO_ARCH_ESP32
  if(!labTx)return;
  QueuedLab q;if(xQueuePeek(labTx,&q,0)!=pdTRUE)return;
  if(q.frame.session!=labSession.load() || uint32_t(millis()-labHelloAt.load())>=2500 ||
     ((q.frame.type==ImuLabProtocol::Type::Live||q.frame.type==ImuLabProtocol::Type::Piezo) && uint32_t(millis()-q.at)>100)){
    xQueueReceive(labTx,&q,0);++labTxDropped;return;
  }
  uint8_t bytes[ImuLabProtocol::MAX_FRAME];size_t size=ImuLabProtocol::encode(q.frame,bytes);
  if(size && uart.availableForWrite()>=int(size)){
    xQueueReceive(labTx,&q,0);uart.write(bytes,size);
  }
#endif
}
