#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include "src/link/EventLink.h"
#include "src/sensing/Imu.h"
#include "src/sensing/GyroCalibration.h"
#include "src/sensing/CalibrationStore.h"
#include "bmi323_fake.h"

using namespace CrashProtocol;
Frame event() { Frame f; f.type = Type::Test; f.uptimeMs = millis(); f.accelerationMg = 1200; return f; }
Frame firstEvent() {
  Frame f;
  assert(ports[2].tx.size() >= 64);
  assert(decode(ports[2].tx.data() + 32, f)); return f;
}
void ack(const Frame& eventFrame, bool durable = true) {
  auto f = eventFrame; f.type = Type::Ack; f.flags = durable ? Durable : 0;
  uint8_t bytes[32]; encode(f, bytes);
  for (auto b : bytes) ports[2].rx.push_back(b);
}
void reset() { FakeNvs::reset(); ports = {}; clockMs = 0; }
void linkTests() {
  reset(); EventLink link; assert(link.begin()); assert(link.writable);
  ports[2].errorHandler(UART_FIFO_OVF_ERROR); ports[2].errorHandler(UART_BUFFER_FULL_ERROR);
  ports[2].errorHandler(UART_FRAME_ERROR); ports[2].errorHandler(UART_PARITY_ERROR); ports[2].errorHandler(UART_BREAK_ERROR);
  assert(link.fifoOverflow == 1 && link.rxBufferFull == 1 && link.framingErrors == 1 && link.parityErrors == 1 && link.breaks == 1);
  Frame live; live.flags = ImuReady; live.accelerationMg = 1020; live.rotationDps = 5;
  live.piezoFront = 111; live.piezoRear = 222;
  clockMs = 22; auto f = event(); assert(link.enqueue(f)); clockMs = 40; link.tick(live);
  Frame h; assert(decode(ports[2].tx.data(), h)); assert(h.type == Type::Heartbeat);
  assert(h.accelerationMg == 1020 && h.rotationDps == 5 && h.piezoFront == 111 && h.piezoRear == 222);
  assert(h.flags == (ImuReady | Durable) && h.pending == 1);
  f = firstEvent(); assert(f.uptimeMs == 22 && f.sequence == 1 && f.boot != 0);
  ports[2].tx.clear(); clockMs += 1000; link.tick(live);
  Frame retry = firstEvent(); assert(retry.boot == f.boot && retry.sequence == f.sequence && retry.uptimeMs == 22);
  ack(f, false); link.tick(live); assert(link.pending() == 1);
  auto wrong = f; ++wrong.sequence; ack(wrong); link.tick(live); assert(link.pending() == 1);
  const uint32_t fixedBoot = link.bootId();
  FakeNvs::failWrite = true; ack(f); link.tick(live); assert(link.pending() == 1 && !link.writable);
  assert(link.bootId() == fixedBoot && link.lastEnqueuedSequence == f.sequence);
  ports[2].tx.clear(); clockMs += 1000; link.tick(live);
  const auto failedRemovalRetry = firstEvent();
  assert(failedRemovalRetry.boot == fixedBoot && failedRemovalRetry.sequence == f.sequence);
  FakeNvs::failWrite = false; ack(f); link.tick(live); assert(link.pending() == 0 && link.writable && link.acknowledgements == 1);
  assert(link.bootId() == fixedBoot && link.enqueue(event()));
  assert(link.lastEnqueuedSequence == f.sequence + 1);
  ports[2].tx.clear(); link.tick(live);
  Frame afterFailedRemoval;
  assert(decode(ports[2].tx.data(), afterFailedRemoval));
  assert(afterFailedRemoval.boot == fixedBoot && afterFailedRemoval.sequence == f.sequence + 1);
  ack(afterFailedRemoval); link.tick(live); assert(link.pending() == 0);
  EventLink reboot; assert(reboot.begin()); assert(reboot.pending() == 0);
  assert(reboot.enqueue(event())); ports[2].tx.clear(); reboot.tick(live); auto persisted = firstEvent();
  EventLink nextBoot; assert(nextBoot.begin()); assert(nextBoot.pending() == 1);
  ports[2].tx.clear(); nextBoot.tick(live); auto replay = firstEvent();
  assert(replay.boot == persisted.boot && replay.sequence == persisted.sequence);
  assert(nextBoot.enqueue(event()) && nextBoot.enqueue(event()) && nextBoot.enqueue(event()));
  assert(nextBoot.pending() == 4 && !nextBoot.enqueue(event()) && nextBoot.overflow == 1);
  ports[2].tx.clear(); ports[2].capacity = 0; clockMs += 1000;
  nextBoot.tick(live); assert(ports[2].tx.empty() && nextBoot.txDeferred > 0);
  ports[2].capacity = 512; nextBoot.tick(live); assert(ports[2].tx.size() == 64);
  reset(); FakeNvs::failWrite = true; EventLink ramOnly; assert(ramOnly.begin());
  assert(!ramOnly.writable && ramOnly.enqueue(event()) && ramOnly.pending() == 1);
  FakeNvs::failWrite = false; clockMs = 5000; ramOnly.tick(live); assert(ramOnly.writable);
  reset(); EventLink readback; assert(readback.begin()); FakeNvs::corruptReadback = true;
  assert(readback.enqueue(event()) && !readback.writable);
  FakeNvs::corruptReadback = false; clockMs = 5000; readback.tick(live); assert(readback.writable);
  reset(); FakeNvs::blobs["sv_sensor/outbox"] = {1,2,3}; const auto original = FakeNvs::blobs;
  EventLink corrupt; assert(!corrupt.begin() && corrupt.corrupt && !corrupt.writable && !corrupt.enqueue(event()));
  assert(FakeNvs::blobs == original);
  reset(); EventLink validate; assert(validate.begin()); auto bad = event(); bad.type = Type::Ack;
  assert(!validate.enqueue(bad)); bad.type = Type::Crash; bad.flags = 0; assert(!validate.enqueue(bad));
  // Duplicate IDs in a CRC-valid old journal fail closed.
  assert(validate.enqueue(event()) && validate.enqueue(event()));
  auto& bytes = FakeNvs::blobs["sv_sensor/outbox"];
  std::copy(bytes.begin()+12, bytes.begin()+44, bytes.begin()+44);
  put16(bytes.data()+bytes.size()-2, crc16(bytes.data(), bytes.size()-2));
  EventLink duplicates; assert(!duplicates.begin() && duplicates.corrupt);
  reset(); clockMs = UINT32_MAX-400; EventLink wrap; assert(wrap.begin()); assert(wrap.enqueue(event()));
  wrap.tick(live); auto atWrap = firstEvent(); ports[2].tx.clear(); clockMs += 1000; wrap.tick(live);
  assert(firstEvent().sequence == atWrap.sequence);
  std::cout << "PASS outbox: live heartbeat, event uptime, retry IDs, ACK policy, NVS failures/readback, restart, capacity, corruption, timer wrap\n";
}
void parserTests() {
  Frame f; f.boot = 1; f.accelerationMg = 1000;
  uint8_t bytes[32]; encode(f, bytes); Parser parser; Frame decoded;
  for (unsigned i = 0; i < 31; ++i) assert(!parser.feed(bytes[i], 1, decoded));
  assert(!parser.feed(bytes[31], 101, decoded));
  bool accepted = false; for (auto b : bytes) accepted |= parser.feed(b, 102, decoded); assert(accepted);
  bytes[20] ^= 1; accepted = false; for (auto b : bytes) accepted |= parser.feed(b, 103, decoded); assert(!accepted);
  encode(f, bytes); accepted = false; for (auto b : bytes) accepted |= parser.feed(b, 104, decoded); assert(accepted);
  // Feed arbitrary noise into the actual fixed-size parser (sanitizers optional in the runner).
  std::mt19937 rng(7); for (unsigned i = 0; i < 100000; ++i) parser.feed(uint8_t(rng()), i / 100, decoded);
  std::cout << "PASS V1 parser: fragmentation, gap expiry, CRC, resynchronization, 100000 noise bytes\n";
}

