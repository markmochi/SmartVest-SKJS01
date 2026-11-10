from pathlib import Path
import os,shutil,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[2]
headers=Path(os.environ['SJKS_ARDUINOJSON'])
for enabled in [0,1]:
 with tempfile.TemporaryDirectory() as tmp:
  folder=Path(tmp);main=folder/'main';shutil.copytree(ROOT/'Main_and_Cameras/SmartVest_SJKS01_Main',main)
  (main/'src/emergency/EmergencyCoordinator.h').write_text('''#pragma once
#include "../link/CrashProtocol.h"
class EmergencyCoordinator {public:bool ready=true;bool readyForCrash()const{return ready;}void observeImpact(uint32_t,uint32_t,uint8_t){}unsigned accepted=0;bool accept(const CrashProtocol::Frame&,bool,uint32_t){++accepted;return true;}};
''',encoding='utf8')
  source=folder/'arming.cpp';source.write_text('''#include "SensorLink.h"
#include "Config.h"
#include <cassert>
#include <iostream>
int main(){
 clockMs=1000;
 EmergencyCoordinator coordinator;SensorLink link(coordinator);link.begin();
 for(auto type:{CrashProtocol::Type::Crash,CrashProtocol::Type::AutomaticCrash}){
  CrashProtocol::Frame frame;frame.type=type;frame.boot=10;frame.sequence=uint32_t(type);frame.flags=7;
  uint8_t bytes[CrashProtocol::FRAME_SIZE];CrashProtocol::encode(frame,bytes);
  for(uint8_t byte:bytes)ports[0].rx.push_back(byte);
  link.tick();
 }
 assert(coordinator.accepted==(Config::ARM_CRASH_DETECTION?2u:0u));
 assert(link.telemetry().acknowledgements==(Config::ARM_CRASH_DETECTION?2u:0u));
 CrashProtocol::Frame hb;hb.type=CrashProtocol::Type::Heartbeat;hb.boot=10;hb.flags=15;
 uint8_t wire[CrashProtocol::FRAME_SIZE];CrashProtocol::encode(hb,wire);for(auto byte:wire)ports[0].rx.push_back(byte);link.tick();
 assert(link.armedAndHealthy()==Config::ARM_CRASH_DETECTION);coordinator.ready=false;assert(!link.armedAndHealthy());
 ports[0].tx.clear();clockMs+=2000;link.tick();
 ImuLabProtocol::Parser parser;ImuLabProtocol::Frame hello;bool found=false;
 for(auto byte:ports[0].tx)if(parser.feed(byte,clockMs,hb,hello)==2&&hello.type==ImuLabProtocol::Type::Hello){assert(hello.token==(Config::ARM_CRASH_DETECTION?ImuLabProtocol::AUTOMATIC_CRASH_HELLO_TOKEN:0));found=true;}
 assert(found);coordinator.ready=true;assert(link.armedAndHealthy()==Config::ARM_CRASH_DETECTION);
 std::cout<<"PASS Main arming control="<<Config::ARM_CRASH_DETECTION<<" gates both automatic crash types and their ACKs\\n";
}
''',encoding='utf8')
  binary=folder/'arming'
  subprocess.run(['g++','-std=c++17','-UNDEBUG',f'-DSV_ARM_CRASH_DETECTION={enabled}',
    '-DARDUINO_ARCH_ESP32=1','-DCONFIG_IDF_TARGET_ESP32=1',
    '-I'+str(ROOT/'testing/sensing/host'),'-I'+str(headers),'-I'+str(main),'-I'+str(main/'src/link'),
    str(main/'src/link/SensorLink.cpp'),str(main/'src/link/ImuLabReceiver.cpp'),str(main/'src/link/PiezoReceiver.cpp'),str(source),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
