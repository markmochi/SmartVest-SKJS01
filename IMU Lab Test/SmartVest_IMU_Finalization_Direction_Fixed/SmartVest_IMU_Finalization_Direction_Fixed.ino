#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

// ============================================================
// SMARTVEST MPU6500 FINALIZATION LAB - DIRECTION FIXED - DRAM SAFE
// ESP32 SENSING + MPU6500
//
// IMPORTANT FIXES:
// 1) Custom structs are declared before any function that uses them.
// 2) Trial recorder RAM is capped at 10 s / 200 Hz so the classic
//    ESP32-WROOM DRAM region is not overflowed by a large static array.
// ============================================================

// ------------------------- CUSTOM TYPES ----------------------
struct Vec3 {
  float x;
  float y;
  float z;
};

struct CaptureStats {
  Vec3 acceleration;
  Vec3 gyro;
  float meanMagnitude;
  float magnitudeStd;
  int validSamples;
};

struct TrialSample {
  uint32_t timeUs;
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  int16_t sideCdeg;
  int16_t forwardCdeg;
};

// Explicit prototypes for functions that use custom structs.
// This prevents Arduino's sketch preprocessor from inventing
// invalid prototypes before it knows the custom types.
bool captureStablePose(CaptureStats &stats, int sampleCount, int delayMs);
bool performUprightCalibration(CaptureStats &stats);
bool performForwardCalibration(CaptureStats &stats, float &detectedLeanAngle);
bool captureNeutralPosture(CaptureStats &stats);

// --------------------------- WIFI ----------------------------
#include "LabWiFi.h"
const char* HOSTNAME = "smartvest-imu";

WebServer server(80);
Preferences preferences;

// ---------------------------- I2C ----------------------------
#define SDA_PIN 21
#define SCL_PIN 22
#define MPU_ADDR_LOW  0x68
#define MPU_ADDR_HIGH 0x69

// ------------------------- MPU REGISTERS ----------------------
#define REG_SMPLRT_DIV       0x19
#define REG_CONFIG           0x1A
#define REG_GYRO_CONFIG      0x1B
#define REG_ACCEL_CONFIG     0x1C
#define REG_ACCEL_CONFIG2    0x1D
#define REG_ACCEL_XOUT_H     0x3B
#define REG_PWR_MGMT_1       0x6B
#define REG_PWR_MGMT_2       0x6C
#define REG_WHO_AM_I         0x75

// ----------------------- SENSOR SETTINGS ---------------------
// ±16g and ±2000 dps for characterization so peaks are less
// likely to clip during controlled tests.
const float ACCEL_SCALE = 2048.0f;
const float GYRO_SCALE  = 16.4f;
const uint32_t SAMPLE_INTERVAL_US = 5000; // ~200 Hz

// ---------------------- ATTITUDE FILTER ----------------------
const float ACCEL_ORIENTATION_LPF = 0.12f;
const float MAX_ACCEL_CORRECTION_GAIN = 0.020f;
const float VISUAL_ANGLE_DEADBAND = 0.5f;

// Literature/reference markers only. Not final SmartVest values.
const float REF_FREE_FALL_G = 0.50f;
const float REF_HIGH_ACCEL_G = 7.14f;
const float REF_HIGH_ROTATION_DPS = 114.6f;

// ------------------------- VECTOR MATH ------------------------
Vec3 makeVec(float x, float y, float z) {
  Vec3 v = {x, y, z};
  return v;
}

float vecDot(const Vec3& a, const Vec3& b) {
  return a.x*b.x + a.y*b.y + a.z*b.z;
}

Vec3 vecCross(const Vec3& a, const Vec3& b) {
  return makeVec(
    a.y*b.z - a.z*b.y,
    a.z*b.x - a.x*b.z,
    a.x*b.y - a.y*b.x
  );
}

Vec3 vecScale(const Vec3& v, float s) {
  return makeVec(v.x*s, v.y*s, v.z*s);
}

Vec3 vecSubtract(const Vec3& a, const Vec3& b) {
  return makeVec(a.x-b.x, a.y-b.y, a.z-b.z);
}

