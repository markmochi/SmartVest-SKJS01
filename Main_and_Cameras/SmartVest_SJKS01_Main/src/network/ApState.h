#pragma once
#include <cstdint>
// Observed driver configuration, not proof of over-the-air beacon reception.
struct ApState {
  bool readable=false, visible=false, configured=false, dhcp=false;
  uint8_t channel=0, protocol=0, limit=0;
  int8_t txPowerQuarterDbm=-128; // Driver limit, not measured RF output; -128 = unavailable.
  int32_t error=0;
  uint32_t recoveryAttempts=0, lastStartMs=0, lastStopMs=0;
  uint16_t disconnectReason=0;
  uint32_t serviceRecoveries=0,authFailures=0;
};
