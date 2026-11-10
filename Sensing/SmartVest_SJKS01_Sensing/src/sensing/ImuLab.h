#pragma once
#include "Imu.h"
#include "FallDetector.h"
#include "GyroCalibration.h"
#include "CalibrationStore.h"
#include "PiezoInputs.h"
#include "../link/EventLink.h"

class ImuLab {
 public:
  ImuLab(Imu& i, GyroCalibration& g, CalibrationStore& c, EventLink& l)
      : imu(i), gyro(g), bias(c), link(l) {}
  struct PiezoSnapshot {uint32_t atMs=0;uint16_t front=0,rear=0;bool valid=false;};
  PiezoSnapshot piezo;
  FallDetector detector;
  void begin();
  void tick();
  bool busy() const;
  bool automaticReady() const {
    return !calActive && attitude && bias.valid && imu.healthy() &&
      (body.flags & (ImuLabProtocol::BodyCalibrated|ImuLabProtocol::NeutralSet)) ==
      (ImuLabProtocol::BodyCalibrated|ImuLabProtocol::NeutralSet);
  }
  ImuLabProtocol::Result startCalibration(ImuLabProtocol::Op op);
  void cancelCalibration();
  void loadBody();
  void handle(const ImuLabProtocol::Frame& frame);
 private:
  Imu& imu; GyroCalibration& gyro; CalibrationStore& bias; EventLink& link;
  ImuLabProtocol::Metadata body;
  ImuLabProtocol::Op calOp = ImuLabProtocol::Op::Gyro;
  bool calActive = false, attitude = false, loaded = false, responsePending = false;
  uint8_t calResult = 0;
  float side = 0, forward = 0, trust = 0;
  float gravityEstimate[3] = {0,0,1};
  uint32_t lastLive = 0, lastMeta = 0, rateAt = 0, rateSamples = 0;
  float rateHz = 0;
  uint16_t loadedWho = 0;
  uint64_t acquisitionUs = 0;
  float activitySq = 0;
  bool wasHealthy = false;
  ImuLabProtocol::Frame response;
  ImuLabProtocol::Frame lastCommand;
  uint32_t lastToken = 0;
  bool saveBody(const ImuLabProtocol::Metadata& candidate);
  void completeCalibration();
  void updateAttitude();
  ImuLabProtocol::Sample currentSample() const;
  void sendLive();
  void sendMetadata();
};
