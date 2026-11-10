#pragma once
#include <Arduino.h>
#include <esp_adc/adc_oneshot.h>
#include "../../Config.h"
#include <Preferences.h>
#include "PiezoImpact.h"
class EventLink;

// Two PROTECTED group outputs; this class does not electrically combine piezos.
// ADC success cannot prove all four piezos in a passive group are connected.
class PiezoInputs {
 public:
  PiezoInputs() = default;
  PiezoInputs(const PiezoInputs&) = delete;
  PiezoInputs& operator=(const PiezoInputs&) = delete;
  ~PiezoInputs() { if (handle) adc_oneshot_del_unit(handle); }
  void begin();
  void tick();
  bool healthy() const;
  PiezoImpact impact;
  void handleCommand(const ImuLabProtocol::Frame& command,EventLink& link,bool busy);
  void report(EventLink& link,uint32_t candidate,uint32_t decisionAt,bool busy);
  void clearPeaks() { frontPeak = rearPeak = 0; }
  uint16_t reportedFront() const { return healthy() ? frontPeak : 0; }
  uint16_t reportedRear() const { return healthy() ? rearPeak : 0; }
  uint16_t frontRaw = 0, rearRaw = 0, frontPeak = 0, rearPeak = 0;
  uint32_t atMs = 0, samples = 0, failures = 0, overruns = 0, clippedPairs = 0;
  uint16_t goodPairs = 0;
  bool fresh = false, initialized = false;
  esp_err_t lastError = ESP_OK;
 private:
  adc_oneshot_unit_handle_t handle = nullptr;
  adc_channel_t frontChannel = ADC_CHANNEL_6, rearChannel = ADC_CHANNEL_7;
  bool started = false, validPair = false;
  uint32_t lastPoll = 0, lastInit = 0;
  bool initialize();
  void invalidate(esp_err_t error);
  Preferences settings;
  bool settingsOpen=false,settingsFault=false,saved=false,responsePending=false;
  uint32_t reportAt=0,reportSequence=1,lastToken=0,lastSession=0;
  ImuLabProtocol::Frame lastCommand,response;
  void loadSettings();
  bool saveSettings(uint16_t front,uint16_t rear);
};
