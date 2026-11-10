#include <cassert>
#include <iostream>
#include "src/sensing/PiezoInputs.h"

void settle(PiezoInputs& piezo) {
  for (unsigned i = 0; i < 10; ++i) { clockMs += 5; piezo.tick(); }
}
int main() {
  FakeAdc::reset(); clockMs = 0;
  {
    PiezoInputs p; p.begin(); assert(p.initialized && !p.healthy());
    FakeAdc::raw[6] = 1100; FakeAdc::raw[7] = 2200; settle(p);
    assert(p.healthy() && p.frontRaw == 1100 && p.rearRaw == 2200);
    assert(p.reportedFront() == 1100 && p.reportedRear() == 2200);
    const auto count = p.samples; p.tick(); assert(!p.fresh && p.samples == count);
    FakeAdc::raw[6] = 10; FakeAdc::raw[7] = 20; clockMs += 5; p.tick();
    assert(p.frontRaw == 10 && p.frontPeak == 1100 && p.rearPeak == 2200);
    p.clearPeaks(); clockMs += 5; p.tick(); assert(p.frontPeak == 10 && p.rearPeak == 20);
    FakeAdc::readError[7] = ESP_ERR_TIMEOUT; clockMs += 5; p.tick();
    assert(!p.healthy() && !p.fresh && p.frontRaw == 0 && p.rearRaw == 0 && p.frontPeak == 0 && p.failures == 1);
    FakeAdc::readError[7] = ESP_OK; settle(p); assert(p.healthy());
    clockMs += 100; assert(!p.healthy() && p.reportedFront() == 0); p.tick();
    assert(p.goodPairs == 1 && !p.healthy() && p.overruns > 0); settle(p); assert(p.healthy());
    FakeAdc::raw[6] = 4095; clockMs += 5; p.tick(); assert(p.clippedPairs == 1 && p.frontRaw == 4095);
    FakeAdc::raw[6] = 4096; clockMs += 5; p.tick(); assert(!p.healthy() && p.lastError == ESP_ERR_INVALID_RESPONSE);
    FakeAdc::raw[6] = 0; settle(p); assert(p.healthy());
  }
  assert(FakeAdc::allocations == 1 && FakeAdc::deletions == 1);
  FakeAdc::reset(); clockMs = 0;
  {
    FakeAdc::configError = ESP_ERR_INVALID_ARG; PiezoInputs p; p.begin();
    assert(!p.initialized && p.failures == 1 && FakeAdc::deletions == 1);
    FakeAdc::configError = ESP_OK; clockMs = 4999; p.tick(); assert(!p.initialized);
    clockMs = 5000; p.tick(); assert(p.initialized); settle(p); assert(p.healthy());
  }
  FakeAdc::reset(); clockMs = 0;
  {
    FakeAdc::allocationError = ESP_ERR_TIMEOUT; PiezoInputs p; p.begin(); assert(!p.initialized);
    FakeAdc::allocationError = ESP_OK; clockMs = 5000; p.tick(); assert(p.initialized);
  }
  FakeAdc::reset(); clockMs = UINT32_MAX - 30;
  {
    PiezoInputs p; p.begin(); settle(p); assert(p.healthy());
  }
  std::cout << "PASS native group ADC: two ADC1 channels, qualification, fresh sampling, heartbeat peaks, read fault/stale recovery, clipping, range errors, resource cleanup, init retry, timer wrap\n";
}