float vecMagnitude(const Vec3& v) {
  return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

Vec3 vecNormalize(const Vec3& v) {
  float m = vecMagnitude(v);
  if (m < 0.000001f) return makeVec(0,0,0);
  return vecScale(v, 1.0f/m);
}

// ---------------------- BODY CALIBRATION ---------------------
// These rider-coordinate basis vectors are expressed in raw
// MPU sensor coordinates. They are learned by the wizard.
Vec3 rightBasis   = {1,0,0};
Vec3 forwardBasis = {0,1,0};
Vec3 upBasis      = {0,0,1};

bool bodyCalibrationValid = false;
bool uprightCaptured = false;
float gravityBaselineG = 1.0f;
float forwardCalibrationAngle = 0.0f;
float sideNeutralOffset = 0.0f;
float forwardNeutralOffset = 0.0f;

// ------------------------- MPU STATE -------------------------
uint8_t mpuAddress = 0;
uint8_t whoAmI = 0;
bool imuOnline = false;
bool imuCalibrating = false;
char sensorName[24] = "Not detected";
uint32_t consecutiveReadErrors = 0;
uint32_t totalReadErrors = 0;
unsigned long lastIMURetryMs = 0;
const unsigned long IMU_RETRY_INTERVAL_MS = 2000;

// Raw registers
int16_t rawAX=0, rawAY=0, rawAZ=0;
int16_t rawGX=0, rawGY=0, rawGZ=0;

// Engineering units
float accelX=0, accelY=0, accelZ=0;
float gyroX=0, gyroY=0, gyroZ=0;

// Gyro biases
float gyroBiasX=0, gyroBiasY=0, gyroBiasZ=0;

// Rider coordinate data
float bodyRightAccel=0, bodyForwardAccel=0, bodyUpAccel=0;
float bodyRightGyro=0, bodyForwardGyro=0, bodyUpGyro=0;

// Filtered attitude state
float filteredRightAccel=0;
float filteredForwardAccel=0;
float filteredUpAccel=1;
bool filteredAccelInitialized=false;
float sideTilt=0;
float forwardTilt=0;
bool attitudeInitialized=false;
float gravityTrust=0;

// Motion state
float accelerationMagnitude=0;
float angularSpeedMagnitude=0;
bool freeFallCandidate=false;
bool highAccelCandidate=false;
bool highRotationCandidate=false;
bool motionCandidate=false;
bool accelSaturation=false;
bool gyroSaturation=false;

// Sample rate
uint32_t previousSampleUs=0;
uint32_t sampleCounter=0;
uint32_t samplesPerSecond=0;
unsigned long sampleRateTimer=0;

// WiFi maintenance
unsigned long lastWiFiCheckMs=0;

// ------------------------ TRIAL BUFFER -----------------------
// ESP32-WROOM has no PSRAM. The previous 4000-sample buffer used
// about 80 kB of internal DRAM and caused the linker overflow.
//
// For deterministic 200 Hz calibration logging we intentionally
// keep the recorder in RAM (no flash-write stalls while sampling).
// 10 s × 200 Hz = 2000 samples. At ~20 bytes/sample this is ~40 kB.
// This leaves substantially more DRAM for Wi-Fi/WebServer runtime.
const uint16_t MAX_TRIAL_SECONDS = 10;
const uint16_t MAX_TRIAL_SAMPLES = MAX_TRIAL_SECONDS * 200;
TrialSample trialBuffer[MAX_TRIAL_SAMPLES];

static_assert(sizeof(TrialSample) <= 20,
              "TrialSample grew unexpectedly; check ESP32 DRAM usage.");
bool recordingTrial=false;
bool trialComplete=false;
uint16_t trialSampleCount=0;
uint16_t trialTargetSamples=0;
uint32_t trialStartUs=0;
char trialType[40]="None";
Vec3 trialRightBasis, trialForwardBasis, trialUpBasis;
float trialSideNeutral=0;
float trialForwardNeutral=0;

// -------------------------- HELPERS --------------------------
float wrapAngle180(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

float angleDifference(float target, float current) {
  return wrapAngle180(target-current);
}

float visualDeadband(float v) {
  return (fabsf(v) < VISUAL_ANGLE_DEADBAND) ? 0.0f : v;
}

// --------------------- PERSISTENT CALIBRATION ----------------
void saveBodyCalibration() {
  preferences.begin("imuCalV2", false);
  preferences.putBool("valid", bodyCalibrationValid);
  preferences.putFloat("rx", rightBasis.x);
  preferences.putFloat("ry", rightBasis.y);
  preferences.putFloat("rz", rightBasis.z);
  preferences.putFloat("fx", forwardBasis.x);
  preferences.putFloat("fy", forwardBasis.y);
  preferences.putFloat("fz", forwardBasis.z);
  preferences.putFloat("ux", upBasis.x);
  preferences.putFloat("uy", upBasis.y);
  preferences.putFloat("uz", upBasis.z);
  preferences.putFloat("grav", gravityBaselineG);
  preferences.putFloat("lean", forwardCalibrationAngle);
  preferences.putFloat("sneutral", sideNeutralOffset);
  preferences.putFloat("fneutral", forwardNeutralOffset);
  preferences.end();
}

void loadBodyCalibration() {
  preferences.begin("imuCalV2", true);
  bodyCalibrationValid = preferences.getBool("valid", false);
  if (bodyCalibrationValid) {
    rightBasis = makeVec(
      preferences.getFloat("rx",1),
      preferences.getFloat("ry",0),
      preferences.getFloat("rz",0));
    forwardBasis = makeVec(
      preferences.getFloat("fx",0),
      preferences.getFloat("fy",1),
      preferences.getFloat("fz",0));
    upBasis = makeVec(
      preferences.getFloat("ux",0),
      preferences.getFloat("uy",0),
      preferences.getFloat("uz",1));
    gravityBaselineG = preferences.getFloat("grav",1);
    forwardCalibrationAngle = preferences.getFloat("lean",0);
    sideNeutralOffset = preferences.getFloat("sneutral",0);
    forwardNeutralOffset = preferences.getFloat("fneutral",0);
    uprightCaptured = true;
  }
  preferences.end();
}

void clearBodyCalibration() {
  preferences.begin("imuCalV2", false);
  preferences.clear();
  preferences.end();

  bodyCalibrationValid=false;
  uprightCaptured=false;
  gravityBaselineG=1.0f;
  forwardCalibrationAngle=0;
  sideNeutralOffset=0;
  forwardNeutralOffset=0;
  attitudeInitialized=false;
  filteredAccelInitialized=false;
}

// --------------------------- I2C -----------------------------
bool pingAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission()==0;
}

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission()==0;
}

bool readRegisters(uint8_t reg, uint8_t* buffer, uint8_t length) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false)!=0) return false;

  uint8_t received = Wire.requestFrom((uint8_t)mpuAddress, length, (uint8_t)true);
  if (received != length) return false;
  for (uint8_t i=0; i<length; i++) buffer[i]=Wire.read();
  return true;
}

void updateSensorName() {
  if (whoAmI==0x70) strcpy(sensorName,"MPU6500");
  else if (whoAmI==0x71) strcpy(sensorName,"MPU9250");
  else if (whoAmI==0x73) strcpy(sensorName,"MPU9255");
  else strcpy(sensorName,"Unknown");
}

bool detectIMU() {
  const uint8_t addresses[2] = {MPU_ADDR_LOW, MPU_ADDR_HIGH};
  for (int i=0;i<2;i++) {
    if (!pingAddress(addresses[i])) continue;
    mpuAddress=addresses[i];
    uint8_t id=0;
    if (!readRegisters(REG_WHO_AM_I,&id,1)) continue;
    if (id==0x70 || id==0x71 || id==0x73) {
      whoAmI=id;
      updateSensorName();
      return true;
    }
  }
  return false;
}

bool configureIMU() {
  if (!writeRegister(REG_PWR_MGMT_1,0x80)) return false;
  delay(120);
  if (!writeRegister(REG_PWR_MGMT_1,0x01)) return false;
  delay(100);
  if (!writeRegister(REG_PWR_MGMT_2,0x00)) return false;
  if (!writeRegister(REG_CONFIG,0x03)) return false;
  if (!writeRegister(REG_GYRO_CONFIG,0x18)) return false;  // ±2000 dps
  if (!writeRegister(REG_ACCEL_CONFIG,0x18)) return false; // ±16g
  if (!writeRegister(REG_ACCEL_CONFIG2,0x03)) return false;
  if (!writeRegister(REG_SMPLRT_DIV,0x04)) return false;   // ~200Hz
  delay(50);

  uint8_t g=0,a=0;
  if (!readRegisters(REG_GYRO_CONFIG,&g,1)) return false;
  if (!readRegisters(REG_ACCEL_CONFIG,&a,1)) return false;
  return ((g&0x18)==0x18 && (a&0x18)==0x18);
}

bool readMotion() {
  uint8_t d[14];
  if (!readRegisters(REG_ACCEL_XOUT_H,d,14)) return false;

  rawAX=(int16_t)((d[0]<<8)|d[1]);
  rawAY=(int16_t)((d[2]<<8)|d[3]);
  rawAZ=(int16_t)((d[4]<<8)|d[5]);
  rawGX=(int16_t)((d[8]<<8)|d[9]);
  rawGY=(int16_t)((d[10]<<8)|d[11]);
  rawGZ=(int16_t)((d[12]<<8)|d[13]);

  accelX=rawAX/ACCEL_SCALE;
  accelY=rawAY/ACCEL_SCALE;
  accelZ=rawAZ/ACCEL_SCALE;
  gyroX=rawGX/GYRO_SCALE;
  gyroY=rawGY/GYRO_SCALE;
  gyroZ=rawGZ/GYRO_SCALE;
  return true;
}

