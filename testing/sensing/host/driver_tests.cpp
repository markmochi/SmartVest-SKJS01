#include "bmi323_fake.h"
#include "src/sensing/BMI323SPI.h"
BMI323SPI bmi323;
#include <cassert>
#ifdef NDEBUG
#error "BMI323 behavioral tests require assertions enabled (compile with -UNDEBUG)"
#endif

bool near(float a,float b,float tolerance=.00001f){return fabsf(a-b)<tolerance;}
void start(FakeBMI323& sensor){
  bmi323=BMI323SPI{};bmi323.startBus();assert(bmi323.begin());
  assert(SPI.sck==18&&SPI.miso==19&&SPI.mosi==23&&SPI.cs==21);
  assert(sensor.resets==0&&sensor.regs[0x20]==0x70B9&&sensor.regs[0x21]==0x70C9);
}
int main(){
  {
    FakeBMI323 hardware;bmi323=BMI323SPI{};
    testPinModes[21]=INPUT;testPinLevels[21]=LOW;
    const unsigned starts=SPI.beginCount,ends=SPI.endCount;
    assert(bmi323.begin());assert(SPI.csHighAtBegin);
    assert(SPI.beginCount==starts+1&&SPI.endCount==ends);
    hardware.allHigh=true;assert(!bmi323.begin());assert(bmi323.chipId==0xFFFF);
    assert(bmi323.accConfig==0&&bmi323.gyrConfig==0&&!bmi323.identified);
    hardware.allHigh=false;assert(bmi323.begin());
    assert(SPI.beginCount==starts+1&&SPI.endCount==ends);
    SPI.beginOK=false;const auto transactions=hardware.transactions.size();
    // Existing bus remains usable; SPI.beginOK only applies on first startup.
    assert(bmi323.begin());assert(hardware.transactions.size()>transactions);
    BMI323SPI newDriver;assert(!newDriver.begin());assert(!newDriver.configured);SPI.beginOK=true;
    hardware.regs[0]=0xAB43;assert(bmi323.begin());
    delay(5001);BMI323Frame frame;hardware.ready(128);
    assert(bmi323.readFrame(frame)==BMIReadResult::Fresh);
    hardware.checkProtocol();
  }
  // One isolated ID/configuration bit error is counted, retried and recovered.
  {
    FakeBMI323 hardware;start(hardware);BMI323Frame frame;
    hardware.ready(128);assert(bmi323.readFrame(frame)==BMIReadResult::Fresh);
    hardware.transientIdReads=1;hardware.transientAccConfigReads=1;
    delay(5001);hardware.ready(256);
    assert(bmi323.readFrame(frame)==BMIReadResult::Rejected);
    hardware.ready(384);assert(bmi323.readFrame(frame)==BMIReadResult::Fresh);
    assert(bmi323.configured&&bmi323.identified);
    assert(bmi323.idFailures==1&&bmi323.configFailures==1);
    hardware.checkProtocol();
  }
  // When an isolated chip-ID read glitches, retry without dropping the SPI link.
  {
    FakeBMI323 hardware;start(hardware);BMI323Frame frame;
    hardware.ready(128);assert(bmi323.readFrame(frame)==BMIReadResult::Fresh);
    // A real ID failure across all retries must remain fatal.
    hardware.regs[0]=0x1141;delay(5001);hardware.ready(256);
    assert(bmi323.readFrame(frame)==BMIReadResult::Rejected);
    assert(bmi323.readFrame(frame)==BMIReadResult::Rejected);
    assert(bmi323.readFrame(frame)==BMIReadResult::Fault);
    assert(bmi323.idFailures>0&&!bmi323.configured);
    hardware.checkProtocol();
  }
  // Forward timestamp corruption must not be treated as a valid movement interval.
  {
    FakeBMI323 hardware;start(hardware);BMI323Frame frame;
    hardware.ready(128);assert(bmi323.readFrame(frame)==BMIReadResult::Fresh);
    hardware.ready(0x00010080);
    assert(bmi323.readFrame(frame)==BMIReadResult::Rejected);
    assert(bmi323.configured);
    assert(bmi323.invalidFrames>0);
    hardware.checkProtocol();
  }
  {
    FakeBMI323 hardware;start(hardware);BMI323Frame f;
    hardware.accel={-2048,4096,2048};hardware.gyro={16384,-8192,4096};hardware.temp=-512;
    hardware.ready(0x12345678,0x00C0);assert(bmi323.readFrame(f)==BMIReadResult::Fresh);
    assert(f.accel[0]==-2048&&f.accel[1]==4096&&f.gyro[1]==-8192&&near(f.temperatureC,22));
    assert(f.sensorTime==0x12345678&&(f.flags&BMI_TEMP_VALID)&&!(f.flags&BMI_TEMP_FRESH));
    hardware.ready(0x12345678);assert(bmi323.readFrame(f)==BMIReadResult::Duplicate);
    hardware.ready(0x123456F8,0x0080);assert(bmi323.readFrame(f)==BMIReadResult::NotReady);
    hardware.forcedStatus=0x0040;assert(bmi323.readFrame(f)==BMIReadResult::Fresh);assert(f.deltaUs==5000);
    hardware.temp=int16_t(0x8000);hardware.ready(0x12345778,0x00E0);
    assert(bmi323.readFrame(f)==BMIReadResult::Fresh&&isnan(f.temperatureC)&&!(f.flags&BMI_TEMP_VALID)&&!(f.flags&BMI_TEMP_FRESH));
    hardware.checkProtocol();
  }
  {
    FakeBMI323 hardware;start(hardware);BMI323Frame f;
    hardware.ready(0x3FFFFF80);assert(bmi323.readFrame(f)==BMIReadResult::Fresh);
    hardware.ready(0);assert(bmi323.readFrame(f)==BMIReadResult::Fresh&&f.deltaUs==5000);
    hardware.ready(384);assert(bmi323.readFrame(f)==BMIReadResult::Fresh&&f.deltaUs==15000&&bmi323.missedPeriods==2);
    hardware.accel[0]=int16_t(0x8000);hardware.saturation=1;hardware.ready(512);
    assert(bmi323.readFrame(f)==BMIReadResult::Fresh&&f.accel[0]==-32768&&f.saturation==1);
    hardware.accel[0]=32767;hardware.ready(640);assert(bmi323.readFrame(f)==BMIReadResult::Fresh&&f.accel[0]==32767&&f.saturation==1);
    hardware.accel[0]=int16_t(0x8000);hardware.saturation=0;hardware.ready(768);assert(bmi323.readFrame(f)==BMIReadResult::Rejected);
    hardware.ready(896);assert(bmi323.readFrame(f)==BMIReadResult::Rejected);
    hardware.ready(1024);assert(bmi323.readFrame(f)==BMIReadResult::Fault);
    assert(bmi323.invalidFrames==3&&!bmi323.configured);hardware.checkProtocol();
  }
  {
    FakeBMI323 hardware;start(hardware);BMI323Frame f;hardware.ready(1024);
    assert(bmi323.readFrame(f)==BMIReadResult::Fresh);hardware.ready(896);
    assert(bmi323.readFrame(f)==BMIReadResult::Rejected&&bmi323.resetFaults==1);
  }
  for(int condition=0;condition<6;condition++){
    FakeBMI323 hardware;bmi323=BMI323SPI{};bmi323.startBus();
    if(condition==0)hardware.regs[0]=0x0071;
    if(condition==1)hardware.allHigh=true;
    if(condition==2)hardware.ignoreAccWrite=true;
    if(condition<3){assert(!bmi323.begin()&&!bmi323.configured);continue;}
    assert(bmi323.begin());BMI323Frame f;
    if(condition==3)hardware.forcedError=0x20;
    if(condition==4)hardware.forcedStatus=1;
    if(condition==5){hardware.regs[0x21]=0x70C8;delay(5001);}
    if(condition==3){assert(bmi323.readFrame(f)==BMIReadResult::Rejected);assert(bmi323.readFrame(f)==BMIReadResult::Rejected);}
    assert(bmi323.readFrame(f)==BMIReadResult::Fault&&!bmi323.configured);
  }
 puts("PASS migrated driver against reference byte-level SPI cases: bus reuse, scaling, partial readiness, temp-invalid, duplicate, sensor rollover, gaps, clipping, timestamp/ID/config/reset faults");
}
