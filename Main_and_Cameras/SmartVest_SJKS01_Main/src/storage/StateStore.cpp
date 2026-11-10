#include "StateStore.h"

#include "../../Config.h"
#include "../core/Domain.h"
#include "../security/Security.h"
#include "../security/Access.h"
#include "../security/CameraPairingKeys.h"

namespace {
bool encodeRecord(JsonDocument& document, String& encoded, size_t limit) {
  if (document.overflowed()) return false;
  const size_t expected = measureJson(document);
  if (expected >= limit || !encoded.reserve(expected)) return false;
  return serializeJson(document, encoded) == expected &&
         encoded.length() == expected;
}

String cleanStoredString(JsonVariantConst value) {
  const char* raw = value | "";
  String result = raw ? String(raw) : String();
  result.trim();
  if (result == "null" || result == "undefined") {
    return String();
  }
  return result;
}
}  // namespace

bool StateStore::begin() {
  // Project identity is independent of NVS, rider resets and firmware version.
  data.cameraKeys[0] = CameraPairing::FRONT_KEY;
  data.cameraKeys[1] = CameraPairing::REAR_KEY;
  if (!preferences.begin("sjks01", false)) {
    lastError = "Cannot open settings namespace. Check the NVS partition.";
    return false;
  }

  const String compact = preferences.getString("config", "");
  const String raw =
      compact.length() ? compact : preferences.getString("state", "");
  persistedSettings = compact;

  // Existing installation: restore saved state.
  if (raw.length()) {
    JsonDocument document;
    if (deserializeJson(document, raw) || document["schema"].as<int>() != 1) {
      lastError =
          "Saved settings cannot be decoded. Credentials were not reset.";
      return false;
    }

    data.revision = document["revision"] | 0U;
    data.dataResetPending = document["dataResetPending"] | false;
    data.adminUsername = document["adminUsername"] | Config::DEVELOPER_USERNAME;
    data.adminSalt = document["adminSalt"] | "";
    data.adminHash = document["adminHash"] | "";
    if (!Access::validRiderUsername(data.adminUsername.c_str(),true) ||
        (data.adminHash.length() && (data.adminHash.length()!=64 || data.adminSalt.length()!=32))) {
      lastError="Saved administrator credentials are invalid.";return false;
    }
    data.nextJob = document["nextJob"] | 1U;
    data.rider = cleanStoredString(document["rider"]);
    // Existing named riders keep their dashboard; new/reset profiles start setup.
    data.riderSetupComplete = document["riderSetupComplete"] | (data.rider.length()>0);
    data.notifyAdded = document["notifyAdded"] | true;
    data.salt = document["salt"].as<String>();
    data.passwordHash = document["passwordHash"].as<String>();
    data.defaultPassword = document["defaultPassword"] | true;
    data.riderAccountResetRevision = document["accountResetRevision"] | 0U;
    data.riderUsername = document["username"] | Config::DEFAULT_RIDER_USERNAME;
    if (!Access::validRiderUsername(data.riderUsername.c_str()) || data.riderUsername==data.adminUsername || data.adminUsername==Config::DEFAULT_RIDER_USERNAME) {
      lastError = "Saved username is invalid. Credentials were not reset.";
      return false;
    }

    const bool pairingChanged = document["cameraKeys"][0].as<String>() != data.cameraKeys[0] ||
                                document["cameraKeys"][1].as<String>() != data.cameraKeys[1];

    if (data.salt.length() != 32 || data.passwordHash.length() != 64 ||
        data.cameraKeys[0].length() != 64 ||
        data.cameraKeys[1].length() != 64) {
      lastError =
          "Saved credentials are invalid. No automatic factory reset was "
          "performed.";
      return false;
    }
    // Upgrade only a verified unchanged old factory password. No writes on
    // read; the next normal settings commit persists credentialVersion and
    // username.
    if ((document["credentialVersion"] | 1) < 2 && data.defaultPassword &&
        data.passwordHash == Security::hashPassword(
                                 Config::LEGACY_DEFAULT_PASSWORD, data.salt)) {
      const String migrated =
          Security::hashPassword(Config::DEFAULT_RIDER_PASSWORD, data.salt);
      if (migrated.length() != 64) {
        lastError = "Cannot prepare rider credentials.";
        return false;
      }
      data.passwordHash = migrated;
    }

    data.defaultPassword = Security::factoryRiderAccount(data);

    for (size_t i = 0; i < Config::MAX_CONTACTS; ++i) {
      String name = cleanStoredString(document["contacts"][i]["name"]);
      String phone = cleanStoredString(document["contacts"][i]["phone"]);
      const std::string normalized = Domain::normalizePhone(phone.c_str());

      if (Domain::validName(name.c_str()) && !normalized.empty()) {
        data.contacts[i].name = name;
        data.contacts[i].phone = normalized.c_str();
      } else {
        data.contacts[i] = Contact();
      }
    }

    // Legacy history stays readable until the small-record migration succeeds.
    JsonDocument legacy;
    if (compact.length()) {
      const String old = preferences.getString("state", "");
      if (old.length()) deserializeJson(legacy, old);
    }
    for (size_t i = 0; i < Config::MAX_SMS_JOBS; ++i) {
      char key[12];
      snprintf(key, sizeof(key), "job%02u", unsigned(i));
      persistedJobs[i] = preferences.getString(key, "");
      JsonDocument record;
      JsonVariantConst jobJson;
      if (persistedJobs[i].length()) {
        if (deserializeJson(record, persistedJobs[i])) {
          lastError =
              "A history record could not be read; settings remain available.";
          continue;
        }
        jobJson = record.as<JsonVariantConst>();
      } else {
        jobJson = compact.length() ? legacy["jobs"][i].as<JsonVariantConst>()
                                   : document["jobs"][i].as<JsonVariantConst>();
      }
      SmsJob& job = data.jobs[i];
      job.id = jobJson["id"] | 0U;
      job.eventBoot = jobJson["eventBoot"] | 0U;
      job.eventSequence = jobJson["eventSequence"] | 0U;
      job.recipientIndex = jobJson["recipientIndex"] | 0;
      job.phone = cleanStoredString(jobJson["phone"]);
      job.text = cleanStoredString(jobJson["text"]);
      job.kind = cleanStoredString(jobJson["kind"]);
      job.state = cleanStoredString(jobJson["state"]);
      job.detail = cleanStoredString(jobJson["detail"]);
      job.attempts = jobJson["attempts"] | 0;
      job.modemReference = jobJson["modemReference"] | -1;
      job.deliveryRequested = jobJson["deliveryRequested"] | false;
      job.deliveryStatus = jobJson["deliveryStatus"] | -1;
      job.carrierReport = cleanStoredString(jobJson["carrierReport"]);
      const std::string normalized = Domain::normalizePhone(job.phone.c_str());
      if (!job.id || normalized.empty() || !job.text.length()) {
        job = SmsJob();
        continue;
      }
      job.phone = normalized.c_str();
      if (job.id >= data.nextJob) data.nextJob = job.id + 1;
      if (job.state == "sending") {
        job.state = "unknown";
        job.detail =
            "Restart during transmission; check recipient before retry.";
      } else if (job.state == "queued" || job.state == "retry") {
        job.state = "cancelled";
        job.detail = "Restart; resend manually if still wanted.";
      }
    }

    healthy = true;
    if (!applyRiderAccountReset()) return false;
    // Keep stored settings consistent with the fixed identity. A write failure
    // leaves source-defined pairing active and exposes the normal write error.
    if (pairingChanged) commit(data);
    return true;
  }

  // First boot: create login credentials; camera identity stays project-fixed.
  data.salt = Security::randomHex(16);
  data.passwordHash =
      Security::hashPassword(Config::DEFAULT_RIDER_PASSWORD, data.salt);
  data.riderUsername = Config::DEFAULT_RIDER_USERNAME;
  data.riderAccountResetRevision = Config::RIDER_ACCOUNT_RESET_REVISION;

  if (data.passwordHash.length() != 64) {
    return false;
  }

  healthy = true;
  return commit(data);
}