// ------------------------ GYRO CAL ---------------------------
bool calibrateGyroscope() {
  imuCalibrating=true;
  Serial.println("Gyro calibration: KEEP VEST STILL...");
  delay(700);

  const int samples=800;
  double sx=0, sy=0, sz=0;
  int valid=0;

  for (int i=0;i<samples;i++) {
    if (readMotion()) {
      sx+=gyroX; sy+=gyroY; sz+=gyroZ;
      valid++;
    }
    delay(2);
  }

  imuCalibrating=false;
  if (valid < samples/2) return false;

  gyroBiasX=sx/valid;
  gyroBiasY=sy/valid;
  gyroBiasZ=sz/valid;

  Serial.printf("Gyro bias X=%.4f Y=%.4f Z=%.4f\n", gyroBiasX,gyroBiasY,gyroBiasZ);
  return true;
}

// --------------------- BODY TRANSFORMATION ------------------
void updateBodyCoordinates() {
  Vec3 a=makeVec(accelX,accelY,accelZ);
  Vec3 g=makeVec(gyroX-gyroBiasX,gyroY-gyroBiasY,gyroZ-gyroBiasZ);

  bodyRightAccel=vecDot(a,rightBasis);
  bodyForwardAccel=vecDot(a,forwardBasis);
  bodyUpAccel=vecDot(a,upBasis);

  bodyRightGyro=vecDot(g,rightBasis);
  bodyForwardGyro=vecDot(g,forwardBasis);
  bodyUpGyro=vecDot(g,upBasis);
}

float calculateGravityTrust(float magnitude) {
  float baseline=(gravityBaselineG<0.25f)?1.0f:gravityBaselineG;
  float relativeError=fabsf(magnitude-baseline)/baseline;
  if (relativeError<=0.08f) return 1.0f;
  if (relativeError>=0.35f) return 0.0f;
  return (0.35f-relativeError)/(0.35f-0.08f);
}

void initializeAttitudeFromAccel() {
  if (!bodyCalibrationValid) {
    attitudeInitialized=false;
    return;
  }

  updateBodyCoordinates();
  filteredRightAccel=bodyRightAccel;
  filteredForwardAccel=bodyForwardAccel;
  filteredUpAccel=bodyUpAccel;
  filteredAccelInitialized=true;

  sideTilt=atan2f(-filteredRightAccel,filteredUpAccel)*180.0f/PI;
  forwardTilt=atan2f(
    -filteredForwardAccel,
    sqrtf(filteredRightAccel*filteredRightAccel + filteredUpAccel*filteredUpAccel)
  )*180.0f/PI;

  attitudeInitialized=true;
}

void updateAttitude(float dt) {
  if (!bodyCalibrationValid) return;

  updateBodyCoordinates();

  if (!filteredAccelInitialized) {
    filteredRightAccel=bodyRightAccel;
    filteredForwardAccel=bodyForwardAccel;
    filteredUpAccel=bodyUpAccel;
    filteredAccelInitialized=true;
  }

  filteredRightAccel += ACCEL_ORIENTATION_LPF*(bodyRightAccel-filteredRightAccel);
  filteredForwardAccel += ACCEL_ORIENTATION_LPF*(bodyForwardAccel-filteredForwardAccel);
  filteredUpAccel += ACCEL_ORIENTATION_LPF*(bodyUpAccel-filteredUpAccel);

  float accelSide = atan2f(-filteredRightAccel,filteredUpAccel)*180.0f/PI;
  float accelForward = atan2f(
    -filteredForwardAccel,
    sqrtf(filteredRightAccel*filteredRightAccel + filteredUpAccel*filteredUpAccel)
  )*180.0f/PI;

  if (!attitudeInitialized) {
    sideTilt=accelSide;
    forwardTilt=accelForward;
    attitudeInitialized=true;
    return;
  }

  // Side lean = rotation about rider-forward axis.
  // Forward/back lean = rotation about rider-right axis.
  float predictedSide=sideTilt + bodyForwardGyro*dt;
  float predictedForward=forwardTilt - bodyRightGyro*dt;
  float gain=MAX_ACCEL_CORRECTION_GAIN*gravityTrust;

  sideTilt=wrapAngle180(predictedSide + gain*angleDifference(accelSide,predictedSide));
  forwardTilt=wrapAngle180(predictedForward + gain*angleDifference(accelForward,predictedForward));
}

// --------------------- STABLE POSE CAPTURE ------------------
bool captureStablePose(CaptureStats &stats, int sampleCount, int delayMs) {
  double ax=0,ay=0,az=0,gx=0,gy=0,gz=0;
  double magSum=0, magSqSum=0;
  int valid=0;

  for (int i=0;i<sampleCount;i++) {
    if (readMotion()) {
      float m=sqrtf(accelX*accelX + accelY*accelY + accelZ*accelZ);
      ax+=accelX; ay+=accelY; az+=accelZ;
      gx+=gyroX; gy+=gyroY; gz+=gyroZ;
      magSum+=m;
      magSqSum+=m*m;
      valid++;
    }
    delay(delayMs);
  }

  if (valid < sampleCount/2) return false;

  stats.acceleration=makeVec(ax/valid,ay/valid,az/valid);
  stats.gyro=makeVec(gx/valid,gy/valid,gz/valid);
  stats.meanMagnitude=magSum/valid;
  float variance=(magSqSum/valid)-(stats.meanMagnitude*stats.meanMagnitude);
  if (variance<0) variance=0;
  stats.magnitudeStd=sqrtf(variance);
  stats.validSamples=valid;
  return true;
}

bool performUprightCalibration(CaptureStats &stats) {
  imuCalibrating=true;
  bool ok=captureStablePose(stats,400,5);
  imuCalibrating=false;

  if (!ok) return false;
  if (stats.magnitudeStd>0.08f) return false;

  upBasis=vecNormalize(stats.acceleration);
  gravityBaselineG=stats.meanMagnitude;

  // Update gyro bias using this truly stationary capture.
  gyroBiasX=stats.gyro.x;
  gyroBiasY=stats.gyro.y;
  gyroBiasZ=stats.gyro.z;

  uprightCaptured=true;
  bodyCalibrationValid=false;
  sideNeutralOffset=0;
  forwardNeutralOffset=0;
  filteredAccelInitialized=false;
  attitudeInitialized=false;
  return true;
}

