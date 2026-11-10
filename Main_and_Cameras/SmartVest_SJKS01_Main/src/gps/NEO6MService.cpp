#include "NEO6MService.h"
#include "UtcClock.h"

#include "../../Config.h"

void NEO6MService::begin() {
  if (!Config::ENABLE_NEO6M && !Config::ENABLE_USB_DEBUG) return;
  baud = Config::ENABLE_NEO6M ? Config::NEO6M_BAUD : Config::DEBUG_BAUD;
  rateSince = millis();
  uart.setRxBufferSize(4096);
  uart.setTxBufferSize(256);
  uart.begin(baud, SERIAL_8N1, Config::ENABLE_NEO6M ? Config::NEO6M_RX_PIN : -1,
             Config::ENABLE_USB_DEBUG ? Config::DEBUG_TX_PIN : -1);
  uart.onReceiveError([this](hardwareSerial_error_t error) { uartHealth.observe(error); });
}

NEO6MService::Telemetry NEO6MService::telemetry() const {
  const uint32_t now = millis();
  return {Config::ENABLE_NEO6M && rxBytes && uint32_t(now-lastByteAt) < 5000,
          Config::ENABLE_NEO6M && validSentences && uint32_t(now-lastSentenceAt) < 5000,
          Config::ENABLE_NEO6M && gps.location.isValid(),
          Config::ENABLE_NEO6M && validSentences && uint32_t(now-lastSentenceAt) < 5000 && gps.location.isValid() && gps.location.age() < Config::NEO6M_FRESH_MS,
          rxBytes, validSentences, uint32_t(gps.failedChecksum()), baud,
          rxBytes ? uint32_t(now-lastByteAt) : UINT32_MAX,
          gps.location.isValid() ? uint32_t(gps.location.age()) : UINT32_MAX};
}

void NEO6MService::tick() {
  if (!Config::ENABLE_NEO6M) {
    return;
  }

  // The GPS only needs a small processing slice each loop. Limiting the amount
  // read at once prevents a GPS backlog from delaying SIM800L servicing.
  int budget = 512;
  while (uart.available() && budget-- > 0) {
    lastByteAt = millis();
    ++rxBytes;
    if (gps.encode(uart.read())) {
      lastSentenceAt = millis();
      ++validSentences;
      // Consume date/time updates together in the same decoded sentence.
      // A later GGA time must not be paired with yesterday's RMC date.
      if(gps.date.isUpdated() && gps.time.isUpdated()){
        clockUtcMs=UtcClock::fromDate(gps.date.year(),gps.date.month(),gps.date.day(),gps.time.hour(),gps.time.minute(),gps.time.second(),gps.time.centisecond());
        clockReceivedAt=millis();
      }
    }
  }
  const uint32_t now = millis();
  // Receive-only baud search. No reset/configuration commands go to the GPS.
  if (Config::NEO6M_AUTO_BAUD && rxBytes && uint32_t(now - lastByteAt) < 3000 &&
      uint32_t(now - rateSince) >= 12000 &&
      (!validSentences || uint32_t(now - lastSentenceAt) >= 12000)) {
    const uint32_t rates[] = {Config::NEO6M_BAUD, 38400, 115200, 57600, 19200, 4800};
    rateIndex = (rateIndex + 1) % (sizeof(rates) / sizeof(rates[0]));
    baud = rates[rateIndex];
    uart.end();
    uart.begin(baud, SERIAL_8N1, Config::NEO6M_RX_PIN, -1);
    uart.onReceiveError([this](hardwareSerial_error_t error) { uartHealth.observe(error); });
    rateSince = now;
  }
}

bool NEO6MService::latestLocation(double& latitude,
                                  double& longitude,
                                  uint32_t& ageMs,
                                  bool& fresh) {
  if (!Config::ENABLE_NEO6M || !gps.location.isValid()) {
    return false;
  }

  latitude = gps.location.lat();
  longitude = gps.location.lng();
  ageMs = gps.location.age();
  fresh = validSentences && uint32_t(millis()-lastSentenceAt) < 5000 && ageMs < Config::NEO6M_FRESH_MS;
  return true;
}

