#pragma once

#include <Arduino.h>
#include "../../Config.h"

// Simple data structures shared by multiple modules.

struct Contact {
  String name;
  String phone;
};

struct SmsJob {
  uint32_t id = 0;
  uint32_t eventBoot = 0;
  uint32_t eventSequence = 0;
  uint8_t recipientIndex = 0;
  String phone;
  String text;
  String kind;
  String state;
  String detail;
  uint8_t attempts = 0;
  int modemReference = -1;
  bool reportEligible = false;
  bool deliveryRequested = false;
  int deliveryStatus = -1;
  String carrierReport;
  uint32_t created = 0;
  uint32_t changed = 0;
};

// Compact login snapshot for both roles; retained name preserves source compatibility.
struct RiderCredentials {
  String adminUsername = Config::DEVELOPER_USERNAME;
  String adminSalt, adminHash;
  String salt;
  String riderUsername = "admin";
  String passwordHash;
  bool defaultPassword = true;
};

struct DeviceState : RiderCredentials {
  // Share the large String-array lifecycle instead of expanding it at each
  // settings transaction. Defaulted operations retain normal value semantics.
  __attribute__((noinline)) DeviceState() = default;
  __attribute__((noinline)) DeviceState(const DeviceState&) = default;
  __attribute__((noinline)) DeviceState(DeviceState&&) = default;
  __attribute__((noinline)) DeviceState& operator=(const DeviceState&) = default;
  __attribute__((noinline)) DeviceState& operator=(DeviceState&&) = default;
  __attribute__((noinline)) ~DeviceState() = default;
  bool dataResetPending = false;
  bool riderSetupComplete = false;
  uint32_t revision = 0;
  uint32_t nextJob = 1;

  String rider;
  Contact contacts[Config::MAX_CONTACTS];
  bool notifyAdded = true;

  String cameraKeys[2];
  uint32_t riderAccountResetRevision = 0;

  SmsJob jobs[Config::MAX_SMS_JOBS];
};