bool performForwardCalibration(CaptureStats &stats, float &detectedLeanAngle) {
  if (!uprightCaptured) return false;

  imuCalibrating=true;
  bool ok=captureStablePose(stats,350,5);
  imuCalibrating=false;

  if (!ok) return false;
  if (stats.magnitudeStd>0.08f) return false;

  Vec3 g=stats.acceleration;
  float alongUp=vecDot(g,upBasis);
  Vec3 horizontal=vecSubtract(g,vecScale(upBasis,alongUp));
  float horizontalMagnitude=vecMagnitude(horizontal);

  detectedLeanAngle=atan2f(horizontalMagnitude,alongUp)*180.0f/PI;

  // Reject too-small pose because direction becomes noisy.
  if (detectedLeanAngle<15.0f || detectedLeanAngle>60.0f) return false;

  // Accelerometers measure specific force opposite gravity.
  // During a forward lean, the horizontal gravity/specific-force
  // projection points toward the rider's BACK. Negate it so
  // forwardBasis really means the rider's physical FORWARD.
  forwardBasis=vecNormalize(vecScale(horizontal,-1.0f));
  rightBasis=vecNormalize(vecCross(forwardBasis,upBasis));
  forwardBasis=vecNormalize(vecCross(upBasis,rightBasis));

  forwardCalibrationAngle=detectedLeanAngle;
  bodyCalibrationValid=true;
  filteredAccelInitialized=false;
  attitudeInitialized=false;
  sideNeutralOffset=0;
  forwardNeutralOffset=0;
  saveBodyCalibration();
  return true;
}

bool captureNeutralPosture(CaptureStats &stats) {
  if (!bodyCalibrationValid) return false;

  imuCalibrating=true;
  bool ok=captureStablePose(stats,250,5);
  imuCalibrating=false;
  if (!ok) return false;

  Vec3 a=stats.acceleration;
  float r=vecDot(a,rightBasis);
  float f=vecDot(a,forwardBasis);
  float u=vecDot(a,upBasis);

  float staticSide=atan2f(-r,u)*180.0f/PI;
  float staticForward=atan2f(-f,sqrtf(r*r+u*u))*180.0f/PI;

  sideNeutralOffset=staticSide;
  forwardNeutralOffset=staticForward;
  sideTilt=staticSide;
  forwardTilt=staticForward;
  filteredRightAccel=r;
  filteredForwardAccel=f;
  filteredUpAccel=u;
  filteredAccelInitialized=true;
  attitudeInitialized=true;
  saveBodyCalibration();
  return true;
}

// ------------------------- TRIAL LOG -------------------------
void recordTrialSample() {
  if (!recordingTrial) return;
  if (trialSampleCount>=trialTargetSamples) {
    recordingTrial=false;
    trialComplete=true;
    return;
  }

  TrialSample &s=trialBuffer[trialSampleCount];
  s.timeUs=micros()-trialStartUs;
  s.ax=rawAX; s.ay=rawAY; s.az=rawAZ;
  s.gx=rawGX; s.gy=rawGY; s.gz=rawGZ;

  float ds=sideTilt-trialSideNeutral;
  float df=forwardTilt-trialForwardNeutral;
  ds=constrain(ds,-327.0f,327.0f);
  df=constrain(df,-327.0f,327.0f);
  s.sideCdeg=(int16_t)lroundf(ds*100.0f);
  s.forwardCdeg=(int16_t)lroundf(df*100.0f);

  trialSampleCount++;
  if (trialSampleCount>=trialTargetSamples) {
    recordingTrial=false;
    trialComplete=true;
  }
}

// ------------------------- IMU UPDATE ------------------------
void updateIMU() {
  if (!imuOnline || imuCalibrating) return;

  uint32_t nowUs=micros();
  uint32_t elapsedUs=nowUs-previousSampleUs;
  if (elapsedUs<SAMPLE_INTERVAL_US) return;
  previousSampleUs=nowUs;

  float dt=elapsedUs/1000000.0f;
  if (dt<=0 || dt>0.05f) dt=0.005f;

  if (!readMotion()) {
    consecutiveReadErrors++;
    totalReadErrors++;
    if (consecutiveReadErrors>=5) {
      imuOnline=false;
      lastIMURetryMs=millis();
    }
    return;
  }

  consecutiveReadErrors=0;

  accelerationMagnitude=sqrtf(accelX*accelX + accelY*accelY + accelZ*accelZ);

  float cgx=gyroX-gyroBiasX;
  float cgy=gyroY-gyroBiasY;
  float cgz=gyroZ-gyroBiasZ;
  angularSpeedMagnitude=sqrtf(cgx*cgx + cgy*cgy + cgz*cgz);

  gravityTrust=calculateGravityTrust(accelerationMagnitude);

  if (bodyCalibrationValid) updateAttitude(dt);

  freeFallCandidate=accelerationMagnitude<REF_FREE_FALL_G;
  highAccelCandidate=accelerationMagnitude>REF_HIGH_ACCEL_G;
  highRotationCandidate=angularSpeedMagnitude>REF_HIGH_ROTATION_DPS;
  motionCandidate=freeFallCandidate || highAccelCandidate || highRotationCandidate;

  accelSaturation=(abs(rawAX)>30000 || abs(rawAY)>30000 || abs(rawAZ)>30000);
  gyroSaturation=(abs(rawGX)>30000 || abs(rawGY)>30000 || abs(rawGZ)>30000);

  recordTrialSample();
  sampleCounter++;
}

bool startIMU() {
  imuOnline=false;

  bool found=false;
  for (int i=0;i<10;i++) {
    if (detectIMU()) { found=true; break; }
    delay(100);
  }
  if (!found) return false;
  if (!configureIMU()) return false;
  if (!calibrateGyroscope()) return false;
  if (!readMotion()) return false;

  if (bodyCalibrationValid) initializeAttitudeFromAccel();

  previousSampleUs=micros();
  consecutiveReadErrors=0;
  imuOnline=true;

  Serial.printf("Detected %s | I2C 0x%02X | WHO_AM_I 0x%02X\n", sensorName,mpuAddress,whoAmI);
  Serial.printf("Body calibration: %s\n", bodyCalibrationValid?"VALID":"REQUIRED");
  return true;
}

void maintainIMU() {
  if (imuOnline || imuCalibrating) return;
  if (millis()-lastIMURetryMs<IMU_RETRY_INTERVAL_MS) return;
  lastIMURetryMs=millis();
  startIMU();
}

void updateSampleRate() {
  if (millis()-sampleRateTimer>=1000) {
    samplesPerSecond=sampleCounter;
    sampleCounter=0;
    sampleRateTimer=millis();
  }
}

