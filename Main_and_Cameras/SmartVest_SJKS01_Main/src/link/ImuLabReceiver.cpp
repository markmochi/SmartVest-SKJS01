#include "SensorLink.h"
#include "../../Config.h"
#include <cmath>
#include <cstring>
#include <esp_system.h>
namespace L = ImuLabProtocol;

bool SensorLink::labOnline() const {
  return Config::ENABLE_SENSOR_LINK && labSeen && uint32_t(millis()-labSeenAt)<1500 && seen && labBoot==heartbeat.boot && uint32_t(millis()-lastSeen)<Config::SENSOR_OFFLINE_MS;
}
uint32_t SensorLink::requestLab(L::Op op,uint16_t argument,uint16_t count,uint32_t revision) {
  if(!labOnline() || commandPending || op==L::Op::Start || op==L::Op::Stop || op==L::Op::ReadSamples)return 0;
  if(!commandSequence){commandSequence=esp_random();if(!commandSequence)commandSequence=1;}
  labCommand=L::Frame();labCommand.session=labSession;labCommand.type=L::Type::Command;labCommand.boot=labBoot;
  labCommand.token=commandSequence++;if(!commandSequence)commandSequence=1;
  labCommand.length=40;labCommand.data[0]=uint8_t(op);L::put16(labCommand.data+2,argument);L::put16(labCommand.data+4,count);
  if(op>=L::Op::PiezoBaseline && op<=L::Op::PiezoStop)L::put32(labCommand.data+8,revision);
  commandPending=true;commandSent=false;commandState=1;commandResult=0;commandAttempts=0;commandAt=millis();
  return labCommand.token;
}
void SensorLink::pumpLabCommand() {
  if(seen && uint32_t(millis()-lastSeen)<Config::SENSOR_OFFLINE_MS && (!labSession || uint32_t(millis()-helloAt)>=1000)){
    if(!labSession){labSession=esp_random();if(!labSession)labSession=1;}
    // Capability remains stable during backpressure so retained events can still receive durable ACKs.
    L::Frame hello;hello.type=L::Type::Hello;hello.boot=heartbeat.boot;hello.session=labSession;if(Config::ARM_CRASH_DETECTION)hello.token=L::AUTOMATIC_CRASH_HELLO_TOKEN;
    uint8_t bytes[L::MAX_FRAME];size_t n=L::encode(hello,bytes);
    if(n && uart.availableForWrite()>=int(n) && uart.write(bytes,n)==n){helloAt=millis();txBytes+=n;++labHelloSent;}
  }
  if(!commandPending)return;
  if(!labOnline() || uint32_t(millis()-commandAt)>3000) {
    commandPending=false;commandState=4;
    return;
  }
  if(commandSent && uint32_t(millis()-commandSentAt)<500)return;
  if(commandAttempts>=5)return;
  uint8_t bytes[L::MAX_FRAME];const size_t size=L::encode(labCommand,bytes);
  if(uart.availableForWrite()<int(size))return;
  const size_t written=uart.write(bytes,size);txBytes+=written;
  if(written==size){commandSent=true;commandSentAt=millis();++commandAttempts;}
}
void SensorLink::receiveLab(const L::Frame& f) {
  if(!labSession || f.session!=labSession || !seen || f.boot!=heartbeat.boot)return;
  if(f.type==L::Type::Piezo){
    PiezoProtocol::Status value;
    if(!labOnline()||!PiezoProtocol::decode(f,value)){++rejected;return;}
    if(piezoSeen && int32_t(value.sequence-piezoSequence)<=0)return;
    piezoLive=value;piezoAt=millis();piezoSequence=value.sequence;piezoSeen=true;reportIncidentImpact();return;
  }
  if(f.type==L::Type::Battery){
    BatteryProtocol::Reading r;
    if(f.length!=BatteryProtocol::SIZE||!BatteryProtocol::decode(f.data,r)){++rejected;return;}
    battery.accept(r,millis());return;
  }
  if(f.type==L::Type::Live) {
    L::Live v;if(!L::decodeLive(f,v) || !std::isfinite(v.rateHz) || !std::isfinite(v.side) || !std::isfinite(v.forward)) {++rejected;return;}
    if(labSeen && f.boot!=labBoot) {
      metaSeen=false;commandPending=false;commandState=4;
    }
    labLive=v;labBoot=f.boot;labSeen=true;labSeenAt=millis();++labLiveAccepted;return;
  }
  if(!labSeen || f.boot!=labBoot)return;
  if(f.type==L::Type::Metadata) {
    L::Metadata m;if(L::decodeMetadata(f,m) && !m.id){labMeta=m;metaSeen=true;}else ++rejected;
  } else if(f.type==L::Type::Response && f.length==6 && f.token==labCommand.token) {
    if(f.data[0]!=labCommand.data[0]){++rejected;return;}
    commandResult=f.data[1];commandPending=false;commandState=commandResult==0?2:3;
  }
}
namespace {
void vectorJson(JsonObject out,const char* key,const float* values) {
  auto a=out[key].to<JsonArray>();for(size_t i=0;i<3;++i)a.add(values[i]);
}
void rawJson(JsonObject out,const char* key,const int16_t* values) {
  auto a=out[key].to<JsonArray>();for(size_t i=0;i<3;++i)a.add(values[i]);
}

}
ImuLabProtocol::Result SensorLink::riderCalibrationReady(L::Op op) const {
  if(op!=L::Op::Gyro && op!=L::Op::Upright && op!=L::Op::Forward && op!=L::Op::Neutral)return L::Result::Unknown;
  if(!labOnline() || !labLive.healthy || uint32_t(millis()-labSeenAt)>350)return L::Result::Offline;
  if(commandPending || labLive.calibrationResult==1 || labLive.state==L::State::Countdown || labLive.state==L::State::Recording)return L::Result::Busy;
  if(op==L::Op::Forward && !(labLive.calibrationFlags&L::UprightSet))return L::Result::Prerequisite;
  if(op==L::Op::Neutral && !(labLive.calibrationFlags&L::BodyCalibrated))return L::Result::Prerequisite;
  return L::Result::Accepted;
}
void SensorLink::writeRiderPoseJson(JsonObject out) {
  const bool online=labOnline(),healthy=online && labLive.healthy;
  // The browser must not animate retained angles as a current measurement.
  const bool valid=healthy && uint32_t(millis()-labSeenAt)<=350 && bool(labLive.sample.flags&L::AttitudeValid) &&
    (labLive.calibrationFlags&(L::GyroCalibrated|L::BodyCalibrated|L::NeutralSet))==
      (L::GyroCalibrated|L::BodyCalibrated|L::NeutralSet);
  out["boot"]=labBoot;out["sequence"]=labLive.sample.sequence;
  out["ageMs"]=labSeen?uint32_t(millis()-labSeenAt):UINT32_MAX;
  out["online"]=online;out["healthy"]=healthy;
  auto pose=out["pose"].to<JsonObject>();pose["valid"]=valid;
  if(valid){pose["sideDeg"]=labLive.side;pose["forwardDeg"]=labLive.forward;}
  else{pose["sideDeg"]=nullptr;pose["forwardDeg"]=nullptr;}
  auto cal=out["calibration"].to<JsonObject>();cal["flags"]=labLive.calibrationFlags;
  cal["operation"]=labLive.calibrationOp;cal["result"]=labLive.calibrationResult;
  cal["samples"]=labLive.calibrationCount;cal["revision"]=labLive.revision;
  auto cmd=out["command"].to<JsonObject>();cmd["token"]=labCommand.token;cmd["state"]=commandState;cmd["result"]=commandResult;
  auto detector=out["detector"].to<JsonObject>();detector["valid"]=valid;
  detector["state"]=labLive.detectorState;detector["candidates"]=labLive.candidateCount;
  detector["automaticReady"]=valid && labLive.automaticReady && armedAndHealthy();
}
void SensorLink::writeLabJson(JsonObject out) {
  const auto heartbeatState=telemetry();
  const bool automaticReady=labOnline() && labLive.automaticReady && armedAndHealthy();
  out["online"]=labOnline();out["boot"]=labBoot;
  out["ageMs"]=labSeen?uint32_t(millis()-labSeenAt):UINT32_MAX;
  // A heartbeat's ImuReady bit is not interchangeable with a fresh V2 live
  // frame. Report them separately to avoid falsely blaming the BMI323.
  out["sensingConnected"]=heartbeatState.online;
  out["sensorReportedReady"]=heartbeatState.imu;
  out["liveTransportReady"]=labOnline();
  out["healthy"]=labOnline() && labLive.healthy;out["firmware"]=metaSeen?labMeta.firmware:"unreported";
  out["schemaVersion"]=2;out["protocolVersion"]=2;out["compatible"]=labOnline();
  out["sensorModel"]="BMI323";out["interface"]="SPI";
  auto profile=out["profile"].to<JsonObject>();profile["sensorModel"]="BMI323";profile["interface"]="SPI";
  profile["accelLsbPerG"]=2048;profile["gyroLsbPerDps"]=16.384;profile["temperatureLsbPerC"]=512;profile["temperatureOffsetC"]=23;
  profile["accConfig"]=0x70B9;profile["gyroConfig"]=0x70C9;profile["nominalRateHz"]=200;
  profile["gapWarningUs"]=7500;profile["spiHz"]=100000;profile["spiMode"]=0;
  auto pins=profile["pins"].to<JsonObject>();pins["sck"]=18;pins["miso"]=19;pins["mosi"]=23;pins["cs"]=21;
  auto detector=out["detector"].to<JsonObject>();detector["policy"]=labLive.automaticDetectionEnabled?"automatic-crash-detection":"diagnostic-only";
  detector["automaticDetectionEnabled"]=labLive.automaticDetectionEnabled;detector["automaticReady"]=automaticReady;
  detector["mainAutomaticDetectionEnabled"]=Config::ARM_CRASH_DETECTION;detector["preset"]="Balanced";
  // A stale/missing peer report is unknown, not evidence of disabled impact detection.
  if(piezoOnline())detector["piezoImpactDetectionEnabled"]=bool(piezoLive.flags&PiezoProtocol::Enabled);
  else detector["piezoImpactDetectionEnabled"]=nullptr;
  detector["piezoCorroborationRequired"]=false;
  auto thresholds=detector["thresholds"].to<JsonObject>();thresholds["rotationDps"]=120;thresholds["rotationMs"]=100;thresholds["postureChangeDeg"]=30;thresholds["tiltDeg"]=60;thresholds["tiltMs"]=800;thresholds["windowMs"]=4500;
  detector["state"]=labLive.detectorState;detector["continuity"]=labLive.continuity;detector["candidates"]=labLive.candidateCount;
  detector["rotationHoldMs"]=labLive.rotationHoldUs/1000.0;detector["tiltHoldMs"]=labLive.tiltHoldUs/1000.0;
  detector["postureChangeDeg"]=labLive.postureChange;detector["activityG"]=labLive.activity;detector["automaticAlerts"]=automaticReady;
  detector["valid"]=labOnline() && labLive.healthy && bool(labLive.sample.flags&L::AttitudeValid);

  if(labLive.candidateCount){auto e=detector["lastCandidate"].to<JsonObject>();e["id"]=labLive.candidateCount;e["sensorBoot"]=labBoot;e["startUs32"]=labLive.eventStartUs;e["decisionUs32"]=labLive.decisionUs;e["durationMs"]=uint32_t(labLive.decisionUs-labLive.eventStartUs)/1000.0;e["rotationHoldMs"]=labLive.eventRotationUs/1000.0;e["tiltHoldMs"]=labLive.eventTiltUs/1000.0;e["peakG"]=labLive.eventPeakG;e["peakDps"]=labLive.eventPeakDps;e["postureChangeDeg"]=labLive.eventChange;e["retention"]="latest only, RAM until sensing reboot";}
  out["whoAmI"]=labLive.who;
  auto diagnostics=out["diagnostics"].to<JsonObject>();
  diagnostics["error"]=labLive.error;diagnostics["phase"]=labLive.phase;diagnostics["register"]=labLive.reg;
  diagnostics["observed"]=labLive.observed;diagnostics["expected"]=labLive.expected;
  diagnostics["sensorRejectedFrames"]=labLive.rejected;diagnostics["duplicatePolls"]=labLive.duplicates;diagnostics["notReadyPolls"]=labLive.notReady;diagnostics["maxCycleUs"]=labLive.cycleUs;
  diagnostics["samples"]=labLive.samples;diagnostics["failures"]=labLive.failures;diagnostics["gapEvents"]=labLive.overruns;
  diagnostics["sampleIntervalUs"]=labLive.intervalUs;diagnostics["measuredRateHz"]=labLive.rateHz;
  diagnostics["rejectedFrames"]=rejected+parser.rejected;
  diagnostics["uartBytesReceived"]=rxBytes;
  diagnostics["uartValidHeartbeats"]=heartbeatFrames;
  diagnostics["uartLabFramesReceived"]=labMessagesReceived;
  diagnostics["uartLiveAccepted"]=labLiveAccepted;
  diagnostics["uartHelloSent"]=labHelloSent;
  diagnostics["uartLabLastAcceptedAgeMs"]=labSeen?uint32_t(millis()-labSeenAt):UINT32_MAX;
  auto cal=out["calibration"].to<JsonObject>();cal["revision"]=labLive.revision;cal["flags"]=labLive.calibrationFlags;cal["operation"]=labLive.calibrationOp;
  cal["result"]=labLive.calibrationResult;cal["samples"]=labLive.calibrationCount;vectorJson(cal,"gyroBiasDps",labLive.bias);
  auto s=out["sample"].to<JsonObject>();s["timeUs"]=labLive.sample.timeUs;s["sequence"]=labLive.sample.sequence;s["flags"]=labLive.sample.flags;
  rawJson(s,"accelRaw",labLive.sample.accel);rawJson(s,"gyroRaw",labLive.sample.gyro);s["temperatureRaw"]=labLive.sample.temperature;
  s["temperatureValid"]=bool(labLive.sample.flags&L::TemperatureValid);s["temperatureFresh"]=bool(labLive.sample.flags&L::TemperatureFresh);
  s["sensorTimeTicks"]=labLive.sample.sensorTime;s["sensorDeltaUs"]=labLive.sample.deltaUs;
  s["candidateId"]=labLive.sample.candidateId;s["detectorFlags"]=labLive.sample.detectorFlags;s["continuity"]=labLive.sample.continuity;
  s["piezoFrontRaw"]=nullptr;s["piezoRearRaw"]=nullptr;
  if(labLive.sample.flags&L::PiezoValid){s["piezoFrontRaw"]=labLive.sample.front;s["piezoRearRaw"]=labLive.sample.rear;}
  auto pose=out["pose"].to<JsonObject>();pose["valid"]=labOnline() && labLive.healthy && bool(labLive.sample.flags&L::AttitudeValid);
  pose["sideDeg"]=labLive.side;pose["forwardDeg"]=labLive.forward;pose["gravityTrust"]=labLive.trust;
  auto cmd=out["command"].to<JsonObject>();cmd["token"]=labCommand.token;cmd["operation"]=labCommand.data[0];cmd["state"]=commandState;cmd["result"]=commandResult;
}
