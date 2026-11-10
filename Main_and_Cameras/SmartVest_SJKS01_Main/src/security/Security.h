#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "../storage/StateStore.h"
#include "Access.h"

/* Dashboard login/session helper. */
class Security {
 public:
  struct Session {
    String token;
    String csrf;
    uint32_t started = 0;
    Access::Role role = Access::Role::Rider;
  };

  Session sessions[Config::MAX_SESSIONS];
  uint8_t failures = 0;
  uint32_t lastFailure = 0;

  static String randomHex(size_t bytes);
  static String hashPassword(const String& password, const String& salt);
  static bool equal(const String& a, const String& b);
  static bool factoryRiderAccount(const RiderCredentials& state);
  static bool verifyRiderPassword(const String& password, const RiderCredentials& state);
  static bool verifyAdminPassword(const String& password, const RiderCredentials& state);
  static bool validNewRiderPassword(const String& password);

  Session* current(WebServer& web);
  Session* login(const String& username, const String& password,
                 const RiderCredentials& state);
  Session* finishLogin(bool valid,bool developer);

  bool throttled() const;
  void clear();
  void clearRiderSessions();
};