// ============================================================
// WEB UI
// ============================================================
const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartVest Motion Lab</title>
<style>
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 50% -10%,#1b2b43,#080c12 48%,#05070b);color:#f4f7fb;font-family:Inter,system-ui,-apple-system,"Segoe UI",sans-serif;min-height:100vh}.wrap{max-width:1300px;margin:auto;padding:24px}.head h1{margin:0;font-size:28px}.head p{color:#8ea0b8;margin:6px 0 18px}.grid{display:grid;gap:12px}.status{grid-template-columns:repeat(5,1fr)}.data{grid-template-columns:repeat(4,1fr)}.raw{grid-template-columns:repeat(3,1fr)}.events{grid-template-columns:repeat(4,1fr)}.card{background:linear-gradient(145deg,rgba(24,34,48,.97),rgba(14,21,31,.97));border:1px solid #263345;border-radius:18px;padding:16px}.label{color:#8ea0b8;font-size:11px;font-weight:800;letter-spacing:.08em;text-transform:uppercase}.value{font-size:24px;font-weight:750;margin-top:6px}.small{font-size:15px;font-weight:700;margin-top:6px}.good{color:#6ee7a8}.warn{color:#f2cb70}.bad{color:#ff7070}.section{margin:26px 0 10px;color:#b7c4d5;font-size:12px;font-weight:800;letter-spacing:.11em;text-transform:uppercase}.visuals{display:grid;grid-template-columns:1fr 1fr;gap:14px}.visual{position:relative;min-height:485px;overflow:hidden;border:1px solid #263345;border-radius:22px;background:radial-gradient(circle at 50% 35%,#17263a,#0c131d 72%)}.viewtitle{position:absolute;z-index:3;top:18px;left:20px}.viewtitle b{display:block}.viewtitle span{font-size:12px;color:#8ea0b8}.chip{position:absolute;top:18px;right:20px;z-index:3;padding:8px 12px;border-radius:12px;background:#101823;border:1px solid #2c3a4e;font-size:20px;font-weight:800}svg{width:100%;height:485px}.ref{stroke:#536278;stroke-width:2;stroke-dasharray:7 8;opacity:.65}.bike{fill:none;stroke:#64758b;stroke-width:10;stroke-linecap:round;stroke-linejoin:round}.wheel{fill:none;stroke:#4f6075;stroke-width:9}.limb{fill:none;stroke:#d9e1ea;stroke-width:17;stroke-linecap:round;stroke-linejoin:round}.helmet{fill:#dce4ec;stroke:#667991;stroke-width:4}.visor{fill:#37546b}.vest{fill:#e8ef23;stroke:#adb61d;stroke-width:3}.pants{fill:#293847}.calgrid{display:grid;grid-template-columns:1.25fr .75fr;gap:14px}.step{display:grid;grid-template-columns:42px 1fr auto;gap:12px;align-items:center;padding:14px;border:1px solid #29384b;border-radius:15px;background:#0f1722;margin-bottom:10px}.num{width:38px;height:38px;border-radius:50%;background:#223047;display:grid;place-items:center;font-weight:800}.desc{color:#8ea0b8;font-size:12px;line-height:1.45;margin-top:3px}button,select{min-height:44px;border-radius:11px;font-size:14px}button{border:0;padding:0 17px;background:#edf2f7;color:#111923;font-weight:750;cursor:pointer}button.secondary{background:#202d3e;color:#edf3f8;border:1px solid #35465d}button.danger{background:#402326;color:#ffc6c6;border:1px solid #65343a}select{background:#172231;color:white;border:1px solid #34465e;padding:0 12px}.controls{display:flex;flex-wrap:wrap;gap:9px;align-items:center}.note{color:#8ea0b8;font-size:12px;line-height:1.55;margin-top:10px}.progress{height:10px;background:#0a111a;border-radius:99px;overflow:hidden;margin-top:14px}.progress div{height:100%;width:0;background:#cbd5e2;transition:width .15s linear}.event{font-size:19px;font-weight:800;margin-top:8px}@media(max-width:900px){.status,.data,.events{grid-template-columns:repeat(2,1fr)}.visuals,.calgrid{grid-template-columns:1fr}}@media(max-width:520px){.wrap{padding:14px}.status,.data,.raw,.events{grid-template-columns:1fr}.step{grid-template-columns:42px 1fr}.step button{grid-column:1/-1}}
</style></head><body><div class="wrap"><div class="head"><h1>SmartVest Motion Calibration Lab</h1><p>MPU6500 • rider-coordinate calibration • controlled crash-motion characterization</p></div>
<div class="grid status"><div class="card"><div class="label">System</div><div class="small good">LIVE</div></div><div class="card"><div class="label">IMU</div><div class="small" id="imu">Checking</div></div><div class="card"><div class="label">Mount calibration</div><div class="small" id="cal">Checking</div></div><div class="card"><div class="label">Attitude reference</div><div class="small" id="att">--</div></div><div class="card"><div class="label">Sampling</div><div class="small" id="rate">--</div></div></div>
<div class="section">Rider posture — 1:1 measured angle</div><div class="visuals">
<div class="visual"><div class="viewtitle"><b>Front View</b><span>Side / lateral torso lean</span></div><div class="chip" id="frontChip">--</div><svg viewBox="0 0 420 485"><line x1="210" y1="70" x2="210" y2="420" class="ref"/><g id="frontRider"><ellipse cx="210" cy="137" rx="34" ry="38" class="helmet"/><path d="M184 135 Q210 113 237 135 L230 154 Q210 164 190 154 Z" class="visor"/><path d="M171 190 L133 231 L145 273" class="limb"/><path d="M249 190 L287 231 L275 273" class="limb"/><path d="M173 175 Q210 160 247 175 L258 271 Q210 294 162 271 Z" class="vest"/><path d="M171 268 Q210 251 249 268 L243 302 Q210 315 177 302 Z" class="pants"/><path d="M190 297 L169 358 L151 404" class="limb"/><path d="M230 297 L251 358 L269 404" class="limb"/></g><circle cx="115" cy="413" r="42" class="wheel"/><circle cx="305" cy="413" r="42" class="wheel"/><path d="M115 413 L165 345 L235 345 L305 413 M165 345 L260 413 M150 345 L270 345" class="bike"/></svg></div>
<div class="visual"><div class="viewtitle"><b>Side View</b><span>Forward / backward torso lean</span></div><div class="chip" id="sideChip">--</div><svg viewBox="0 0 420 485"><line x1="205" y1="70" x2="205" y2="420" class="ref"/><g id="sideRider"><ellipse cx="205" cy="136" rx="31" ry="38" class="helmet"/><path d="M210 124 Q240 132 236 151 Q221 159 206 153 Z" class="visor"/><path d="M181 176 Q208 162 232 180 L240 272 Q208 289 177 267 Z" class="vest"/><path d="M226 196 L268 229 L297 230" class="limb"/><path d="M180 267 Q211 255 240 273 L231 302 Q205 312 179 299 Z" class="pants"/><path d="M205 297 L170 348 L144 399" class="limb"/><path d="M222 297 L255 349 L280 397" class="limb"/></g><circle cx="112" cy="413" r="42" class="wheel"/><circle cx="307" cy="413" r="42" class="wheel"/><path d="M112 413 L168 346 L242 346 L307 413 M168 346 L256 413 M154 346 L269 346 M266 346 L294 291" class="bike"/></svg></div></div>
<div class="section">Motion measurements</div><div class="grid data"><div class="card"><div class="label">Side lean</div><div class="value" id="sideTilt">--</div></div><div class="card"><div class="label">Forward lean</div><div class="value" id="forwardTilt">--</div></div><div class="card"><div class="label">Acceleration magnitude</div><div class="value" id="amag">--</div></div><div class="card"><div class="label">Angular speed</div><div class="value" id="gmag">--</div></div><div class="card"><div class="label">Rider right accel</div><div class="value" id="br">--</div></div><div class="card"><div class="label">Rider forward accel</div><div class="value" id="bf">--</div></div><div class="card"><div class="label">Rider up accel</div><div class="value" id="bu">--</div></div><div class="card"><div class="label">Gravity confidence</div><div class="value" id="trust">--</div></div></div>
<div class="section">Crash-motion reference indicators</div><div class="grid events"><div class="card"><div class="label">Free fall</div><div class="event" id="ff">--</div><div class="note">Reference &lt; 0.50 g</div></div><div class="card"><div class="label">High acceleration</div><div class="event" id="ha">--</div><div class="note">Reference &gt; 7.14 g</div></div><div class="card"><div class="label">High rotation</div><div class="event" id="hr">--</div><div class="note">Reference &gt; 114.6 °/s</div></div><div class="card"><div class="label">IMU candidate</div><div class="event" id="mc">--</div><div class="note">Piezo confirmation still required for final crash confirmation.</div></div></div>
<div class="section">Mount calibration wizard</div><div class="calgrid"><div class="card"><div class="step"><div class="num">1</div><div><b>Capture Upright</b><div class="desc">Wear or secure the vest in the final mounting position. Hold completely still. This establishes rider-UP regardless of PCB orientation.</div></div><button id="upright">Capture Upright</button></div><div class="step"><div class="num">2</div><div><b>Capture Forward Direction</b><div class="desc">Lean forward about 20–30° and HOLD still. Do not twist or lean sideways. This derives rider-FORWARD and rider-RIGHT.</div></div><button id="forward">Capture Forward</button></div><div class="step"><div class="num">3</div><div><b>Set Neutral Riding Posture</b><div class="desc">Return to your normal upright/riding posture and hold still. This removes normal posture bias without changing raw data.</div></div><button id="neutral">Set Neutral</button></div><div class="note" id="calmsg">Calibration has not started.</div></div><div class="card"><div class="label">Calibration diagnostics</div><div class="small" id="diag">--</div><div class="note">Calibration is stored in ESP32 NVS. Recalibrate any time the MPU board is physically moved or rotated.</div><div class="controls" style="margin-top:16px"><button class="danger" id="clear">Clear Calibration</button></div></div></div>
<div class="section">Raw MPU6500 diagnostics</div><div class="grid raw"><div class="card"><div class="label">Accel X</div><div class="value" id="ax">--</div></div><div class="card"><div class="label">Accel Y</div><div class="value" id="ay">--</div></div><div class="card"><div class="label">Accel Z</div><div class="value" id="az">--</div></div><div class="card"><div class="label">Gyro X</div><div class="value" id="gx">--</div></div><div class="card"><div class="label">Gyro Y</div><div class="value" id="gy">--</div></div><div class="card"><div class="label">Gyro Z</div><div class="value" id="gz">--</div></div></div>
<div class="section">Controlled trial recorder</div><div class="card"><div class="controls"><select id="type"><option>Stationary_0deg</option><option>Side_Right_15</option><option>Side_Right_30</option><option>Side_Right_45</option><option>Side_Right_60</option><option>Side_Left_15</option><option>Side_Left_30</option><option>Side_Left_45</option><option>Side_Left_60</option><option>Forward_15</option><option>Forward_30</option><option>Forward_45</option><option>Forward_60</option><option>Walking</option><option>RapidMovement</option><option>ControlledDrop</option><option>PendulumImpact</option><option>Other</option></select><select id="duration"><option value="5">5 seconds</option><option value="10" selected>10 seconds</option></select><button id="start">Start Trial</button><button class="secondary" id="stop">Stop</button><button class="secondary" id="download">Download CSV</button></div><div class="progress"><div id="bar"></div></div><div class="note" id="trial">No trial recorded.</div></div>
</div>
<script>
const $=id=>document.getElementById(id);let vs=0,vf=0;
function t(id,v){$(id).textContent=v}function state(id,on,label){let e=$(id);e.textContent=on?label:'NO';e.className='event '+(on?'bad':'good')}
async function poll(){try{let c=new AbortController(),to=setTimeout(()=>c.abort(),1200);let r=await fetch('/api/data?t='+Date.now(),{cache:'no-store',signal:c.signal});clearTimeout(to);let d=await r.json();t('imu',d.imuOnline?d.sensor+' ONLINE':'OFFLINE');$('imu').className='small '+(d.imuOnline?'good':'bad');t('cal',d.calibrated?'CALIBRATED':'CALIBRATION REQUIRED');$('cal').className='small '+(d.calibrated?'good':'warn');let trust=Math.round(d.gravityTrust*100);t('att',d.calibrated?(trust>=65?'GRAVITY LOCK':'GYRO-DOMINANT'):'UNAVAILABLE');t('rate',d.sampleRate+' Hz');t('trust',trust+'%');t('diag',d.calibrated?'Forward calibration '+d.calibrationLean.toFixed(1)+'° | gravity baseline '+d.gravityBaseline.toFixed(3)+' g':'Complete Steps 1–3 before relying on posture angles.');t('ax',d.ax.toFixed(4)+' g');t('ay',d.ay.toFixed(4)+' g');t('az',d.az.toFixed(4)+' g');t('gx',d.gx.toFixed(2)+' °/s');t('gy',d.gy.toFixed(2)+' °/s');t('gz',d.gz.toFixed(2)+' °/s');t('amag',d.accelMag.toFixed(3)+' g');t('gmag',d.gyroMag.toFixed(1)+' °/s');
if(d.calibrated){t('sideTilt',d.sideTilt.toFixed(1)+'°');t('forwardTilt',d.forwardTilt.toFixed(1)+'°');t('br',d.bodyRight.toFixed(3)+' g');t('bf',d.bodyForward.toFixed(3)+' g');t('bu',d.bodyUp.toFixed(3)+' g');vs+=(d.sideTilt-vs)*.22;vf+=(d.forwardTilt-vf)*.22;vs=Math.max(-90,Math.min(90,vs));vf=Math.max(-90,Math.min(90,vf));$('frontRider').setAttribute('transform','rotate('+vs+' 210 300)');$('sideRider').setAttribute('transform','rotate('+vf+' 205 300)');t('frontChip',d.sideTilt.toFixed(1)+'°');t('sideChip',d.forwardTilt.toFixed(1)+'°')}else{vs=0;vf=0;$('frontRider').setAttribute('transform','rotate(0 210 300)');$('sideRider').setAttribute('transform','rotate(0 205 300)');t('sideTilt','CALIBRATE');t('forwardTilt','CALIBRATE');t('br','--');t('bf','--');t('bu','--');t('frontChip','--');t('sideChip','--')}
state('ff',d.freeFall,'DETECTED');state('ha',d.highAccel,'DETECTED');state('hr',d.highRotation,'DETECTED');state('mc',d.motionCandidate,'CANDIDATE');let pct=d.trialTarget?100*d.trialSamples/d.trialTarget:0;$('bar').style.width=Math.min(100,pct)+'%';if(d.recording)t('trial','RECORDING — '+d.trialType+' | '+d.trialSamples+' / '+d.trialTarget);else if(d.trialComplete)t('trial','COMPLETE — '+d.trialType+' | '+d.trialSamples+' samples ready.')}catch(e){t('imu','CONNECTION LOST');$('imu').className='small bad'}setTimeout(poll,100)}
async function cal(url,msg){t('calmsg',msg);try{let r=await fetch(url,{method:'POST',cache:'no-store'}),j=await r.json();t('calmsg',j.message)}catch(e){t('calmsg','Calibration request failed.')}}
$('upright').onclick=()=>cal('/api/cal/upright','Hold completely still — capturing upright reference...');$('forward').onclick=()=>cal('/api/cal/forward','Hold the 20–30° forward lean still — determining rider axes...');$('neutral').onclick=()=>cal('/api/cal/neutral','Hold normal riding posture still — setting neutral...');$('clear').onclick=()=>cal('/api/cal/clear','Clearing stored calibration...');
$('start').onclick=async()=>{let type=$('type').value,seconds=$('duration').value;t('trial','3...');await new Promise(r=>setTimeout(r,1000));t('trial','2...');await new Promise(r=>setTimeout(r,1000));t('trial','1...');await new Promise(r=>setTimeout(r,1000));await fetch('/api/trial/start?type='+encodeURIComponent(type)+'&seconds='+seconds,{cache:'no-store'})};$('stop').onclick=()=>fetch('/api/trial/stop',{cache:'no-store'});$('download').onclick=()=>location.href='/api/trial/csv';poll();
</script></body></html>
)rawliteral";

