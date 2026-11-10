#include <cassert>
#include <iostream>
#include "src/link/EventLink.h"
#include "SensorLink.h"
#include "src/sensing/PiezoInputs.h"

using namespace CrashProtocol;
void pump(EventLink& sensor, SensorLink& main, Frame live) {
  sensor.tick(live); transfer(2, 0); main.tick(); transfer(0, 2); sensor.tick(live);
}
int main() {
  FakeNvs::reset(); clockMs = 100; EmergencyCoordinator coordinator;
  SensorLink main(coordinator); main.begin(); EventLink sensor; assert(sensor.begin());
  Frame live; live.flags = ImuReady; live.accelerationMg = 1035; live.rotationDps = 7;
  pump(sensor, main, live);
  auto t = main.telemetry(); assert(t.online && t.imu && !t.piezo && !t.armed && t.durable);
  assert(!main.armedAndHealthy());
  JsonDocument doc; main.writeJson(doc.to<JsonObject>());
  assert(doc["reportedAccelerationMg"].as<int>() == 1035);
  assert(doc["reportedRotationDps"].as<int>() == 7);
  Frame test; test.type = Type::Test; test.uptimeMs = 115;
  clockMs = 115; assert(sensor.enqueue(test)); clockMs = 123;
  pump(sensor, main, live); assert(sensor.pending() == 0 && coordinator.unique == 1);
  assert(coordinator.lastAgeKnown && coordinator.lastAge == 8);
  assert(main.telemetry().acknowledgements == 1);
  // Commit rejection in the main coordinator must produce no ACK.
  clockMs = 200; test.uptimeMs = clockMs; assert(sensor.enqueue(test));
  coordinator.commitOk = false; pump(sensor, main, live); assert(sensor.pending() == 1);
  coordinator.commitOk = true; clockMs += 1000;
  // Accept event durably, then lose its ACK. Retry uses same ID; no duplicate job.
  sensor.tick(live); transfer(2, 0); main.tick(); ports[0].tx.clear();
  const auto unique = coordinator.unique; assert(sensor.pending() == 1);
  clockMs += 1000; pump(sensor, main, live);
  assert(sensor.pending() == 0 && coordinator.unique == unique);
  // Main TX backpressure: accepted event retries until an ACK can be sent.
  clockMs += 10; test.uptimeMs = clockMs; assert(sensor.enqueue(test));
  ports[0].capacity = 0; pump(sensor, main, live); assert(sensor.pending() == 1 && main.telemetry().deferred > 0);
  ports[0].capacity = 512; clockMs += 1000; pump(sensor, main, live); assert(sensor.pending() == 0);
  // Actual main rejects an unarmed Crash frame, even if its CRC is valid.
  Frame crash; crash.type = Type::Crash; crash.boot = 123; crash.sequence = 1;
  uint8_t bytes[32]; encode(crash, bytes); const auto before = main.telemetry().rejected;
  for (auto b : bytes) ports[0].rx.push_back(b);
  main.tick();
  assert(main.telemetry().rejected == before + 1);
  // Heartbeats keep arriving even while an event remains unacknowledged.
  coordinator.commitOk = false; clockMs += 100; test.uptimeMs = clockMs; assert(sensor.enqueue(test));
  const auto heartbeats = main.telemetry().heartbeats;
  for (unsigned i = 0; i < 5; ++i) { clockMs += 1000; pump(sensor, main, live); assert(main.telemetry().online); }
  assert(sensor.pending() == 1 && main.telemetry().heartbeats >= heartbeats + 5);
  clockMs += 3500; main.tick(); assert(!main.telemetry().online);
  JsonDocument stale; main.writeJson(stale.to<JsonObject>());
  assert(stale["reportedAccelerationMg"].isNull());
  // Run the native reader's real code and send its group peaks through V1.
  PiezoInputs inputs; inputs.begin(); FakeAdc::raw[6] = 1200; FakeAdc::raw[7] = 3200;
  for (unsigned i = 0; i < 10; ++i) { clockMs += 5; inputs.tick(); }
  assert(inputs.healthy());
  live.flags |= PiezoReady; live.piezoFront = inputs.reportedFront(); live.piezoRear = inputs.reportedRear();
  pump(sensor, main, live); JsonDocument groups; main.writeJson(groups.to<JsonObject>());
  assert(groups["reportedPiezoFront"].as<int>() == 1200 && groups["reportedPiezoRear"].as<int>() == 3200);
  assert(main.telemetry().online && main.telemetry().piezo && !main.armedAndHealthy());
  std::cout << "PASS actual main SensorLink: heartbeat/API fields, age, durable-accept gate, ACK loss/dedup, TX backpressure, unarmed rejection, liveness/stale API\n";
}
