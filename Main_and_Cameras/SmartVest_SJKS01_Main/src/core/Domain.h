#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include "../../Config.h"

namespace Domain {

inline bool elapsed(uint32_t now, uint32_t then, uint32_t interval) {
  return uint32_t(now - then) >= interval;
}

inline std::string normalizePhone(const std::string& input) {
  std::string phone;

  for (char c : input) {
    if (c == ' ' || c == '-' || c == '(' || c == ')') {
      continue;
    }
    if ((c < '0' || c > '9') && c != '+') {
      return "";
    }
    phone += c;
  }

  if (phone.size() == 11 && phone.substr(0, 2) == "09") {
    phone = "+63" + phone.substr(1);
  } else if (phone.size() == 12 && phone.substr(0, 3) == "639") {
    phone = "+" + phone;
  }

  if (phone.size() != 13 || phone.substr(0, 4) != "+639") {
    return "";
  }

  for (size_t i = 1; i < phone.size(); ++i) {
    if (phone[i] < '0' || phone[i] > '9') {
      return "";
    }
  }

  return phone;
}

inline bool validName(const std::string& name) {
  if (name.empty() || name.size() > 40) {
    return false;
  }

  bool hasLetter = false;
  for (char c : name) {
    const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');

    const bool allowed = alpha || (c >= '0' && c <= '9') || c == ' ' ||
                         c == '.' || c == '-' || c == '\'';

    if (!allowed) {
      return false;
    }
    hasLetter |= alpha;
  }

  return hasLetter;
}

inline std::string invitationMessage(const std::string& rider) {
  return "Hello from " + rider + Config::SMS_INVITATION_TEXT;
}

inline std::string smsLocation(double latitude, double longitude) {
  char coordinates[40] = {};
  std::snprintf(coordinates, sizeof(coordinates), "%.6f,%.6f", latitude,
                longitude);
  return coordinates;
}

// Shared wording for automatic events, all-contact tests and single-contact
// tests. The position is the main board's latest valid fix, not a reconstructed
// crash site.
inline std::string incidentMessage(const std::string& rider,
                                   const std::string& location, bool fresh,
                                   bool test) {
  std::string message;
  if (test)
    message = std::string(Config::SMS_TEST_HEADER) + "\n" + rider + "\n" +
              Config::SMS_TEST_TEXT;
  else
    message = std::string(Config::SMS_ALERT_HEADER) + "\nRider: " + rider + "\n" +
              Config::SMS_ALERT_ACTION;
  if (location.empty()) return message + "\n" + Config::SMS_NO_GPS_TEXT;
  message += "\n";
  message += fresh ? Config::SMS_FRESH_LOCATION_LABEL
                   : Config::SMS_STALE_LOCATION_LABEL;
  message += "\n" + location;
  return message;
}

inline std::string crashMessage(const std::string& rider,
                                const std::string& location,
                                bool locationIsFresh) {
  return incidentMessage(rider, location, locationIsFresh, false);
}

inline std::string crashTestMessage(const std::string& rider, double latitude,
                                    double longitude, bool hasLocation,
                                    bool fresh) {
  std::string location;
  if (hasLocation) location = smsLocation(latitude, longitude);
  return incidentMessage(rider, location, fresh, true);
}

inline bool isTerminalJobState(const std::string& state) {
  return state == "delivered" || state == "undelivered" ||
         state == "accepted" || state == "failed" || state == "unknown" ||
         state == "cancelled";
}

}  // namespace Domain