// -------------------------- WEB API --------------------------
void handleRoot() {
  server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate");
  server.send_P(200,"text/html",MAIN_PAGE);
}

void handleData() {
  if (bodyCalibrationValid) updateBodyCoordinates();

  float cgx=gyroX-gyroBiasX;
  float cgy=gyroY-gyroBiasY;
  float cgz=gyroZ-gyroBiasZ;
  float ds=visualDeadband(sideTilt-sideNeutralOffset);
  float df=visualDeadband(forwardTilt-forwardNeutralOffset);
  String ip=WiFi.localIP().toString();

  char json[1800];
  snprintf(json,sizeof(json),
    "{\"imuOnline\":%s,\"calibrated\":%s,\"sensor\":\"%s\",\"ip\":\"%s\","
    "\"sampleRate\":%lu,"
    "\"ax\":%.5f,\"ay\":%.5f,\"az\":%.5f,"
    "\"gx\":%.4f,\"gy\":%.4f,\"gz\":%.4f,"
    "\"bodyRight\":%.5f,\"bodyForward\":%.5f,\"bodyUp\":%.5f,"
    "\"sideTilt\":%.4f,\"forwardTilt\":%.4f,"
    "\"accelMag\":%.5f,\"gyroMag\":%.4f,"
    "\"gravityTrust\":%.4f,\"gravityBaseline\":%.5f,\"calibrationLean\":%.3f,"
    "\"freeFall\":%s,\"highAccel\":%s,\"highRotation\":%s,\"motionCandidate\":%s,"
    "\"recording\":%s,\"trialComplete\":%s,\"trialType\":\"%s\",\"trialSamples\":%u,\"trialTarget\":%u}",
    imuOnline?"true":"false",
    bodyCalibrationValid?"true":"false",
    sensorName,ip.c_str(),
    (unsigned long)samplesPerSecond,
    accelX,accelY,accelZ,
    cgx,cgy,cgz,
    bodyRightAccel,bodyForwardAccel,bodyUpAccel,
    ds,df,
    accelerationMagnitude,angularSpeedMagnitude,
    gravityTrust,gravityBaselineG,forwardCalibrationAngle,
    freeFallCandidate?"true":"false",
    highAccelCandidate?"true":"false",
    highRotationCandidate?"true":"false",
    motionCandidate?"true":"false",
    recordingTrial?"true":"false",
    trialComplete?"true":"false",
    trialType,trialSampleCount,trialTargetSamples
  );

  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",json);
}

