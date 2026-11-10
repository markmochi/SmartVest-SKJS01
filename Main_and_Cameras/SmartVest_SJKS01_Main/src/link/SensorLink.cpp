#include "SensorLink.h"

#include "../../Config.h"
void SensorLink::begin() {
  uart.setRxBufferSize(2048);
  uart.setTxBufferSize(512);
  uart.begin(Config::SENSOR_BAUD, SERIAL_8N1, Config::SENSOR_RX_PIN,
             Config::SENSOR_TX_PIN);
  uart.onReceiveError([this](hardwareSerial_error_t error) { uartHealth.observe(error); });
}
bool SensorLink::armedAndHealthy() const {
  const uint8_t required = CrashProtocol::ImuReady | CrashProtocol::PiezoReady |
                           CrashProtocol::Armed | CrashProtocol::Durable;
  return Config::ARM_CRASH_DETECTION && Config::ENABLE_SENSOR_LINK && seen && coordinator.readyForCrash() &&
         uint32_t(millis() - lastSeen) < Config::SENSOR_OFFLINE_MS &&
         (heartbeat.flags & required) == required &&
         heartbeat.pending < CrashProtocol::SENSOR_OUTBOX_CAPACITY;
}
void SensorLink::tick() {
  if (!Config::ENABLE_SENSOR_LINK) return;
  CrashProtocol::Frame frame;
  ImuLabProtocol::Frame lab;
  int budget = 512;
  while (uart.available() && budget-- > 0) {
    const uint8_t byte = uint8_t(uart.read());
    ++rxBytes; lastByteAt = millis();
    const uint8_t kind = parser.feed(byte, millis(), frame, lab);
    if (kind == 2) { ++labMessagesReceived; receiveLab(lab); continue; }
    if (kind != 1) continue;
    ++frames;
    if (frame.type == CrashProtocol::Type::Heartbeat) {
      ++heartbeatFrames;
      if(seen && heartbeat.boot!=frame.boot){labSeen=metaSeen=commandPending=piezoSeen=false;piezoSequence=0;battery.reset();helloAt=millis()-1000;}
      heartbeat = frame;
      seen = true;
      lastSeen = millis();
      continue;
    }
    if (frame.type != CrashProtocol::Type::Crash &&
        frame.type != CrashProtocol::Type::Test && frame.type != CrashProtocol::Type::AutomaticCrash)
      continue;
    const uint8_t required = CrashProtocol::ImuReady |
                             CrashProtocol::PiezoReady | CrashProtocol::Armed;
    if (!frame.sequence ||
        ((frame.type == CrashProtocol::Type::Crash || frame.type == CrashProtocol::Type::AutomaticCrash) &&
         (frame.flags & required) != required) ||
        (frame.type == CrashProtocol::Type::Test &&
         !Config::ENABLE_BENCH_TESTS) ||
        ((frame.type == CrashProtocol::Type::Crash || frame.type == CrashProtocol::Type::AutomaticCrash) && !Config::ARM_CRASH_DETECTION)) {
      ++rejected;
      continue;
    }
    const uint32_t estimatedSensorNow =
        heartbeat.uptimeMs + uint32_t(millis() - lastSeen);
    const bool ageKnown =
        seen && heartbeat.boot == frame.boot &&
        uint32_t(millis() - lastSeen) < Config::SENSOR_OFFLINE_MS &&
        int32_t(estimatedSensorNow - frame.uptimeMs) >= 0;
    if (!coordinator.accept(
            frame, ageKnown,
            ageKnown ? uint32_t(estimatedSensorNow - frame.uptimeMs) : 0))
      continue;
    reportIncidentImpact();
    frame.type = CrashProtocol::Type::Ack;
    frame.flags = CrashProtocol::Durable;
    uint8_t bytes[CrashProtocol::FRAME_SIZE];
    CrashProtocol::encode(frame, bytes);
    if (uart.availableForWrite() < int(sizeof(bytes))) { ++ackDeferred; continue; }
    const size_t written = uart.write(bytes, sizeof(bytes));
    txBytes += written;
    if (written == sizeof(bytes)) ++accepted;
    else ++ackDeferred;
  }
  pumpLabCommand();
}
SensorLink::Telemetry SensorLink::telemetry() const {
  const uint32_t now = millis();
  const bool online = Config::ENABLE_SENSOR_LINK && seen && uint32_t(now-lastSeen) < Config::SENSOR_OFFLINE_MS;
  return {online, online && bool(heartbeat.flags & CrashProtocol::Armed),
          online && bool(heartbeat.flags & CrashProtocol::ImuReady),
          online && bool(heartbeat.flags & CrashProtocol::PiezoReady),
          online && bool(heartbeat.flags & CrashProtocol::Durable),
          rxBytes, frames, heartbeatFrames, accepted, rejected + parser.rejected, txBytes, ackDeferred,
          rxBytes ? uint32_t(now-lastByteAt) : UINT32_MAX,
          seen ? uint32_t(now-lastSeen) : UINT32_MAX, heartbeat.pending};
}
void SensorLink::writeJson(JsonObject out) {
  const auto t = telemetry();
  out["enabled"] = Config::ENABLE_SENSOR_LINK;
  out["online"] = t.online;
  out["twoWayConfirmed"] = labOnline();
  out["armed"] = t.armed;
  out["imuReady"] = t.imu;
  out["piezoConfigured"] = t.piezo;
  out["outboxWritable"] = t.durable;
  out["pending"] = t.pending;
  out["outboxCapacity"] = CrashProtocol::SENSOR_OUTBOX_CAPACITY;
  out["acknowledgements"] = t.acknowledgements;
  out["rejected"] = t.rejected;
  out["rxBytes"] = t.bytes;
  out["validFrames"] = t.frames;
  out["heartbeats"] = t.heartbeats;
  out["txBytes"] = t.transmitted;
  out["ackDeferred"] = t.deferred;
  out["rxAgeMs"] = nullptr;
  if (t.rxAge != UINT32_MAX) out["rxAgeMs"] = t.rxAge;
  out["heartbeatAgeMs"] = nullptr;
  if (t.heartbeatAge != UINT32_MAX) out["heartbeatAgeMs"] = t.heartbeatAge;
  out["uart"] = 0;
  uartHealth.writeJson(out["uartErrors"].to<JsonObject>());
  out["baud"] = Config::SENSOR_BAUD;
  out["rxPin"] = Config::SENSOR_RX_PIN;
  out["txPin"] = Config::SENSOR_TX_PIN;
  out["protocolBytes"] = CrashProtocol::FRAME_SIZE;
  out["boot"] = seen ? heartbeat.boot : 0;
  out["sensorUptimeMs"] = seen ? heartbeat.uptimeMs : 0;
  out["reportedAccelerationMg"] = nullptr;
  out["reportedRotationDps"] = nullptr;
  out["reportedPiezoFront"] = nullptr;
  out["reportedPiezoRear"] = nullptr;
  if (t.online) {
    out["reportedAccelerationMg"] = heartbeat.accelerationMg;
    out["reportedRotationDps"] = heartbeat.rotationDps;
    out["reportedPiezoFront"] = heartbeat.piezoFront;
    out["reportedPiezoRear"] = heartbeat.piezoRear;
  }
}
