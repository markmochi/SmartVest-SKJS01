#include "Security.h"

#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <atomic>
#include <cstring>

#include "../../Config.h"
#include "../core/Domain.h"

namespace {
// Immutable proof from a completed factory-password derivation, normally
// established while settings load. Never trust the saved defaultPassword flag.
char factorySalt[33]{},factoryHash[65]{};
std::atomic<uint32_t> factoryProof{0};
bool factoryProven(const RiderCredentials& state){
  return factoryProof.load(std::memory_order_acquire)==2&&state.salt.length()==32&&state.passwordHash.length()==64&&
    !std::memcmp(factorySalt,state.salt.c_str(),33)&&!std::memcmp(factoryHash,state.passwordHash.c_str(),65);
}
}

String Security::randomHex(size_t bytes) {
  String output;
  output.reserve(bytes * 2);

  const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t value = esp_random();
    output += hex[value >> 4];
    output += hex[value & 15];
  }

  return output;
}

String Security::hashPassword(const String& password, const String& salt) {
  unsigned char output[32];

  const int result = mbedtls_pkcs5_pbkdf2_hmac_ext(
      MBEDTLS_MD_SHA256,
      reinterpret_cast<const unsigned char*>(password.c_str()),
      password.length(), reinterpret_cast<const unsigned char*>(salt.c_str()),
      salt.length(), 10000, sizeof(output), output);

  if (result) {
    return "";
  }

  const char hex[] = "0123456789abcdef";
  char hexOutput[65];
  for (size_t i = 0; i < sizeof(output); ++i) {
    hexOutput[i * 2] = hex[output[i] >> 4];
    hexOutput[i * 2 + 1] = hex[output[i] & 15];
  }
  hexOutput[64] = 0;
  return String(hexOutput);
}

bool Security::equal(const String& a, const String& b) {
  if (a.length() != b.length()) {
    return false;
  }

  volatile uint8_t difference = 0;
  for (size_t i = 0; i < a.length(); ++i) {
    difference |= a[i] ^ b[i];
  }

  return difference == 0;
}

bool Security::factoryRiderAccount(const RiderCredentials& state) {
  if (state.passwordHash.length() != 64) return false;
  if(factoryProven(state))return true;
  const bool factory=equal(hashPassword(Config::DEFAULT_RIDER_PASSWORD, state.salt), state.passwordHash) ||
         equal(hashPassword(Config::LEGACY_DEFAULT_PASSWORD, state.salt), state.passwordHash);
  uint32_t empty=0;
  if(factory&&state.salt.length()==32&&factoryProof.compare_exchange_strong(empty,1)){
    std::memcpy(factorySalt,state.salt.c_str(),33);std::memcpy(factoryHash,state.passwordHash.c_str(),65);
    factoryProof.store(2,std::memory_order_release);
  }
  return factory;
}

bool Security::verifyRiderPassword(const String& password, const RiderCredentials& state) {
  if (!password.length() || password.length() > 64 || state.passwordHash.length() != 64) return false;
  if(Config::ALLOW_DEFAULT_RIDER_LOGIN&&factoryProven(state)&&
      (equal(password,Config::DEFAULT_RIDER_PASSWORD)||equal(password,Config::LEGACY_DEFAULT_PASSWORD)))return true;
  if (equal(hashPassword(password, state.salt), state.passwordHash)) return true;
  if (!Config::ALLOW_DEFAULT_RIDER_LOGIN) return false;
  const char* alias = equal(password, Config::DEFAULT_RIDER_PASSWORD)
                          ? Config::LEGACY_DEFAULT_PASSWORD
                          : equal(password, Config::LEGACY_DEFAULT_PASSWORD)
                                ? Config::DEFAULT_RIDER_PASSWORD : nullptr;
  return alias && equal(hashPassword(alias, state.salt), state.passwordHash);
}

bool Security::validNewRiderPassword(const String& password) {
  return password.length() >= Config::RIDER_MIN_PASSWORD_LENGTH && password.length() <= 64 &&
         !equal(password, Config::DEFAULT_RIDER_PASSWORD) &&
         !equal(password, Config::LEGACY_DEFAULT_PASSWORD);
}

Security::Session* Security::current(WebServer& web) {
  const String cookie = web.header("Cookie");
  String token;

  int start = 0;
  while (start < static_cast<int>(cookie.length())) {
    int end = cookie.indexOf(';', start);
    if (end < 0) {
      end = cookie.length();
    }

    String part = cookie.substring(start, end);
    part.trim();

    if (part.startsWith("SVSID=")) {
      token = part.substring(6);
      break;
    }

    start = end + 1;
  }

  if (token.length() != 64) {
    return nullptr;
  }

  for (Session& session : sessions) {
    if (!session.token.length() || !equal(token, session.token)) {
      continue;
    }

    if (Domain::elapsed(millis(), session.started,
                        Config::SESSION_TIMEOUT_MS)) {
      session = Session();
      return nullptr;
    }

    return &session;
  }

  return nullptr;
}

bool Security::throttled() const {
  return failures >= 5 &&
         !Domain::elapsed(millis(), lastFailure, Config::LOGIN_THROTTLE_MS);
}

bool Security::verifyAdminPassword(const String& password, const RiderCredentials& state) {
  return state.adminHash.length() ? equal(hashPassword(password,state.adminSalt),state.adminHash) : equal(password,Config::DEVELOPER_PASSWORD);
}

Security::Session* Security::login(const String& username,
                                   const String& password,
                                   const RiderCredentials& state) {
  if (throttled()) {
    return nullptr;
  }

  if (Domain::elapsed(millis(), lastFailure, Config::LOGIN_THROTTLE_MS)) {
    failures = 0;
  }

  const bool developer = username == state.adminUsername;
  const bool usernameOk = developer || username == state.riderUsername;
  const bool passwordOk = developer ? verifyAdminPassword(password, state)
                                    : usernameOk && verifyRiderPassword(password, state);
  return finishLogin(usernameOk && passwordOk &&
      (developer || Config::ALLOW_DEFAULT_RIDER_LOGIN || !factoryRiderAccount(state)),developer);
}

Security::Session* Security::finishLogin(bool valid,bool developer) {
  if(throttled())return nullptr;
  if(Domain::elapsed(millis(),lastFailure,Config::LOGIN_THROTTLE_MS))failures=0;
  if (!valid) {
    failures++;
    lastFailure = millis();
    return nullptr;
  }

  failures = 0;

  int slot = 0;
  for (size_t i = 0; i < Config::MAX_SESSIONS; ++i) {
    if (!sessions[i].token.length() ||
        Domain::elapsed(millis(), sessions[i].started,
                        Config::SESSION_TIMEOUT_MS)) {
      slot = static_cast<int>(i);
      break;
    }
  }

  sessions[slot] = {randomHex(32), randomHex(32), millis(),
                    developer ? Access::Role::Developer : Access::Role::Rider};
  return &sessions[slot];
}

void Security::clear() {
  for (Session& session : sessions) {
    session = Session();
  }
}

void Security::clearRiderSessions() {
  for (Session& session : sessions)
    if (session.role == Access::Role::Rider) session = Session();
}
