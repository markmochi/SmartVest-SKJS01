#include "Config.h"
#include "src/link/EventLink.h"
#include "src/sensing/ImuLab.h"
#include "src/power/NanoBattery.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <cmath>
#include <cstring>

// Ownership: worker owns SPI/calibration/attitude/detector/capture; loop owns
// piezo/UART/outbox/USB. Queues copy snapshots; no live object is shared.
Imu imu;
GyroCalibration gyroCalibration;
CalibrationStore calibrationStore;
PiezoInputs piezo;
EventLink eventLink;
NanoBattery battery;
uint32_t batteryForwardAt=0,batteryAttemptAt=0,batteryForwarded=0;
ImuLab imuLab(imu,gyroCalibration,calibrationStore,eventLink);
// A queue-owned diagnostic snapshot: the Arduino loop never reads the worker's
// SPI driver directly, avoiding cross-core data races during retries.
struct Status {
  CrashProtocol::Frame frame;
  uint32_t at=0,samples=0,candidates=0,failures=0,notReady=0,duplicates=0,decisionAt=0;
  bool labBusy=false;
  uint32_t driverRejected=0,idFailures=0,configFailures=0,maxCycleUs=0;
  uint16_t chipId=0,accConfig=0,gyrConfig=0,lastError=0;
  uint8_t error=0,phase=0,calibration=0;
  char driverDetail[96]={};
};
QueueHandle_t statusQueue=nullptr,piezoQueue=nullptr,commandQueue=nullptr;
TaskHandle_t acquisitionTask=nullptr;
struct AutomaticEvent {CrashProtocol::Frame frame;uint32_t candidate=0;};
QueueHandle_t automaticQueue=nullptr;
AutomaticEvent workerPending;
bool workerHasPending=false;
uint32_t observedCandidate=0;
std::atomic<uint32_t> automaticQueueBlocked{0},automaticQueueDropped{0},automaticSkipped{0};
uint32_t automaticQueued=0,automaticLastCandidate=0,automaticLastSequence=0;
uint32_t automaticQueueDelayMs=0;
Status status;
uint32_t lastReport=0,commandDrops=0,workerDecisionAt=0;
char line[32]={};size_t lineLength=0;bool lineOverflow=false;
struct Command {ImuLabProtocol::Frame frame;uint8_t local=0;};
void receiveLab(void*,const ImuLabProtocol::Frame& f){if(f.type==ImuLabProtocol::Type::Command&&f.length==40&&f.data[0]>=uint8_t(ImuLabProtocol::Op::PiezoBaseline)){piezo.handleCommand(f,eventLink,status.labBusy);return;}Command c;c.frame=f;if(commandQueue && xQueueSend(commandQueue,&c,0)!=pdTRUE)++commandDrops;}
uint32_t lastPublished=0;
void initializeAcquisition(){
  imuLab.begin();
  if(SensorConfig::IMU_MAPPING_CONFIRMED){imu.begin();if(imu.whoAmI)calibrationStore.load(imu.whoAmI);}
}
void acquisitionStep(){
    Command c;if(xQueueReceive(commandQueue,&c,0)==pdTRUE){
      if(c.local==1)imuLab.startCalibration(ImuLabProtocol::Op::Gyro);
      else if(c.local==2)imuLab.cancelCalibration();
      else imuLab.handle(c.frame);
    }
    ImuLab::PiezoSnapshot ps;if(xQueueReceive(piezoQueue,&ps,0)==pdTRUE)imuLab.piezo=ps;
    if(SensorConfig::IMU_MAPPING_CONFIRMED)imu.tick();
    imuLab.tick();
    // Read the retained worker-owned piezo snapshot.
    const bool canTrigger=SensorConfig::ARM_CRASH_DETECTION &&
      eventLink.automaticPeerCurrent() && imuLab.automaticReady() && imuLab.piezo.valid &&
      uint32_t(millis()-imuLab.piezo.atMs)<SensorConfig::PIEZO_STALE_MS;
    if(imuLab.detector.count!=observedCandidate){
      observedCandidate=imuLab.detector.count;workerDecisionAt=millis();
      if(canTrigger && automaticQueue){
        if(workerHasPending)++automaticQueueDropped;
        else {
          workerPending=AutomaticEvent();workerPending.candidate=observedCandidate;
          auto& f=workerPending.frame;f.type=CrashProtocol::Type::AutomaticCrash;f.uptimeMs=workerDecisionAt;
          f.flags=CrashProtocol::ImuReady|CrashProtocol::PiezoReady|CrashProtocol::Armed;
          f.accelerationMg=uint16_t(fminf(65535,imuLab.detector.last.peakG*1000+.5f));
          f.rotationDps=uint16_t(fminf(65535,imuLab.detector.last.peakDps+.5f));
          f.piezoFront=imuLab.piezo.front;f.piezoRear=imuLab.piezo.rear;
          workerHasPending=true;
        }
      }else ++automaticSkipped;
    }
    if(workerHasPending){
      if(xQueueSend(automaticQueue,&workerPending,0)==pdTRUE)workerHasPending=false;
      else ++automaticQueueBlocked;
    }
    if(uint32_t(millis()-lastPublished)>=20){
      Status s;s.at=millis();s.decisionAt=workerDecisionAt;s.labBusy=imuLab.busy();s.samples=imu.samples;s.candidates=imuLab.detector.count;s.error=uint8_t(imu.error);s.phase=uint8_t(imu.phase);s.calibration=calibrationStore.valid;
      s.failures=imu.failures;s.notReady=imu.notReady;s.duplicates=imu.duplicates;
      s.driverRejected=imu.driver.rejectedFrames;s.idFailures=imu.driver.idFailures;
      s.configFailures=imu.driver.configFailures;s.maxCycleUs=imu.maxCycleUs;
      s.chipId=imu.driver.chipId;s.accConfig=imu.driver.accConfig;
      s.gyrConfig=imu.driver.gyrConfig;s.lastError=imu.driver.lastError;
      snprintf(s.driverDetail,sizeof(s.driverDetail),"%s",imu.driver.detail);
      if(imu.healthy()){
        s.frame.flags|=CrashProtocol::ImuReady;float a=0,g=0;
        for(unsigned i=0;i<3;++i){a+=imu.sample.accel[i]*imu.sample.accel[i];float w=imu.sample.gyro[i]-(calibrationStore.valid?calibrationStore.bias[i]:0);g+=w*w;}
        s.frame.accelerationMg=uint16_t(fminf(65535,sqrtf(a)*1000+.5f));s.frame.rotationDps=uint16_t(fminf(65535,sqrtf(g)+.5f));
      }
      // Retain qualified candidates through outbox backpressure, but never advertise readiness while full or unwritable.
        if(canTrigger && eventLink.automaticEventReady())s.frame.flags|=CrashProtocol::Armed;
      // Worker emits a bounded candidate snapshot; only loop touches durable NVS/UART.
      xQueueOverwrite(statusQueue,&s);lastPublished=millis();
    }
}
void acquire(void*){
  initializeAcquisition();
  TickType_t wake=xTaskGetTickCount();const TickType_t period=pdMS_TO_TICKS(2)?pdMS_TO_TICKS(2):1;
  for(;;){
    acquisitionStep();
    if(xTaskGetTickCount()-wake>=period)wake=xTaskGetTickCount()-period+1;
    vTaskDelayUntil(&wake,period);
  }
}

