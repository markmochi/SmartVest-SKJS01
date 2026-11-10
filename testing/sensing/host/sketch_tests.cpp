#include <cassert>
#include <iostream>
#include "bmi323_fake.h"
#include "esp_adc/adc_oneshot.h"
#include "SmartVest_SJKS01_Sensing.ino"
void step(unsigned n){for(unsigned i=0;i<n;i++){if(i%2==0)acquisitionStep();loop();}}
int main(){FakeBMI323 fake;fake.automatic=true;fake.gyro={0,0,0};setup();initializeAcquisition();
#if !SV_UART_MAPPING_CONFIRMED && !SV_IMU_MAPPING_CONFIRMED
 step(5000);assert(ports[2].begins==0&&SPI.beginCount==0&&FakeNvs::writes==0&&FakeAdc::reads==0);command("SYSTEM_TEST");assert(eventLink.pending()==0);
#else
 step(500);assert(imu.healthy());const auto tx=eventLink.txFrames;command("CAL_GYRO");step(2400);assert(calibrationStore.valid);assert(eventLink.txFrames>tx);
 command("SYSTEM_TEST");assert(!eventLink.pending());assert(!(telemetry().flags&CrashProtocol::Armed));
 FakeAdc::raw[6]=123;FakeAdc::raw[7]=456;step(50);assert(piezo.healthy());assert(telemetry().flags&CrashProtocol::PiezoReady);
 FakeAdc::readError[7]=ESP_ERR_TIMEOUT;step(10);assert(!(telemetry().flags&CrashProtocol::PiezoReady));FakeAdc::readError[7]=ESP_OK;step(55);
 // Retry a deferred latest battery packet promptly, rather than phase-locking
 // its one-second attempt to a busy shared UART.
 ImuLabProtocol::Frame hello;hello.type=ImuLabProtocol::Type::Hello;hello.boot=eventLink.bootId();hello.session=77;
 uint8_t wire[ImuLabProtocol::MAX_FRAME];const auto size=ImuLabProtocol::encode(hello,wire);
 for(size_t i=0;i<size;++i)ports[2].rx.push_back(wire[i]);
 BatteryProtocol::Reading pack;pack.sequence=1;pack.uptime=millis();pack.age=0;pack.flags=3;pack.profile=1;pack.percent=50;pack.busMV=11200;
 battery.samples.accept(pack,millis());const auto forwarded=batteryForwarded;const auto lastGoodSend=batteryForwardAt;
 ports[2].capacity=0;loop();assert(eventLink.labActive());assert(batteryForwarded==forwarded&&batteryForwardAt==lastGoodSend);
 ports[2].capacity=512;const auto deferredAt=millis();step(25);
 assert(batteryForwarded==forwarded+1&&uint32_t(millis()-deferredAt)<1000);
 ports[2].capacity=0;Serial.capacity=0;auto n=imu.samples;step(2000);assert(imu.samples>n+350);assert(!eventLink.pending());ports[2].capacity=512;
 fake.allHigh=true;step(120);assert(!(telemetry().flags&CrashProtocol::ImuReady));assert(telemetry().flags&CrashProtocol::PiezoReady);
 // Withheld worker updates expire independently of loop/UART progress.
 status.at=millis()-101;assert(!(telemetry().flags&CrashProtocol::ImuReady));
 // Historical TEST entries are retained without replay in this build.
 CrashProtocol::Frame old;old.boot=1;old.sequence=1;old.type=CrashProtocol::Type::Test;
 std::vector<uint8_t> blob(142);blob[0]='O';blob[1]='B';blob[2]=1;blob[3]=1;CrashProtocol::put32(blob.data()+4,1);CrashProtocol::put32(blob.data()+8,2);CrashProtocol::encode(old,blob.data()+12);CrashProtocol::put16(blob.data()+140,CrashProtocol::crc16(blob.data(),140));FakeNvs::blobs["sv_sensor/outbox"]=blob;EventLink legacy;assert(legacy.begin());ports[2].tx.clear();legacy.tick(telemetry());assert(legacy.pending()==1&&legacy.replayBlocked()&&ports[2].tx.size()==32);
#endif
 std::cout<<"PASS actual sketch cooperative adapter: mapping gates, acquisition/service ownership, calibration/UART progress, disabled incidents, piezo health, backpressure, stale-worker expiry, historical TEST quarantine\n";
}
