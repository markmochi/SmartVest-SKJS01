"""Actual incident coordinator with host-only GPS/NVS/SMS-job adapters.

No radio messages or camera commands are sent to physical devices.
"""
import os,shutil,subprocess,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
headers=Path(os.environ['SJKS_ARDUINOJSON'])
with tempfile.TemporaryDirectory() as t:
 p=Path(t);s=p/'sketch';h=p/'host'
 shutil.copytree(r/'SmartVest_SJKS01_Main',s);shutil.copytree(r/'../testing/main/host',h)
 prefs=(h/'Preferences.h').read_text();prefs=prefs[:prefs.index('class Preferences')]+'''
 inline std::vector<uint8_t> blob;inline bool failWrite=false;
 class Preferences {public:
 bool begin(const char*,bool){return true;}
 size_t getBytesLength(const char*){return blob.size();}
 size_t getBytes(const char*,void*p,size_t n){if(n!=blob.size())return 0;std::memcpy(p,blob.data(),n);return n;}
 size_t putBytes(const char*,const void*p,size_t n){if(failWrite)return 0;blob.assign((const uint8_t*)p,(const uint8_t*)p+n);return n;}
 };
'''
 (h/'Preferences.h').write_text(prefs)
 (h/'esp_system.h').write_text('#pragma once\n')
 (s/'src/gps/NEO6MService.h').write_text('''#pragma once
#include "../core/Models.h"
class NEO6MService {public:bool fix=true,fresh=true;uint32_t age=0;
 bool latestLocation(double& a,double& b,uint32_t& ms,bool& f){a=14.5;b=121.0;ms=age;f=fresh;return fix;}
};
''')
 (s/'src/storage/StateStore.h').write_text('''#pragma once
#include <Preferences.h>
#include "../core/Models.h"
class StateStore {public:DeviceState data;bool healthy=true,writable=true;bool commit(const DeviceState& next){data=next;return true;}
 static bool enqueueSms(DeviceState& d,const char* phone,const char* text,const char* kind){
   for(auto& j:d.jobs)if(!j.id){j.id=d.nextJob++;j.phone=phone;j.text=text;j.kind=kind;j.state="queued";return true;}return false;
 }
};
''')
 program=p/'automatic_alerts.cpp';program.write_text(r'''
#include "EmergencyCoordinator.h"
#include <cassert>
#include <iostream>
uint32_t clockMs=1000;
JsonDocument status(EmergencyCoordinator& e){JsonDocument j;e.writeJson(j.to<JsonObject>());return j;}
int main(){
 StateStore store;NEO6MService gps;store.data.rider="BENCH RIDER";store.data.contacts[0].phone="+639123456789";
 EmergencyCoordinator e(store,gps);e.begin();
 CrashProtocol::Frame f;f.type=CrashProtocol::Type::AutomaticCrash;f.flags=7;f.boot=123;f.sequence=1;f.uptimeMs=900;
 assert(e.accept(f,true,100));auto j=status(e);
 assert(e.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Blink);
 assert(j["automaticCrashDetectionEnabled"].as<bool>());
 assert(j["incidents"][0]["test"].as<bool>());
 assert(j["incidents"][0]["message"].as<std::string>().find(Config::SMS_ALERT_HEADER)==0);
 assert(j["incidents"][0]["message"].as<std::string>().find(Config::SMS_TEST_HEADER)==std::string::npos);
 assert(j["incidents"][0]["message"].as<std::string>().find(Config::SMS_ALERT_ACTION)!=std::string::npos);
 assert(j["incidents"][0]["message"].as<std::string>().find("14.500000")!=std::string::npos);
 assert(j["incidents"][0]["latency"]["estimatedDetectionToMainMs"]==100);
 assert(j["incidents"][0]["latency"]["front"]["receivedAckMs"].isNull());
 clockMs=1010;e.tick();assert(store.data.nextJob==2);
 auto& job=store.data.jobs[0];assert(job.eventBoot==123 && job.eventSequence==1 && job.recipientIndex==0);
 clockMs=1020;assert(e.submissionGuard(job));assert(!e.submissionGuard(job));
 clockMs=1040;job.state="accepted";e.tick();
 assert(e.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Steady);
 j=status(e);assert(j["incidents"][0]["recipients"][0]["state"]=="accepted");
 const auto l=j["incidents"][0]["recipients"][0]["latency"];
 assert(l["queuedMs"]==10 && l["submissionIntentMs"]==20 && l["firstResultMs"]==40);
 assert(e.accept(f,true,200));e.tick();assert(store.data.nextJob==2 && status(e)["incidents"].size()==1);
 assert(e.crashLight.tick(6039)==CrashLedSignal::Pattern::Steady);
 assert(e.crashLight.tick(6040)==CrashLedSignal::Pattern::Off);
 job.state="delivered";e.tick();assert(e.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Off);
 std::cout<<"PASS automatic frame -> standard GPS-aware alert job -> persisted submission guard/outcome; retry has no duplicate job\n";

 clockMs=1050;JsonDocument cmd;e.cameraCommand(0,cmd.to<JsonObject>());
 assert(cmd["command"]["type"]=="LOCK" && cmd["command"]["test"].as<bool>());
 assert(cmd["command"]["preClips"]==Config::CAMERA_PRE_CLIPS);
 assert(cmd["command"]["postClips"]==Config::CAMERA_POST_CLIPS);
 assert(cmd["command"]["boot"]==123 && cmd["command"]["sequence"]==1);
 clockMs=1070;assert(e.cameraAck(0,123,1,"received"));
 clockMs=1080;assert(e.cameraAck(0,123,1,"received"));
 assert(!e.cameraAck(0,123,99,"protected"));
 clockMs=301000;assert(e.cameraAck(0,123,1,"protected"));
 clockMs=302000;assert(e.cameraAck(0,123,1,"protected"));
 assert(!e.cameraAck(0,123,1,"received"));
 j=status(e);assert(j["incidents"][0]["latency"]["front"]["firstOfferedMs"]==50);
 assert(j["incidents"][0]["latency"]["front"]["receivedAckMs"]==70);
 assert(j["incidents"][0]["latency"]["front"]["protectedAckMs"]==300000);
 assert(j["incidents"][0]["latency"]["rear"]["receivedAckMs"].isNull());
 std::cout<<"PASS independent camera LOCK and identity/monotonic ACK stages; first timing retained, protected window completion distinct from receipt\n";

 gps.fix=false;f.sequence=2;assert(e.accept(f,true,5));e.tick();
 assert(e.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Blink);
 job.state="accepted";e.tick();assert(e.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Blink);
 auto& second=store.data.jobs[1];second.state="unknown";e.tick();
 assert(e.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Blink);
 second.state="failed";e.tick();assert(e.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Blink);
 j=status(e);assert(j["incidents"][1]["message"].as<std::string>().find(Config::SMS_NO_GPS_TEXT)!=std::string::npos);
 assert(store.data.nextJob==3);
 assert(e.cameraAck(1,123,2,"failed"));e.tick();assert(store.data.nextJob==3);
 gps.fix=true;gps.fresh=false;gps.age=10000;f.sequence=3;assert(e.accept(f,true,10));
 j=status(e);assert(j["incidents"][2]["message"].as<std::string>().find(Config::SMS_STALE_LOCATION_LABEL)!=std::string::npos);
 std::cout<<"PASS missing/stale GPS and failed camera do not suppress SMS job creation\n";

 // Fourth TEST waits while three tests are active; a real incident slot is reserved.
 f.sequence=4;assert(!e.accept(f,true,15));
 // Release one test slot through terminal SMS and both protected camera ACKs.
 assert(e.cameraAck(1,123,1,"protected"));
 f.sequence=4;failWrite=true;assert(!e.accept(f,true,15));
 const auto next=store.data.nextJob;e.tick();assert(store.data.nextJob==next);
 failWrite=false;clockMs+=5000;e.tick();assert(e.accept(f,true,15));
 EmergencyCoordinator reboot(store,gps);reboot.begin();j=status(reboot);
 assert(reboot.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Off);
 reboot.tick();assert(reboot.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Off);
 assert(j["incidents"].size()==4 && !j["incidents"][0]["latency"]["available"].as<bool>());
 assert(j["incidents"][0]["latency"]["estimatedDetectionToMainMs"].isNull());
 std::cout<<"PASS flash failure withholds acceptance/submission; existing journal format survives reboot; unknown RAM latencies stay unknown\n";

 // Isolated fresh journal: manual diagnostics, no contacts, ordinary crash,
 // multiple recipients and a commit failure never invent SMS success.
 blob.clear();StateStore isolated;NEO6MService noGps;noGps.fix=false;
 EmergencyCoordinator led(isolated,noGps);led.begin();clockMs=500000;
 f.type=CrashProtocol::Type::Test;f.boot=456;f.sequence=1;
 assert(led.accept(f,true,0,true));assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Off);
 f.type=CrashProtocol::Type::Crash;f.sequence=2;
 assert(led.accept(f,true,0));assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Blink);
 clockMs+=5000;led.tick();assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Off);
 isolated.data.contacts[0].phone="+639123456789";isolated.data.contacts[1].phone="+639123456788";
 f.sequence=3;failWrite=true;assert(!led.accept(f,true,0));
 assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Blink);
 failWrite=false;clockMs+=5000;led.tick();led.tick();
 assert(isolated.data.jobs[0].eventSequence==3&&isolated.data.jobs[1].eventSequence==3);
 isolated.data.jobs[0].state="accepted";led.tick();
 assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Steady);
 clockMs+=4000;isolated.data.jobs[1].state="accepted";led.tick();
 assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Steady);
 clockMs+=1000;assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Off);
 assert(led.accept(f,true,0));assert(led.crashLight.tick(clockMs)==CrashLedSignal::Pattern::Off);
 std::cout<<"PASS production coordinator LED events: diagnostic isolation, no recipients, storage fault, late success, first recipient only and duplicate silence\n";
}
''')
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(h),'-I'+str(headers),'-I'+str(s),str(s/'src/emergency/EmergencyCoordinator.cpp'),str(program),'-I'+str(s/'src/emergency'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