void sendCalResult(int code, const String& msg) {
  String safe=msg;
  safe.replace("\\","\\\\");
  safe.replace("\"","\\\"");
  server.send(code,"application/json","{\"message\":\""+safe+"\"}");
}

void handleCalibrationUpright() {
  if (!imuOnline) { sendCalResult(409,"IMU is offline."); return; }
  CaptureStats s;
  if (!performUprightCalibration(s)) {
    sendCalResult(409,"Upright capture failed. Keep the vest completely still and try again.");
    return;
  }
  saveBodyCalibration();
  sendCalResult(200,"Upright captured. Gravity baseline = "+String(gravityBaselineG,3)+" g. Now lean FORWARD about 20–30 degrees, hold still, then press Capture Forward.");
}

void handleCalibrationForward() {
  if (!uprightCaptured) { sendCalResult(409,"Capture Upright first."); return; }
  CaptureStats s;
  float angle=0;
  if (!performForwardCalibration(s,angle)) {
    sendCalResult(409,"Forward capture rejected. Lean forward 20–30 degrees and HOLD still. Avoid side lean or twisting.");
    return;
  }
  sendCalResult(200,"Body axes calibrated. Detected forward lean = "+String(angle,1)+" degrees. Return to normal posture and press Set Neutral.");
}

void handleCalibrationNeutral() {
  if (!bodyCalibrationValid) { sendCalResult(409,"Complete Upright and Forward calibration first."); return; }
  CaptureStats s;
  if (!captureNeutralPosture(s)) {
    sendCalResult(409,"Neutral capture failed. Keep the vest still and try again.");
    return;
  }
  sendCalResult(200,"Neutral posture saved. Rider orientation is ready for angle-validation testing.");
}