bool StateStore::applyRiderAccountReset() {
  if (!Config::RIDER_ACCOUNT_RESET_REVISION ||
      data.riderAccountResetRevision >= Config::RIDER_ACCOUNT_RESET_REVISION)
    return true;
  DeviceState next = data;
  next.salt = Security::randomHex(16);
  next.passwordHash = Security::hashPassword(Config::DEFAULT_RIDER_PASSWORD, next.salt);
  if (next.passwordHash.length() != 64) {
    lastError = "Cannot prepare rider login reset.";
    return false;
  }
  next.riderUsername = Config::DEFAULT_RIDER_USERNAME;
  next.defaultPassword = true;
  next.riderAccountResetRevision = Config::RIDER_ACCOUNT_RESET_REVISION;
  ++next.revision;
  if (!commit(next)) {
    lastError = "Rider login reset could not be saved. Previous credentials remain active.";
    return false;
  }
  return true;
}

void StateStore::writeSettingsJson(const DeviceState& state,
                                   JsonObject output) {
  output["revision"] = state.revision;
  output["riderSetupComplete"] = state.riderSetupComplete;
  output["rider"] = state.rider;
  output["notifyAdded"] = state.notifyAdded;

  JsonArray contacts = output["contacts"].to<JsonArray>();
  for (const Contact& contact : state.contacts) {
    const std::string normalized =
        Domain::normalizePhone(contact.phone.c_str());
    if (!Domain::validName(contact.name.c_str()) || normalized.empty()) {
      continue;
    }

    JsonObject item = contacts.add<JsonObject>();
    item["name"] = contact.name;
    item["phone"] = normalized.c_str();
  }
}

