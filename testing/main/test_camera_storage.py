"""Run production catalog, metadata and recovery commit functions on host files."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
s=(root/'SmartVest_SJKS01_Camera/src/CameraApp.cpp').read_text(encoding='utf8')
clip=s[s.index('struct Clip {'):s.index('QueueHandle_t commands=')]
event=s[s.index('bool eventTag('):s.index('void reply(')]
verify=s[s.index('bool verifyEvent('):s.index('void finishEvents(')]
catalog=s[s.index('bool serviceCatalog('):s.index('bool serviceEvidence(')]
add=s[s.index('bool addClip('):s.index('void failStorage(')]
json=s[s.index('bool loadJson('):s.index('// FAT stores local wall time.')]
retention=s[s.index('bool trim();'):s.index('bool checkpoint()')]
recovery=s[s.index('bool scanRecordingSessions()'):s.index('void serviceStorage()')]
pre=r'''
#include "InterruptedAvi.h"
#include "AviVerifier.h"
#include "ClipStorage.h"
#include "EvidencePolicy.h"
#include <ArduinoJson.h>
#include "../host/Preferences.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <fstream>
#include <iterator>
uint32_t clockMs=1000;
constexpr const char* ROLE="front";constexpr const char* TOKEN="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
namespace CameraPairing{constexpr const char* FRONT_KEY=TOKEN;constexpr const char* REAR_KEY=TOKEN;}
constexpr uint32_t MAX_FRAME_GAP_MS=2000,LINK_MARGIN_MS=2000;constexpr int NAME_UTC_OFFSET_MINUTES=480;constexpr size_t NORMAL_CLIPS=30;
constexpr uint64_t FREE_RESERVE_BYTES=64ULL*1024*1024,MAX_AVI_BYTES=128ULL*1024*1024;
template<class... T> void emitLog(T...){ }void publish(){}
struct NoMemory:ArduinoJson::Allocator {
 void* allocate(size_t)override{return nullptr;}
 void deallocate(void*)override{}
 void* reallocate(void*,size_t)override{return nullptr;}
};
'''
globals=r'''
std::vector<Clip> history,recent;uint64_t cardSessionMax=0,freeAtBoundary=0,latestClipUtc=0;
unsigned normalCount=0,deletedCount=0,lockedCount=0;
String storageWarning,lastClipName;Clip current;std::vector<Event> events;bool catalogFailure=false;
void failStorage(const char*){catalogFailure=true;}
InterruptedAvi recovery;File recoveryDirectory;uint32_t recoveryScanAt=0;
AviVerifier verifier;bool recoveryVerifying=false,verifying=false;Clip recoveryClip;
'''
clock_code=r'''
AviWriter writer;File checkpointFile;uint32_t checkpointAt=0;uint64_t cardTotalBytes=0;
uint64_t utcAnchor=0,utcAt=0;char utcSource[24]="device";int snapshotMux=0;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
'''+s[s.index('void inventory()'):s.index('std::vector<Clip> history')]+s[s.index('void refreshClipTime('):s.index('bool eventTag(')].replace('clockMs()', 'millis()')+s[s.index('bool checkpoint()'):s.index('bool openClip()')].replace('clockMs()', 'millis()')
checks=r'''
std::vector<uint8_t> read(const String& p){std::ifstream f(SD_MMC.path(p),std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const String& p,const std::vector<uint8_t>& bytes){std::ofstream f(SD_MMC.path(p),std::ios::binary);f.write((const char*)bytes.data(),bytes.size());assert(f.good());}
const String identity="/clips/front_commit",base="/system/recording/front_commit";
std::vector<uint8_t> make(){
 AviWriter writer;assert(writer.begin(base,640,480));const uint8_t frame[]={0xff,0xd8,0xff,0xd9};for(int i=0;i<12;++i)assert(writer.frame(frame,4));
 RecordingCheckpoint::State state;state.session=42;state.start=100;state.first=100;state.last=1475;state.at=1600;state.frames=12;state.bytes=writer.size();assert(writer.checkpoint(state.payload));
 uint8_t bytes[RecordingCheckpoint::SIZE];assert(RecordingCheckpoint::encode(state,TOKEN,ROLE,identity.c_str(),bytes));put(base+".rec",{bytes,bytes+sizeof(bytes)});writer.abort();return read(base+".part");
}
void drive(){for(unsigned i=0;i<2000;++i){if(recovery.busy())recovery.step();else serviceRecovery();if(!recovery.busy()&&!recovery.ready()&&!recoveryVerifying)return;}assert(false);}
void prepare(const std::string& path){recovery.cancel();verifier.cancel();recoveryVerifying=false;recoveryDirectory.close();history.clear();recent.clear();SD_MMC.root=path;SD_MMC.renameCalls=SD_MMC.failRenameAt=0;for(const char* p:{"/clips","/system/clips","/system/recording","/system/recovery"})std::filesystem::create_directories(path+p);}
int main(int argc,char**argv){
 assert(argc==2);const std::string directory=argv[1];
 // A clock received during a clip must reach its next durable checkpoint.
 prepare(directory+"/late_clock");current=Clip{};current.role=ROLE;current.session=42;current.start=100;
 current.stem="/clips/front_time-unknown_session-42_clip-1";
 assert(writer.begin(ClipStorage::internal(current.stem,"recording"),640,480));
 const uint8_t clockFrame[]={0xff,0xd8,0xff,0xd9};assert(writer.frame(clockFrame,4));current.frames=writer.count();current.first=current.last=200;
 checkpointFile=SD_MMC.open(ClipStorage::internal(current.stem,"recording")+".rec",FILE_WRITE);
 utcAnchor=1700000000000ULL;utcAt=1000;clockMs=2000;assert(checkpoint());
 assert(current.utcStart==utcAnchor-900);checkpointFile.close();writer.abort();
 File savedClock=SD_MMC.open(ClipStorage::internal(current.stem,"recording")+".rec",FILE_READ);
 uint8_t clockBytes[RecordingCheckpoint::SIZE];assert(savedClock.read(clockBytes,sizeof(clockBytes))==sizeof(clockBytes));
 RecordingCheckpoint::State clockRecord;assert(RecordingCheckpoint::decode(clockBytes,TOKEN,ROLE,current.stem.c_str(),clockRecord));
 assert(clockRecord.utc==utcAnchor-900);savedClock.close();current=Clip{};clockMs=1000;

 for(unsigned failure=0;failure<=4;++failure){
  prepare(directory+"/rename_"+std::to_string(failure));auto raw=make();assert(recovery.begin(identity,ROLE,TOKEN));
  const String target=recovery.destination(),scratch=recovery.temporary();SD_MMC.failRenameAt=failure;drive();
  assert((SD_MMC.exists(base+".part")&&read(base+".part")==raw)||(SD_MMC.exists(scratch+".raw")&&read(scratch+".raw")==raw));
  SD_MMC.failRenameAt=0;
  if(SD_MMC.exists(base+".rec")){assert(recovery.begin(identity,ROLE,TOKEN));drive();}
  assert(SD_MMC.exists(ClipStorage::interruption(target)));JsonDocument doc;Clip clip;
  assert(loadJson(ClipStorage::metadata(target),doc)&&clipFromJson(doc.as<JsonObjectConst>(),clip)&&clip.recovered&&!clip.legacy&&clip.session==42&&sealed(clip));
  assert(SD_MMC.exists(scratch+".raw")&&read(scratch+".raw")==raw);assert(ClipStorage::occupied(identity));
 }
 prepare(directory+"/legacy_recovered");make();assert(recovery.begin(identity,ROLE,TOKEN));
 const String oldTarget=recovery.destination();drive();const auto metadataBefore=read(ClipStorage::metadata(oldTarget));
 const auto videoBefore=read(ClipStorage::interruption(oldTarget));
 assert(SD_MMC.rename(ClipStorage::interruption(oldTarget),oldTarget+"_LOCKED.avi"));
 assert(collectHistory());assert(read(ClipStorage::interruption(oldTarget))==videoBefore);
 assert(read(ClipStorage::metadata(oldTarget))==metadataBefore);inventory();assert(lockedCount==1&&normalCount==0);
 prepare(directory+"/legacy_fragment_label");make();assert(SD_MMC.remove(base+".rec"));assert(recovery.begin(identity,ROLE,TOKEN));
 const String legacyTarget=recovery.destination();drive();JsonDocument legacyMetadata;Clip legacyClip;
 assert(loadJson(ClipStorage::metadata(legacyTarget),legacyMetadata));assert(clipFromJson(legacyMetadata.as<JsonObjectConst>(),legacyClip));
 assert(legacyClip.recovered&&legacyClip.legacy);const auto legacyBytes=read(ClipStorage::interruption(legacyTarget));
 assert(SD_MMC.rename(ClipStorage::interruption(legacyTarget),legacyTarget+"_LOCKED.avi"));
 assert(collectHistory());assert(read(ClipStorage::interruption(legacyTarget))==legacyBytes);
 prepare(directory+"/write_failure");auto raw=make();assert(recovery.begin(identity,ROLE,TOKEN));
 while(recovery.busy())recovery.step();assert(recovery.ready());const String target=recovery.destination(),scratch=recovery.temporary();
 hostWriteBudget=0;drive();hostWriteBudget=SIZE_MAX;assert(read(base+".part")==raw);
 assert(recovery.begin(identity,ROLE,TOKEN));drive();assert(SD_MMC.exists(ClipStorage::interruption(target))&&read(scratch+".raw")==raw);
 prepare(directory+"/conflict");raw=make();assert(recovery.begin(identity,ROLE,TOKEN));const String conflict=recovery.destination()+"_LOCKED.avi";put(conflict,std::vector<uint8_t>(800,0));drive();assert(read(base+".part")==raw&&read(conflict)==std::vector<uint8_t>(800,0));
 prepare(directory+"/catalog");raw=make();cardSessionMax=0;assert(scanRecordingSessions()&&cardSessionMax==42);
 put("/system/clips/front_broken.json",{'{','b','a','d'});assert(collectHistory());assert(SD_MMC.exists("/system/clips/front_broken.json.bad"));assert(read(base+".part")==raw);
 // Many protected records do not grow the in-memory normal catalog.
 for(unsigned i=0;i<250;++i){
  Clip clip;clip.role=ROLE;clip.stem=String("/clips/front_catalog_")+String(i);clip.videoStem=clip.stem;clip.session=i+50;clip.start=100;clip.end=1600;clip.first=100;clip.last=1475;clip.frames=12;clip.good=true;clip.bytes=1000;
  strcpy(clip.payload,"1111111111111111111111111111111111111111111111111111111111111111");assert(clipSeal(clip,clip.seal));JsonDocument d;clipJson(d.to<JsonObject>(),clip);assert(saveJson(ClipStorage::metadata(clip.stem),d));put(clip.stem+"_LOCKED.avi",{'v'});
 }
 assert(collectHistory()&&history.empty()&&cardSessionMax==299);inventory();assert(lockedCount==250&&normalCount==0);
 std::filesystem::create_directories(SD_MMC.root+"/system/events");Event delayed;delayed.boot=2;delayed.sequence=2;delayed.session=55;delayed.path="/system/events/e_2_2.json";delayed.window={100,1475,800,true,true};delayed.catalogPending=true;events.push_back(delayed);Clip latest;latest.end=100000;recent.push_back(latest);
 for(unsigned i=0;i<300&&events[0].catalogPending;++i){const auto before=events[0].clips.size();assert(serviceCatalog());assert(events[0].clips.size()<=before+1);}
 assert(!catalogFailure&&!events[0].catalogPending&&events[0].verifying&&events[0].clips.size()==1&&events[0].clips[0].stem=="/clips/front_catalog_5");events.clear();
 std::cout<<"PASS delayed lookup beyond RAM history: one catalog entry per call, matching older locked clip found, unrelated records excluded, verification scheduled\n";
 prepare(directory+"/event");Clip clip;clip.role=ROLE;clip.stem=clip.videoStem="/clips/front_event";clip.session=1;clip.start=100;clip.end=1600;clip.first=100;clip.last=1475;clip.frames=12;clip.good=true;clip.bytes=1000;clip.gap=125;
 strcpy(clip.payload,"1111111111111111111111111111111111111111111111111111111111111111");assert(clipSeal(clip,clip.seal));put(clip.stem+"_LOCKED.avi",{'v'});
 Event e;e.boot=1;e.sequence=1;e.session=1;e.window={100,1475,800,true,true};e.path="/system/events/e_1_1.json";std::filesystem::create_directories(SD_MMC.root+"/system/events");e.clips.push_back(clip);
 assert(!verifyEvent(e));e.clips[0].verified=true;assert(verifyEvent(e));e.clips[0].recovered=true;assert(clipSeal(e.clips[0],e.clips[0].seal));assert(!verifyEvent(e));
 assert(saveEvent(e));JsonDocument manifest;assert(loadJson(e.path,manifest));const String tag=manifest["eventSeal"];assert(eventSealed(manifest));manifest["end"]=1476;manifest["eventSeal"]=tag;assert(!eventSealed(manifest));
 // Allocation failure must not turn a reduced document into a sealed/committed
 // event, or touch an existing manifest or its recovery staging file.
 const auto saved=read(e.path);const std::vector<uint8_t> staging={'r','e','t','a','i','n'};put(e.path+".tmp",staging);
 NoMemory unavailable;JsonDocument incomplete(&unavailable);incomplete["stage"]=2;assert(incomplete.overflowed());char seal[65];
 assert(!eventTag(incomplete,seal)&&!saveJson(e.path,incomplete));assert(read(e.path)==saved&&read(e.path+".tmp")==staging);
 JsonDocument oversized;oversized["data"]=std::string(17000,'x');assert(!oversized.overflowed());
 assert(!eventTag(oversized,seal)&&!saveJson(e.path,oversized));assert(read(e.path)==saved&&read(e.path+".tmp")==staging);
 JsonDocument replacement;replacement["stage"]=3;hostWriteBudget=3;assert(!saveJson(e.path,replacement));hostWriteBudget=SIZE_MAX;
 assert(read(e.path)==saved);assert(saveJson(e.path,replacement));assert(loadJson(e.path,manifest)&&manifest["stage"]==3);
 JsonDocument metadata;clipJson(metadata.to<JsonObject>(),clip);Clip parsed;assert(clipFromJson(metadata.as<JsonObjectConst>(),parsed));metadata["bytes"]=1001;assert(!clipFromJson(metadata.as<JsonObjectConst>(),parsed));
 std::cout<<"PASS actual camera persistence rejects overflowed/oversized JSON before writes or sealing; short writes retain the previous manifest and a later save recovers\n";
 std::cout<<"PASS actual protection policy rejects unverified/recovered footage; incident-window and clip-metadata tampering rejected\n";
 std::cout<<"PASS actual recovery commit: every rename failure, metadata short write, restart, conflicting destination, immutable original retention, sealed partial metadata, malformed-metadata quarantine, card session discovery and 250 locked clips without catalog growth\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'storage.cpp';f.write_text(pre+clip+globals+json+event+add+verify+catalog+retention+recovery+clock_code+checks,encoding='utf8');exe=p/'storage.exe'
 subprocess.run(['g++','-std=c++17','-I'+str(root.parent/'testing/system/platform'),'-Wall','-Wextra','-Werror','-I'+str(root/'../testing/main/camera'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(root/'SmartVest_SJKS01_Camera/src'),str(f),'-lcrypto','-o',str(exe)],check=True)
 subprocess.run([str(exe),str(p/'card')],check=True)