void handleCalibrationClear() {
  clearBodyCalibration();
  sendCalResult(200,"Stored mount calibration cleared. Restart from Capture Upright.");
}

void handleTrialStart() {
  if (!imuOnline) { server.send(409,"text/plain","IMU offline"); return; }
  if (!bodyCalibrationValid) { server.send(409,"text/plain","Body calibration required"); return; }
  if (recordingTrial) { server.send(409,"text/plain","Trial already recording"); return; }

  String req=server.arg("type");
  if (!req.length()) req="Other";
  req.toCharArray(trialType,sizeof(trialType));

  int seconds=constrain(server.arg("seconds").toInt(),5,(int)MAX_TRIAL_SECONDS);
  trialTargetSamples=(uint16_t)min((int)MAX_TRIAL_SAMPLES,seconds*200);
  trialSampleCount=0;
  trialComplete=false;
  trialRightBasis=rightBasis;
  trialForwardBasis=forwardBasis;
  trialUpBasis=upBasis;
  trialSideNeutral=sideNeutralOffset;
  trialForwardNeutral=forwardNeutralOffset;
  trialStartUs=micros();
  recordingTrial=true;
  server.send(200,"text/plain","STARTED");
}

void handleTrialStop() {
  recordingTrial=false;
  trialComplete=(trialSampleCount>0);
  server.send(200,"text/plain","STOPPED");
}

void handleTrialCSV() {
  if (recordingTrial) { server.send(409,"text/plain","Stop the trial before downloading."); return; }
  if (!trialSampleCount) { server.send(404,"text/plain","No trial data."); return; }

  String filename="SmartVest_"+String(trialType)+".csv";
  server.sendHeader("Content-Disposition","attachment; filename=\""+filename+"\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"text/csv","");

  char line[520];
  snprintf(line,sizeof(line),
    "# SmartVest MPU6500 IMU Trial\n"
    "# WHO_AM_I,0x%02X\n"
    "# I2C_Address,0x%02X\n"
    "# Accel_Range,+/-16g\n"
    "# Gyro_Range,+/-2000dps\n"
    "# Sample_Rate_Target,200Hz\n"
    "# Trial,%s\n"
    "# Gravity_Baseline_G,%.5f\n"
    "# Forward_Calibration_Angle,%.3f\n"
    "# Right_Basis,%.6f,%.6f,%.6f\n"
    "# Forward_Basis,%.6f,%.6f,%.6f\n"
    "# Up_Basis,%.6f,%.6f,%.6f\n",
    whoAmI,mpuAddress,trialType,gravityBaselineG,forwardCalibrationAngle,
    trialRightBasis.x,trialRightBasis.y,trialRightBasis.z,
    trialForwardBasis.x,trialForwardBasis.y,trialForwardBasis.z,
    trialUpBasis.x,trialUpBasis.y,trialUpBasis.z
  );
  server.sendContent(line);

  server.sendContent("time_ms,raw_ax,raw_ay,raw_az,raw_gx,raw_gy,raw_gz,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,body_right_g,body_forward_g,body_up_g,accel_mag_g,gyro_mag_dps,fused_side_deg,fused_forward_deg\n");

  for (uint16_t i=0;i<trialSampleCount;i++) {
    TrialSample &s=trialBuffer[i];
    float ax=s.ax/ACCEL_SCALE, ay=s.ay/ACCEL_SCALE, az=s.az/ACCEL_SCALE;
    float gx=s.gx/GYRO_SCALE-gyroBiasX;
    float gy=s.gy/GYRO_SCALE-gyroBiasY;
    float gz=s.gz/GYRO_SCALE-gyroBiasZ;
    Vec3 a=makeVec(ax,ay,az);
    float br=vecDot(a,trialRightBasis);
    float bf=vecDot(a,trialForwardBasis);
    float bu=vecDot(a,trialUpBasis);
    float am=sqrtf(ax*ax+ay*ay+az*az);
    float gm=sqrtf(gx*gx+gy*gy+gz*gz);

    snprintf(line,sizeof(line),
      "%.3f,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.5f,%.5f,%.5f,%.6f,%.6f,%.6f,%.6f,%.5f,%.2f,%.2f\n",
      s.timeUs/1000.0f,
      s.ax,s.ay,s.az,s.gx,s.gy,s.gz,
      ax,ay,az,gx,gy,gz,br,bf,bu,am,gm,
      s.sideCdeg/100.0f,s.forwardCdeg/100.0f
    );
    server.sendContent(line);
    if ((i%50)==0) delay(1);
  }
  server.sendContent("");
}

// ---------------------------- WIFI ---------------------------
void startWiFi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);

  unsigned long start=millis();
  Serial.print("Connecting WiFi");
  while (WiFi.status()!=WL_CONNECTED && millis()-start<20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status()==WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }
}

void startServer() {
  server.on("/",HTTP_GET,handleRoot);
  server.on("/api/data",HTTP_GET,handleData);
  server.on("/api/cal/upright",HTTP_POST,handleCalibrationUpright);
  server.on("/api/cal/forward",HTTP_POST,handleCalibrationForward);
  server.on("/api/cal/neutral",HTTP_POST,handleCalibrationNeutral);
  server.on("/api/cal/clear",HTTP_POST,handleCalibrationClear);
  server.on("/api/trial/start",HTTP_GET,handleTrialStart);
  server.on("/api/trial/stop",HTTP_GET,handleTrialStop);
  server.on("/api/trial/csv",HTTP_GET,handleTrialCSV);
  server.onNotFound([](){server.send(404,"text/plain","Not found");});
  server.begin();
}

// --------------------------- SETUP ---------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("SMARTVEST MPU6500 FINALIZATION LAB - DIRECTION FIXED");
  Serial.println("========================================");

  Wire.begin(SDA_PIN,SCL_PIN);
  Wire.setClock(100000);
  delay(700);

  loadBodyCalibration();
  startWiFi();
  startServer();

  if (WiFi.status()==WL_CONNECTED && MDNS.begin(HOSTNAME)) {
    MDNS.addService("http","tcp",80);
  }

  startIMU();
  sampleRateTimer=millis();
  lastIMURetryMs=millis();

  Serial.print("Dashboard: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Or: http://smartvest-imu.local");
}

// ---------------------------- LOOP ---------------------------
void loop() {
  updateIMU();
  maintainIMU();
  updateSampleRate();
  server.handleClient();

  if (millis()-lastWiFiCheckMs>=5000) {
    lastWiFiCheckMs=millis();
    if (WiFi.status()!=WL_CONNECTED) WiFi.reconnect();
  }

  delay(1);
}