void NEO6MService::writeJson(JsonObject output) {
  output["enabled"] = Config::ENABLE_NEO6M;
  output["uart"] = 1;
  uartHealth.writeJson(output["uartErrors"].to<JsonObject>());
  output["rxPin"] = Config::NEO6M_RX_PIN;
  output["txPin"] = nullptr;
  output["gpsTransmitEnabled"] = false;
  output["reservedGpsRxPin"] = Config::NEO6M_TX_PIN;
  output["consoleTxPin"] = Config::ENABLE_USB_DEBUG ? Config::DEBUG_TX_PIN : -1;

  if (!Config::ENABLE_NEO6M) {
    output["state"] = "disabled";
    output["characters"] = 0;
    output["checksumFailures"] = 0;
    output["latitude"] = nullptr;
    output["longitude"] = nullptr;
    output["ageMs"] = nullptr;
    output["satellites"] = nullptr;
    output["hdop"] = nullptr;
    output["utc"] = nullptr;
    return;
  }

  const uint32_t now = millis();
  output["rxBytes"] = rxBytes;
  output["validSentences"] = validSentences;
  output["baud"] = baud;
  output["rxPin"] = Config::NEO6M_RX_PIN;
  output["autoBaud"] = Config::NEO6M_AUTO_BAUD;
  if (rxBytes) output["lastByteAgeMs"] = uint32_t(now - lastByteAt);
  else output["lastByteAgeMs"] = nullptr;
  if (validSentences) output["lastSentenceAgeMs"] = uint32_t(now - lastSentenceAt);
  else output["lastSentenceAgeMs"] = nullptr;
  const bool receiving = rxBytes && uint32_t(now - lastByteAt) < 5000;
  const bool decoding = validSentences && uint32_t(now - lastSentenceAt) < 5000;
  output["receiving"] = receiving;
  output["decoding"] = decoding;
  output["streamState"] = !receiving ? "no_data" : decoding ? "live" : "unreadable";
  if (satellitesInView.isValid() && satellitesInView.age() < 10000)
    output["satellitesInView"] = atoi(satellitesInView.value());
  else output["satellitesInView"] = nullptr;
  const bool valid = gps.location.isValid();
  const bool fresh = receiving && decoding && valid && gps.location.age() < Config::NEO6M_FRESH_MS;

  output["state"] =
      fresh ? "fresh" :
      valid ? "stale" :
      decoding ? "searching" : receiving ? "unreadable" : "no_data";

  output["characters"] = gps.charsProcessed();
  output["checksumFailures"] = gps.failedChecksum();

  if (valid) {
    output["latitude"] = gps.location.lat();
    output["longitude"] = gps.location.lng();
    output["ageMs"] = gps.location.age();
  } else {
    output["latitude"] = nullptr;
    output["longitude"] = nullptr;
    output["ageMs"] = nullptr;
  }

  if (gps.satellites.isValid() && gps.satellites.age() < 10000) {
    output["satellites"] = gps.satellites.value();
  } else {
    output["satellites"] = nullptr;
  }

  if (gps.hdop.isValid() && gps.hdop.age() < 10000) {
    output["hdop"] = gps.hdop.hdop();
  } else {
    output["hdop"] = nullptr;
  }

  if (gps.date.isValid() &&
      gps.time.isValid() &&
      gps.time.age() < 10000 &&
      gps.date.age() < 10000) {
    char utc[32];
    snprintf(utc, sizeof(utc),
             "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    output["utc"] = utc;
  } else {
    output["utc"] = nullptr;
  }
}

bool NEO6MService::latestUtcMs(uint64_t& utcMs) {
  const uint32_t age=uint32_t(millis()-clockReceivedAt);
  if(!Config::ENABLE_NEO6M || !UtcClock::valid(clockUtcMs) || age>2000)return false;
  utcMs=clockUtcMs+age;return true;
}

// HDOP describes satellite geometry; it is not a measured position error.
bool NEO6MService::weakGeometry() {
  return Config::ENABLE_NEO6M && gps.hdop.isValid() && gps.hdop.age()<10000 && gps.hdop.hdop()>5;
}