bool StateStore::commit(const DeviceState& next) {
  if (!healthy) {
    return false;
  }

  JsonDocument document;
  writeSettingsJson(next, document.to<JsonObject>());

  document["schema"] = 1;
  document["credentialVersion"] = 2;
  document["adminUsername"] = next.adminUsername;
  document["adminSalt"] = next.adminSalt;
  document["adminHash"] = next.adminHash;
  document["dataResetPending"] = next.dataResetPending;
  document["username"] = next.riderUsername;
  document["nextJob"] = next.nextJob;
  document["salt"] = next.salt;
  document["passwordHash"] = next.passwordHash;
  document["defaultPassword"] = next.defaultPassword;
  document["accountResetRevision"] = next.riderAccountResetRevision;

  for (int i = 0; i < 2; ++i) {
    document["cameraKeys"][i] = i ? CameraPairing::REAR_KEY : CameraPairing::FRONT_KEY;
  }

  String raw;
  if (!encodeRecord(document, raw, 3000)) {
    writable = false;
    lastError = "Settings response memory unavailable; saved settings retained.";
    return false;
  }

  // Settings and each history slot are separate small NVS strings.
  // Write changed jobs first; RAM changes only after all requested writes
  // succeed. On reboot pending jobs are cancelled, so a partial write cannot
  // auto-send.
  for (size_t i = 0; i < Config::MAX_SMS_JOBS; ++i) {
    const SmsJob& job = next.jobs[i];
    JsonDocument record;
    if (job.id) {
      record["id"] = job.id;
      record["eventBoot"] = job.eventBoot;
      record["eventSequence"] = job.eventSequence;
      record["recipientIndex"] = job.recipientIndex;
      record["phone"] = job.phone;
      record["text"] = job.text;
      record["kind"] = job.kind;
      record["state"] = job.state;
      record["detail"] = job.detail;
      record["attempts"] = job.attempts;
      record["modemReference"] = job.modemReference;
      record["deliveryRequested"] = job.deliveryRequested;
      record["deliveryStatus"] = job.deliveryStatus;
      record["carrierReport"] = job.carrierReport;
    } else {
      record.to<JsonObject>();
    }
    String encoded;
    if (!encodeRecord(record, encoded, 2000)) {
      writable = false;
      lastError = "History response memory unavailable; record not written.";
      return false;
    }
    if (encoded == persistedJobs[i]) continue;
    char key[12];
    snprintf(key, sizeof(key), "job%02u", unsigned(i));
    if (preferences.putString(key, encoded) != encoded.length()) {
      writable = false;
      lastError = String("Could not save history record ") +
                  String(unsigned(i + 1)) +
                  ". Login and live monitoring remain available.";
      return false;
    }
    persistedJobs[i] = encoded;
  }
  if (raw != persistedSettings) {
    if (preferences.putString("config", raw) != raw.length()) {
      writable = false;
      lastError =
          "Could not save settings. Existing loaded credentials remain active.";
      return false;
    }
    persistedSettings = raw;
  }
  writable = true;
  lastError = "";

  data = next;
  data.cameraKeys[0] = CameraPairing::FRONT_KEY;
  data.cameraKeys[1] = CameraPairing::REAR_KEY;
  return true;
}

int StateStore::findFreeJobSlot(DeviceState& state) {
  int oldestTerminalJob = -1;

  for (size_t i = 0; i < Config::MAX_SMS_JOBS; ++i) {
    if (!state.jobs[i].id) {
      return static_cast<int>(i);
    }

    if (Domain::isTerminalJobState(state.jobs[i].state.c_str()) &&
        (oldestTerminalJob < 0 ||
         state.jobs[i].id < state.jobs[oldestTerminalJob].id)) {
      oldestTerminalJob = static_cast<int>(i);
    }
  }

  return oldestTerminalJob;
}

bool StateStore::enqueueSms(DeviceState& state, const String& phone,
                            const String& text, const String& kind) {
  // Reserve five waiting slots for incident recipients. Real crashes may also
  // replace a waiting introduction, never a message already being transmitted.
  size_t active = 0;
  for (const auto& job : state.jobs)
    if (job.id && !Domain::isTerminalJobState(job.state.c_str())) ++active;
  if (kind != "crash" && active >= Config::MAX_SMS_JOBS - Config::MAX_CONTACTS)
    return false;
  int slot = findFreeJobSlot(state);
  if (slot < 0 && kind == "crash")
    for (size_t i = 0; i < Config::MAX_SMS_JOBS; ++i)
      if (state.jobs[i].kind == "addition" &&
          (state.jobs[i].state == "queued" || state.jobs[i].state == "retry")) {
        slot = int(i);
        break;
      }

  if (slot < 0 || text.length() == 0 ||
      text.length() > Config::SMS_MAX_LENGTH) {
    return false;
  }

  SmsJob job;
  job.id = state.nextJob++;
  job.phone = phone;
  job.text = text;
  job.kind = kind;
  job.state = "queued";
  job.detail = "Waiting for cellular network";
  job.created = millis();
  job.changed = job.created;

  state.jobs[slot] = job;
  return true;
}
