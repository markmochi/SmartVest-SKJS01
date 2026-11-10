#pragma once

#include <ArduinoJson.h>
#include <Preferences.h>

#include "../core/Models.h"

/*
  StateStore
  ----------
  Saves rider settings, contacts, login data, camera keys, and SMS jobs in NVS.
  Dashboard changes survive a restart because this module owns persistence.
*/
class StateStore {
 public:
  DeviceState data;
  bool healthy = false; // Valid settings/credentials are loaded.
  bool writable = true;
  String lastError;

  bool begin();
  bool commit(const DeviceState& next);

  static int findFreeJobSlot(DeviceState& state);
  static bool enqueueSms(DeviceState& state,
                         const String& phone,
                         const String& text,
                         const String& kind);
  static void writeSettingsJson(const DeviceState& state, JsonObject output);

 private:
  bool applyRiderAccountReset();
  Preferences preferences;
  String persistedSettings;
  String persistedJobs[Config::MAX_SMS_JOBS];
};
