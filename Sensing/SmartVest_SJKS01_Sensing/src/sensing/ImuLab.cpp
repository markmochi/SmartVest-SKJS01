#include "ImuLab.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace L = ImuLabProtocol;
namespace {
constexpr float DEG = 57.29577951308232f;
float dot(const float* a,const float* b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
void cross(const float* a,const float* b,float* out) {
  const float v[3] = {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
  std::memcpy(out,v,sizeof(v));
}
bool normalize(float* a) {
  const float n=std::sqrt(dot(a,a)); if(!std::isfinite(n) || n<1e-6f)return false;
  for(size_t i=0;i<3;++i)a[i]/=n;
  return true;
}
float wrap(float a) { while(a>180)a-=360;while(a< -180)a+=360;return a; }
bool validBody(const L::Metadata& b) {
  const float* axes[] = {b.right,b.forward,b.up};
  for(const auto* a:axes)for(size_t i=0;i<3;++i)if(!std::isfinite(a[i]))return false;
  if(!std::isfinite(b.gravity) || b.gravity<=0.75f || b.gravity>=1.25f)return false;
  for(const auto* a:axes)if(std::fabs(dot(a,a)-1)>0.002f)return false;
  if(std::fabs(dot(b.right,b.forward))>0.002f || std::fabs(dot(b.right,b.up))>0.002f || std::fabs(dot(b.forward,b.up))>0.002f)return false;
  float c[3];cross(b.right,b.forward,c);
  return dot(c,b.up)>0.998f && std::isfinite(b.neutral[0]) && std::isfinite(b.neutral[1]) &&
         std::fabs(b.neutral[0])<=180 && std::fabs(b.neutral[1])<=90;
}
}
void ImuLab::begin() {
  rateAt=micros();rateSamples=imu.samples;
}
bool ImuLab::busy() const {
  return calActive;
}
// Independent calibration storage schema; never persist a UART metadata frame.
bool ImuLab::saveBody(const L::Metadata& c) {
  uint8_t b[82]={},check[82]={};b[0]='B';b[1]='M';b[2]=1;b[3]=c.flags;
  L::put16(b+4,c.who);L::put16(b+6,0x70B9);L::put16(b+8,0x70C9);L::put32(b+12,c.revision);
  L::putFloat(b+16,c.gravity);L::putFloat(b+20,c.lean);
  for(unsigned i=0;i<3;++i){L::putFloat(b+24+i*4,c.bias[i]);L::putFloat(b+36+i*4,c.right[i]);L::putFloat(b+48+i*4,c.forward[i]);L::putFloat(b+60+i*4,c.up[i]);}
  L::putFloat(b+72,c.neutral[0]);L::putFloat(b+76,c.neutral[1]);L::put16(b+80,CrashProtocol::crc16(b,80));
  Preferences prefs;if(!prefs.begin("sv_bmi_mount",false))return false;
  bool ok=prefs.putBytes("body",b,sizeof(b))==sizeof(b)&&prefs.getBytesLength("body")==sizeof(b)&&prefs.getBytes("body",check,sizeof(check))==sizeof(check)&&!std::memcmp(b,check,sizeof(b));prefs.end();return ok;
}
void ImuLab::loadBody() {
  body=L::Metadata();loaded=true;loadedWho=imu.whoAmI;attitude=false;detector.invalidate(FallDetector::Calibration);
  uint8_t b[82]={};Preferences prefs;if(!prefs.begin("sv_bmi_mount",true))return;
  bool ok=prefs.getBytesLength("body")==sizeof(b)&&prefs.getBytes("body",b,sizeof(b))==sizeof(b);prefs.end();
  if(!ok||b[0]!='B'||b[1]!='M'||b[2]!=1||b[10]||b[11]||L::get16(b+4)!=imu.whoAmI||L::get16(b+6)!=0x70B9||L::get16(b+8)!=0x70C9||L::get16(b+80)!=CrashProtocol::crc16(b,80))return;
  L::Metadata c;c.who=imu.whoAmI;c.flags=b[3]&uint8_t(~L::GyroCalibrated);c.revision=L::get32(b+12);c.gravity=L::getFloat(b+16);c.lean=L::getFloat(b+20);
  for(unsigned i=0;i<3;++i){c.bias[i]=L::getFloat(b+24+i*4);c.right[i]=L::getFloat(b+36+i*4);c.forward[i]=L::getFloat(b+48+i*4);c.up[i]=L::getFloat(b+60+i*4);}
  c.neutral[0]=L::getFloat(b+72);c.neutral[1]=L::getFloat(b+76);
  if((c.flags&~uint8_t(15))||!std::isfinite(c.gravity)||c.gravity<=.75f||c.gravity>=1.25f)return;
  if((c.flags&L::BodyCalibrated)&&!validBody(c))return;
  if(c.flags&L::UprightSet){for(float v:c.up)if(!std::isfinite(v))return;if(std::fabs(dot(c.up,c.up)-1)>.002f)return;}
  // The two records must describe the same completed calibration save.
  if(!(b[3]&L::GyroCalibrated) || std::memcmp(c.bias,bias.bias,sizeof(c.bias)))bias.valid=false;
  body=c;
}
L::Result ImuLab::startCalibration(L::Op op) {
  if(busy() || gyro.result==GyroCalibration::Result::Collecting)return L::Result::Busy;
  if(!imu.healthy())return L::Result::Offline;
  if(op==L::Op::Forward && !(body.flags&L::UprightSet))return L::Result::Prerequisite;
  if(op==L::Op::Neutral && !(body.flags&L::BodyCalibrated))return L::Result::Prerequisite;
  if(op!=L::Op::Gyro && op!=L::Op::Upright && op!=L::Op::Forward && op!=L::Op::Neutral)return L::Result::Unknown;
  calOp=op;calActive=true;calResult=1;attitude=false;detector.invalidate(FallDetector::Calibration);gyro.start(millis(),bias.valid?bias.bias:nullptr);return L::Result::Accepted;
}
void ImuLab::cancelCalibration() { gyro.cancel();calActive=false;calResult=9; }
void ImuLab::completeCalibration() {
  calActive=false;calResult=uint8_t(gyro.result);
  if(gyro.result!=GyroCalibration::Result::Complete)return;
  L::Metadata candidate=body;
  candidate.who=imu.whoAmI;candidate.revision=body.revision+1;
  float mean[3];for(size_t i=0;i<3;++i)mean[i]=gyro.acceleration(i);
  const float magnitude=std::sqrt(dot(mean,mean));
  if(calOp==L::Op::Upright) {
    candidate=L::Metadata();candidate.who=imu.whoAmI;candidate.revision=body.revision+1;
    std::memcpy(candidate.up,mean,sizeof(mean));normalize(candidate.up);candidate.gravity=magnitude;
    candidate.flags=L::UprightSet;
  } else if(calOp==L::Op::Forward) {
    const float vertical=dot(mean,candidate.up);float horizontal[3];
    for(size_t i=0;i<3;++i)horizontal[i]=mean[i]-vertical*candidate.up[i];
    const float angle=std::atan2(std::sqrt(dot(horizontal,horizontal)),vertical)*DEG;
    if(angle<15 || angle>60){calResult=7;return;}
    // Specific-force projection during forward lean points toward rider's back.
    for(size_t i=0;i<3;++i)candidate.forward[i]=-horizontal[i];
    normalize(candidate.forward);cross(candidate.forward,candidate.up,candidate.right);normalize(candidate.right);
    cross(candidate.up,candidate.right,candidate.forward);normalize(candidate.forward);
    candidate.lean=angle;candidate.flags|=L::BodyCalibrated;
    candidate.flags&=uint8_t(~L::NeutralSet);candidate.neutral[0]=candidate.neutral[1]=0;
  } else if(calOp==L::Op::Neutral) {
    const float r=dot(mean,candidate.right),f=dot(mean,candidate.forward),u=dot(mean,candidate.up);
    candidate.neutral[0]=std::atan2(-r,u)*DEG;
    candidate.neutral[1]=std::atan2(-f,std::sqrt(r*r+u*u))*DEG;
    candidate.flags|=L::NeutralSet;
  }
  if(!bias.save(imu.whoAmI,gyro.bias)){calResult=8;return;}
  candidate.flags|=L::GyroCalibrated;std::memcpy(candidate.bias,bias.bias,sizeof(candidate.bias));
  if(!saveBody(candidate)){bias.valid=false;calResult=8;body.flags&=uint8_t(~(L::BodyCalibrated|L::NeutralSet));return;}
  body=candidate;attitude=false;calResult=2;
}
void ImuLab::updateAttitude() {
  if(!(body.flags&L::BodyCalibrated) || !bias.valid){attitude=false;trust=0;detector.invalidate(FallDetector::Calibration);return;}
  const auto& s=imu.sample;
  float a[3]={dot(s.accel,body.right),dot(s.accel,body.forward),dot(s.accel,body.up)};
  float corrected[3];for(size_t i=0;i<3;++i)corrected[i]=s.gyro[i]-bias.bias[i];
  float w[3]={dot(corrected,body.right)/DEG,dot(corrected,body.forward)/DEG,dot(corrected,body.up)/DEG};
  const float n=std::sqrt(dot(s.accel,s.accel)),relative=std::fabs(n-body.gravity)/body.gravity;
  trust=relative<=0.08f?1:relative>=0.35f?0:(0.35f-relative)/0.27f;
  const uint32_t gap=s.deltaValid?s.deltaUs:0;
  // Integrate a 3D gravity direction, including coupled-axis rotation. Yaw is
  // unobservable from these six axes. Attitude is only a visualization estimate.
  if(!attitude || imu.continuityLost) {
    if(trust<0.5f || s.saturated || n<0.001f){attitude=false;detector.invalidate(FallDetector::Rejected);return;}
    std::memcpy(gravityEstimate,a,sizeof(a));normalize(gravityEstimate);attitude=true;
  } else {
    const float dt=gap/1000000.0f;float spin[3];cross(w,gravityEstimate,spin);
    for(size_t i=0;i<3;++i)gravityEstimate[i]-=spin[i]*dt;
    normalize(gravityEstimate);
    if(n>0.001f) {
      const float gain=std::min(0.1f,dt/(0.8f+dt)*trust);
      for(size_t i=0;i<3;++i)gravityEstimate[i]+=gain*(a[i]/n-gravityEstimate[i]);
      normalize(gravityEstimate);
    }
  }
  side=wrap(std::atan2(-gravityEstimate[0],gravityEstimate[2])*DEG-body.neutral[0]);
  forward=wrap(std::atan2(-gravityEstimate[1],std::sqrt(gravityEstimate[0]*gravityEstimate[0]+gravityEstimate[2]*gravityEstimate[2]))*DEG-body.neutral[1]);
  const float dt=gap*1e-6f,residual=n-body.gravity;
  if(dt>0 && dt<.1f)activitySq+=(residual*residual-activitySq)*dt;
  float rate=std::sqrt(dot(corrected,corrected));
  detector.feed(acquisitionUs,n,rate,side,forward,std::sqrt(std::max(0.0f,activitySq)),body.gravity);

}
L::Sample ImuLab::currentSample() const {
  L::Sample s;s.timeUs=imu.sample.atUs;s.sequence=imu.sample.sequence;
  s.sensorTime=imu.sample.sensorTime;s.deltaUs=imu.sample.deltaUs;s.candidateId=detector.count;s.detectorFlags=detector.flags;s.continuity=uint8_t(detector.reason);
  if(imu.sample.temperatureValid)s.flags|=L::TemperatureValid;
  if(imu.sample.temperatureFresh)s.flags|=L::TemperatureFresh;
  if(imu.sample.deltaValid)s.flags|=L::SensorDeltaValid;
  std::memcpy(s.accel,imu.sample.rawAccel,sizeof(s.accel));std::memcpy(s.gyro,imu.sample.rawGyro,sizeof(s.gyro));
  s.temperature=imu.sample.rawTemperature;
  if(imu.sample.accelClipped)s.flags|=L::AccelClip;
  if(imu.sample.gyroClipped)s.flags|=L::GyroClip;
  if(imu.continuityLost)s.flags|=L::TimingGap;
  if(attitude){s.flags|=L::AttitudeValid;s.sideCdeg=int16_t(std::lround(side*100));s.forwardCdeg=int16_t(std::lround(forward*100));}
  if(bias.valid)s.flags|=L::GyroValid;
  if(piezo.valid && uint32_t(millis()-piezo.atMs)<SensorConfig::PIEZO_STALE_MS){s.flags|=L::PiezoValid;s.front=piezo.front;s.rear=piezo.rear;}
  return s;
}
void ImuLab::handle(const L::Frame& f) {
  if(!link.labSessionCurrent(f.session))return;
  if(f.type!=L::Type::Command || f.length!=40 || !f.token)return;
  if(f.session!=lastCommand.session){lastToken=0;responsePending=false;}
  if(f.token==lastToken && f.boot==lastCommand.boot && std::memcmp(f.data,lastCommand.data,40)==0){responsePending=true;return;}
  // Do not overwrite an unsent acknowledgement with another command.
  if(responsePending)return;
  const auto op=L::Op(f.data[0]);L::Result result=L::Result::Accepted;
  if(f.token==lastToken)result=L::Result::Range;
  else if(f.boot!=link.bootId())result=L::Result::WrongBoot;
  else if(op==L::Op::Gyro || op==L::Op::Upright || op==L::Op::Forward || op==L::Op::Neutral)result=startCalibration(op);
  else if(op==L::Op::Retry) {
    if(busy())result=L::Result::Busy;else{imu.retry();attitude=false;}
  } else if(op==L::Op::ClearBody) {
    if(busy())result=L::Result::Busy;
    else {L::Metadata cleared;cleared.who=imu.whoAmI;cleared.revision=body.revision+1;
      if(!saveBody(cleared))result=L::Result::Storage;else{body=cleared;attitude=false;detector.reset();}}
  } else if(op==L::Op::ResetCandidate){detector.reset();} else result=L::Result::Unknown;
  response=L::Frame();response.type=L::Type::Response;response.session=f.session;response.boot=link.bootId();response.token=f.token;response.length=6;
  response.data[0]=uint8_t(op);response.data[1]=uint8_t(result);L::put32(response.data+2,0);
  lastToken=f.token;lastCommand=f;responsePending=true;lastLive=millis()-100;
}
void ImuLab::sendMetadata() {
  L::Frame f;f.boot=link.bootId();L::encodeMetadata(body,f);
  if(link.sendLab(f))lastMeta=millis();
}
void ImuLab::sendLive() {
  L::Live v;v.automaticDetectionEnabled=SensorConfig::ARM_CRASH_DETECTION;
  v.automaticReady=v.automaticDetectionEnabled && automaticReady() && link.automaticEventReady() &&
    piezo.valid && uint32_t(millis()-piezo.atMs)<SensorConfig::PIEZO_STALE_MS;
  v.healthy=imu.healthy();v.who=imu.whoAmI;v.error=uint8_t(imu.error);v.phase=uint8_t(imu.phase);
  v.reg=imu.faultRegister;v.observed=imu.observed;v.expected=imu.expected;
  v.calibrationFlags=body.flags & uint8_t(~L::GyroCalibrated);if(bias.valid)v.calibrationFlags|=L::GyroCalibrated;
  v.samples=imu.samples;v.failures=imu.failures;v.overruns=imu.overruns;v.intervalUs=imu.sample.intervalUs;v.rateHz=rateHz;
  v.sample=currentSample();v.side=side;v.forward=forward;v.trust=trust;std::memcpy(v.bias,bias.bias,sizeof(v.bias));
  v.calibrationOp=uint8_t(calOp);v.calibrationResult=calResult;v.calibrationCount=gyro.count;
  v.candidateCount=detector.count;v.detectorState=detector.state;v.continuity=detector.reason;
  v.rotationHoldUs=detector.rotationHoldUs;v.tiltHoldUs=detector.tiltHoldUs;v.postureChange=detector.change;v.activity=std::sqrt(std::max(0.0f,activitySq));v.revision=body.revision;
  v.rejected=imu.driver.rejectedFrames;v.duplicates=imu.duplicates;v.notReady=imu.notReady;v.cycleUs=imu.maxCycleUs;
  const auto& e=detector.last;v.eventStartUs=e.startUs;v.decisionUs=e.decisionUs;v.eventRotationUs=e.rotationUs;v.eventTiltUs=e.tiltUs;v.eventPeakG=e.peakG;v.eventPeakDps=e.peakDps;v.eventChange=e.change;
  L::Frame f;f.boot=link.bootId();L::encodeLive(v,f);if(link.sendLab(f))lastLive=millis();
}
void ImuLab::tick() {
  if(lastCommand.session && !link.labSessionCurrent(lastCommand.session)){responsePending=false;}
  const bool healthy=imu.healthy();
  if(healthy && (!wasHealthy || !loaded || loadedWho!=imu.whoAmI)){
    bias.load(imu.whoAmI);loadBody();
  }
  if(!healthy){attitude=false;bias.valid=false;detector.invalidate(FallDetector::SensorLost);}
  wasHealthy=healthy;
  if(imu.continuityLost){attitude=false;detector.invalidate(imu.fresh?FallDetector::Gap:FallDetector::Rejected);}
  if(imu.fresh){acquisitionUs+=imu.sample.deltaValid?imu.sample.deltaUs:5000;if(imu.sample.saturated){attitude=false;detector.invalidate(FallDetector::Clipped);}}

  if(calActive && gyro.count && imu.continuityLost)gyro.result=GyroCalibration::Result::Gap;
  gyro.tick(millis(),imu.healthy());if(imu.fresh)gyro.feed(imu.sample);
  if(calActive && gyro.result!=GyroCalibration::Result::Collecting)completeCalibration();
  if(calActive)detector.invalidate(FallDetector::Calibration);
  if(imu.fresh && !calActive && !imu.sample.saturated)updateAttitude();
  const uint32_t nowUs=micros();
  if(uint32_t(nowUs-rateAt)>=1000000u) {
    rateHz=float(imu.samples-rateSamples)*1000000.0f/float(uint32_t(nowUs-rateAt));rateAt=nowUs;rateSamples=imu.samples;
  }
  if(responsePending){if(link.sendLab(response))responsePending=false;return;}
  if(uint32_t(millis()-lastLive)>=100){sendLive();return;}
  if(uint32_t(millis()-lastMeta)>=1000){sendMetadata();return;}
}