void imuTests(){
 reset();FakeBMI323 fake;fake.automatic=true;Imu imu;imu.begin();clockMs+=5;imu.tick();assert(imu.healthy()&&imu.fresh&&imu.whoAmI==0x43);assert(imu.sample.accel[2]==1);assert(std::fabs(imu.sample.gyro[0]-16/16.384f)<1e-6);fake.checkProtocol();
 const auto n=imu.samples;imu.tick();assert(!imu.fresh&&imu.samples==n);clockMs+=5;imu.tick();assert(imu.fresh&&imu.sample.deltaUs==5000);
 fake.allHigh=true;clockMs+=5;imu.tick();assert(!imu.fresh&&imu.continuityLost);clockMs+=100;imu.tick();assert(!imu.healthy());
 fake.allHigh=false;clockMs+=5000;imu.tick();clockMs+=5;imu.tick();assert(imu.healthy());
 fake.accel={31744,0,2048};fake.gyro={31949,0,0};clockMs+=5;imu.tick();assert(imu.fresh&&imu.sample.accelClipped&&imu.sample.gyroClipped);fake.accel={0,0,2048};fake.gyro={16,-32,8};clockMs+=5;imu.tick();assert(!imu.sample.saturated);
 CalibrationStore store;float b[3]={1,-2,.5};assert(store.save(0x43,b));CalibrationStore loaded;loaded.load(0x43);assert(loaded.stored&&loaded.valid);assert(loaded.bias[0]==1);FakeNvs::corruptReadback=true;assert(!store.save(0x43,b));FakeNvs::corruptReadback=false;
 GyroCalibration cal;cal.start(clockMs);const auto origin=micros();for(unsigned i=0;i<240;i++){const auto next=origin+(i+1)*5000;clockMs=next/1000;testExtraUs=next%1000;imu.tick();if(imu.fresh)cal.feed(imu.sample);}assert(cal.result==GyroCalibration::Result::Complete);
 cal.start(clockMs);fake.mode=1;const auto origin2=micros();for(unsigned i=0;i<240;i++){const auto next=origin2+(i+1)*5000;clockMs=next/1000;testExtraUs=next%1000;imu.tick();if(imu.fresh)cal.feed(imu.sample);}assert(cal.result==GyroCalibration::Result::Motion);
 for(int mode:{2,3}){GyroCalibration capture;capture.start(clockMs);fake.mode=mode;auto at=micros();for(unsigned i=0;i<240;i++){auto now=at+(i+1)*5000;clockMs=now/1000;testExtraUs=now%1000;imu.tick();if(imu.fresh)capture.feed(imu.sample);}assert(capture.result==GyroCalibration::Result::Motion);}
 GyroCalibration gap;gap.start(clockMs);ImuSample sample;sample.accel[2]=1;sample.sequence=1;gap.feed(sample);sample.sequence=2;sample.deltaUs=10000;gap.feed(sample);assert(gap.result==GyroCalibration::Result::Gap);
 SPI.beginOK=false;Imu noBus;noBus.begin();assert(noBus.error==Imu::Error::Start&&!noBus.healthy());SPI.beginOK=true;
 CalibrationStore wrong;assert(!wrong.load(0x71)&&!wrong.valid);
 std::cout<<"PASS BMI SPI/profile/freshness/rejection/recovery/calibration/storage isolation\n";
}
int main(){linkTests();parserTests();imuTests();}