CrashProtocol::Frame telemetry(){
  CrashProtocol::Frame f;
  if(acquisitionTask && uint32_t(millis()-status.at)<100)f=status.frame;
  if(piezo.healthy()){f.flags|=CrashProtocol::PiezoReady;f.piezoFront=piezo.reportedFront();f.piezoRear=piezo.reportedRear();}
  if(!(f.flags&CrashProtocol::PiezoReady) || !eventLink.automaticEventReady())f.flags&=uint8_t(~CrashProtocol::Armed);
  return f;
}
void pumpAutomatic(){
  if(!automaticQueue || !eventLink.automaticPeerCurrent())return;
  AutomaticEvent e;if(xQueuePeek(automaticQueue,&e,0)!=pdTRUE)return;
  if(eventLink.pending()>=SensorConfig::MAX_PENDING_EVENTS)return;
  if(eventLink.enqueue(e.frame)){
    xQueueReceive(automaticQueue,&e,0);++automaticQueued;
    automaticLastCandidate=e.candidate;automaticLastSequence=eventLink.lastEnqueuedSequence;
    automaticQueueDelayMs=uint32_t(millis()-e.frame.uptimeMs);
  }
}
void command(const char* text){
  if(!strcmp(text,"CAL_GYRO") || !strcmp(text,"CANCEL_CAL")){
    Command c;c.local=!strcmp(text,"CAL_GYRO")?1:2;if(commandQueue && xQueueSend(commandQueue,&c,0)!=pdTRUE)++commandDrops;
  }else if(!strcmp(text,"PIEZO_RESET_PEAKS"))piezo.clearPeaks();
  else if(!strcmp(text,"SYSTEM_TEST") && SensorConfig::ENABLE_SYSTEM_UART_TEST){
    auto f=telemetry();f.type=CrashProtocol::Type::Test;f.uptimeMs=millis();eventLink.enqueue(f);
  }
  lastReport=millis()-1000;
}
void setup(){
  Serial.setTxBufferSize(2048);Serial.begin(SensorConfig::DEBUG_BAUD);
  if(SensorConfig::UART_MAPPING_CONFIRMED)eventLink.begin();
  battery.begin();
  piezo.begin();
  statusQueue=xQueueCreate(1,sizeof(Status));piezoQueue=xQueueCreate(1,sizeof(ImuLab::PiezoSnapshot));commandQueue=xQueueCreate(4,sizeof(Command));
  automaticQueue=xQueueCreate(4,sizeof(AutomaticEvent));
  if(statusQueue && piezoQueue && commandQueue && automaticQueue){
    eventLink.attachLab(nullptr,receiveLab);
    if(xTaskCreatePinnedToCore(acquire,"BMI323 acquisition",8192,nullptr,2,&acquisitionTask,1)!=pdPASS)acquisitionTask=nullptr;
  }
}
void loop(){
  if(statusQueue)xQueueReceive(statusQueue,&status,0);
  pumpAutomatic();
  if(SensorConfig::UART_MAPPING_CONFIRMED){const uint32_t sent=eventLink.heartbeatTransmissions;eventLink.tick(telemetry());if(sent!=eventLink.heartbeatTransmissions)piezo.clearPeaks();}
  battery.tick();
  if(battery.samples.seen&&uint32_t(millis()-batteryForwardAt)>=1000&&uint32_t(millis()-batteryAttemptAt)>=20){
    batteryAttemptAt=millis();
    if(eventLink.sendBattery(battery.samples.snapshot(millis()))){batteryForwardAt=millis();++batteryForwarded;}
  }
  piezo.tick();
  piezo.report(eventLink,status.candidates,status.decisionAt,status.labBusy);
  if(piezoQueue){ImuLab::PiezoSnapshot ps{piezo.atMs,piezo.frontRaw,piezo.rearRaw,piezo.healthy()};xQueueOverwrite(piezoQueue,&ps);}
  for(unsigned budget=0;budget<32 && Serial.available();++budget){
    char c=char(Serial.read());if(c=='\r')continue;
    if(c=='\n'){line[lineLength]=0;if(!lineOverflow)command(line);lineLength=0;lineOverflow=false;}
    else if(lineLength+1<sizeof(line) && !lineOverflow)line[lineLength++]=c;else lineOverflow=true;
  }
  if(uint32_t(millis()-lastReport)>=1000){
    // Three short lines distinguish a healthy BMI323 from a broken UART or
    // missing diagnostic session. No hardware work is done from the loop task.
    char b[250];
    int bn=snprintf(b,sizeof(b),"[BATTERY] nano=%u valid=%u age=%lu frames=%lu bad=%lu uartErrors=%lu forwarded=%lu bytes=%lu error=%u flags=0x%02X raw_mV=%lu pending=%u\n",
      unsigned(battery.samples.seen&&uint32_t(millis()-battery.samples.receivedAt)<BatteryProtocol::STALE_MS),
      unsigned(battery.samples.fresh(millis())),(unsigned long)battery.samples.age(millis()),
      (unsigned long)battery.samples.accepted,(unsigned long)battery.rejected(),(unsigned long)battery.uartErrors.load(),(unsigned long)batteryForwarded,
      (unsigned long)battery.bytes,unsigned(battery.samples.data.error),unsigned(battery.samples.data.flags),
      (unsigned long)battery.samples.data.busMV,unsigned(eventLink.pending()));
    if(bn>0&&bn<int(sizeof(b))&&Serial.availableForWrite()>=bn)Serial.write(reinterpret_cast<const uint8_t*>(b),bn);
    const bool current=acquisitionTask && uint32_t(millis()-status.at)<250;
    int n=snprintf(b,sizeof(b),"[BMI] task=%u snapshotFresh=%u ready=%u phase=%u error=%u samples=%lu failures=%lu notReady=%lu duplicates=%lu\n",
      unsigned(acquisitionTask!=nullptr),unsigned(current),unsigned(bool(telemetry().flags&CrashProtocol::ImuReady)),
      unsigned(status.phase),unsigned(status.error),(unsigned long)status.samples,(unsigned long)status.failures,
      (unsigned long)status.notReady,(unsigned long)status.duplicates);
    if(n>0 && n<int(sizeof(b)) && Serial.availableForWrite()>=n)Serial.write(reinterpret_cast<const uint8_t*>(b),n);
    n=snprintf(b,sizeof(b),"[SPI] id=0x%04X accel=0x%04X gyro=0x%04X err=0x%04X idFails=%lu cfgFails=%lu rejected=%lu maxReadUs=%lu reason=%s\n",
      status.chipId,status.accConfig,status.gyrConfig,status.lastError,
      (unsigned long)status.idFailures,(unsigned long)status.configFailures,(unsigned long)status.driverRejected,
      (unsigned long)status.maxCycleUs,status.driverDetail[0]?status.driverDetail:"Worker has not published a snapshot");
    if(n>0 && n<int(sizeof(b)) && Serial.availableForWrite()>=n)Serial.write(reinterpret_cast<const uint8_t*>(b),n);
    n=snprintf(b,sizeof(b),"[LINK] heartbeats=%lu received=%lu rxRejected=%lu uartFrame=%lu uartOverflow=%lu session=%u labDeferred=%lu labDropped=%lu commandDropped=%lu piezo=%u armed=%u\n",
      (unsigned long)eventLink.heartbeatTransmissions,(unsigned long)eventLink.rxBytes,
      (unsigned long)eventLink.rejectedFrames(),(unsigned long)eventLink.framingErrors.load(),
      (unsigned long)(eventLink.fifoOverflow.load()+eventLink.rxBufferFull.load()),unsigned(eventLink.labActive()),
      (unsigned long)eventLink.labTxDeferred.load(),(unsigned long)eventLink.labTxDropped.load(),
      (unsigned long)commandDrops,unsigned(piezo.healthy()),unsigned(bool(telemetry().flags&CrashProtocol::Armed)));
    if(n>0 && n<int(sizeof(b)) && Serial.availableForWrite()>=n)Serial.write(reinterpret_cast<const uint8_t*>(b),n);
    n=snprintf(b,sizeof(b),"[DETECTION] peer=%u ready=%u queued=%lu candidate=%lu sequence=%lu queueMs=%lu ackSequence=%lu ackAgeMs=%lu blocked=%lu dropped=%lu skipped=%lu\n",
      unsigned(eventLink.automaticPeerCurrent()),unsigned(bool(telemetry().flags&CrashProtocol::Armed)),
      (unsigned long)automaticQueued,(unsigned long)automaticLastCandidate,(unsigned long)automaticLastSequence,
      (unsigned long)automaticQueueDelayMs,(unsigned long)eventLink.lastAckSequence,(unsigned long)eventLink.lastAckDecisionAgeMs,
      (unsigned long)automaticQueueBlocked.load(),(unsigned long)automaticQueueDropped.load(),(unsigned long)automaticSkipped.load());
    if(n>0 && n<int(sizeof(b)) && Serial.availableForWrite()>=n)Serial.write(reinterpret_cast<const uint8_t*>(b),n);
    lastReport=millis();
  }
  delay(1);
}
