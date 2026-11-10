"""Execute the recorder's actual bounded catalog/deletion worker on host SD files."""
from pathlib import Path
import ast,os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
s=(root/'SmartVest_SJKS01_Camera/src/CameraApp.cpp').read_text()
fixture=ast.parse(Path(__file__).with_name('test_camera_storage.py').read_text())
pieces={}
for node in fixture.body:
 if isinstance(node,ast.Assign) and isinstance(node.value,ast.Constant) and isinstance(node.value.value,str):
  pieces[node.targets[0].id]=node.value.value
pre=pieces['pre'].replace('constexpr uint32_t MAX_FRAME_GAP_MS=2000,LINK_MARGIN_MS=2000;', 'constexpr uint32_t LINK_MARGIN_MS=2000;').replace('constexpr size_t NORMAL_CLIPS=30;', '').replace('constexpr uint64_t FREE_RESERVE_BYTES=64ULL*1024*1024,MAX_AVI_BYTES=128ULL*1024*1024;', '')
clip=s[s.index('struct Clip {'):s.index('QueueHandle_t commands=')]
helpers=s[s.index('bool loadJson('):s.index('// FAT stores local wall time.')]
globals=pieces['globals']+r'''
#include "FileLibrary.h"
FileRequest fileRequest;FileReply fileReply;
bool fileRequestReady=false,fileReplyReady=false,sd=true,running=true;
uint64_t session=1000,lastSavedUtc=0;
uint32_t logStream=22;
struct DownloadStub{uint64_t size=0;bool active(){return false;}}downloadTransfer;
bool beginDownload(const FileRequest&,char*,size_t){return false;}

int snapshotMux=0;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
'''
idle=s[s.index('  if(now+12<nextFrame){'):s.index('  delay(1);',s.index('  if(now+12<nextFrame){'))]
wire=s[s.index('        if(pollCode==200&&d["id"].as<String>()==ROLE&&d["files"].is<JsonObject>()'):s.index('        httpSession.completed(operation,millis(),pollCode',s.index('        if(pollCode==200&&d["id"].as<String>()==ROLE&&d["files"].is<JsonObject>()'))]
schedule='void pollFiles(JsonDocument& d){int pollCode=200;\n'+wire+'}\n'+r'''
uint64_t nextFrame=0;uint32_t checkpointAt=0;
bool checkpoint(){return true;}
bool serviceEvidence(){return verifying;}
bool serviceCatalog(){return false;}
void serviceRecovery(){}
void idleTick(){uint64_t now=millis();
'''+idle+'}\n'
tests=r'''
void put(const String& p){File f=SD_MMC.open(p,FILE_WRITE);assert(f);assert(f.write(uint8_t('v'))==1);}
void make(unsigned n,bool locked,bool recovered=false,bool known=true){
 Clip c;c.role=ROLE;c.stem=c.videoStem=String("/clips/front_library_")+String(n);c.session=n;c.start=100;c.end=1600;c.first=100;c.last=1475;c.frames=12;c.good=true;c.bytes=1000;c.utcStart=known?1700000000000ULL+n*60000:0;c.recovered=recovered;
 strcpy(c.payload,"1111111111111111111111111111111111111111111111111111111111111111");assert(clipSeal(c,c.seal));
 JsonDocument d;clipJson(d.to<JsonObject>(),c);assert(saveJson(ClipStorage::metadata(c.stem),d));put(locked?(recovered?ClipStorage::interruption(c.stem):c.stem+"_LOCKED.avi"):c.stem+".avi");
 if(locked)++lockedCount;else{++normalCount;history.push_back(c);recent.push_back(c);}
}
void request(unsigned id,unsigned op){fileRequest=FileRequest{};fileRequest.id=id;fileRequest.stream=logStream;fileRequest.operation=op;fileRequest.receivedAt=clockMs;fileRequestReady=true;fileReplyReady=false;}
void drive(){for(unsigned i=0;i<2000&&!fileReplyReady;++i)serviceLibrary();assert(fileReplyReady);}
int main(int argc,char**argv){
 assert(argc==2);SD_MMC.root=argv[1];for(const char* p:{"/clips","/system/clips","/system/recording","/system/recovery"})std::filesystem::create_directories(SD_MMC.root+p);
 current.session=session;current.start=100;
 for(unsigned n=1;n<=12;++n)make(n,false);
 for(unsigned n=20;n<51;++n)make(n,true);
 // Real recorder idle scheduling must keep the library moving while evidence
 // verification spans many turns, with the same one-entry worker and SD files.
 verifying=true;nextFrame=clockMs+125;checkpointAt=clockMs;
 JsonDocument joint;joint["id"]=ROLE;joint["apiVersion"]=1;joint["command"]["type"]="LOCK";
 joint["files"]["id"]=100;joint["files"]["stream"]=logStream;joint["files"]["operation"]=1;
 pollFiles(joint);assert(fileRequestReady&&fileRequest.id==100);
 for(unsigned i=0;i<2000&&!fileReplyReady;++i)idleTick();
 assert(fileReplyReady&&fileReply.count==5&&!fileReply.error[0]);
 request(101,2);for(unsigned i=0;i<2000&&!fileReplyReady;++i)idleTick();
 assert(fileReplyReady&&fileReply.count==5&&fileReply.rows[0].locked);
 request(102,3);idleTick();assert(fileReplyReady&&fileReply.error[0]&&fileReply.deleted==0);
 verifying=false;
 request(1,2);drive();assert(fileReply.count==5&&fileReply.more&&fileReply.rows[0].session==50&&fileReply.rows[4].session==46);
 auto last=fileReply.rows[4];request(2,2);fileRequest.beforeSession=last.session;fileRequest.beforeStart=last.start;strcpy(fileRequest.beforeName,last.name);drive();assert(fileReply.count==5&&fileReply.rows[0].session==45);
 request(3,1);drive();assert(fileReply.count==5&&fileReply.rows[0].session==12&&!fileReply.rows[0].locked);
 // No unbounded list and no normal deletion while protection is active.
 events.push_back(Event{});request(4,3);drive();assert(fileReply.error[0]&&fileReply.deleted==0&&normalCount==12);events.clear();
 request(5,3);drive();assert(!fileReply.error[0]&&fileReply.deleted==12&&normalCount==0&&lockedCount==31&&history.empty()&&recent.empty());
 make(13,false);request(5,3);serviceLibrary();assert(normalCount==1); // duplicate is not replayed
 // A newly finalized/current-session clip is outside the deletion cutoff.
 make(1000,false);request(6,4);drive();assert(fileReply.deleted==32&&normalCount==1&&lockedCount==0);
 assert(SD_MMC.exists("/clips/front_library_1000.avi"));
 request(7,1);drive();assert(fileReply.count==1&&!fileReply.more&&fileReply.rows[0].session==1000);
 // A new incident preempts an already started operation before another removal.
 make(14,false);make(15,false);request(8,4);serviceLibrary();const auto deleted=libraryPage.deleted;events.push_back(Event{});drive();assert(fileReply.error[0]&&fileReply.deleted==deleted);events.clear();
 request(9,1);fileRequest.stream=999;drive();assert(fileReply.error[0]);
 request(10,3);fileRequest.receivedAt=clockMs-10001;drive();assert(fileReply.error[0]&&fileReply.deleted==0);
 make(16,false);SD_MMC.failRemoveAt=SD_MMC.removeCalls+1;request(11,3);drive();assert(fileReply.error[0]&&fileReply.deleted==0);assert(SD_MMC.exists("/clips/front_library_16.avi"));SD_MMC.failRemoveAt=0;
 request(12,1);serviceLibrary();clockMs+=90001;drive();assert(fileReply.error[0]);
 request(13,3);serviceLibrary();sd=false;drive();assert(fileReply.error[0]);sd=true;
 request(14,1);serviceLibrary();++session;drive();assert(fileReply.error[0]);
 make(900,true,true,false);make(901,true,true,true);request(15,2);drive();
 assert(fileReply.rows[0].reason==2&&fileReply.rows[0].utc>0);
 assert(fileReply.rows[1].reason==2&&fileReply.rows[1].utc==0);
 JsonDocument legacy;legacy["stem"]="/clips/front_legacy_protected";legacy["session"]=uint64_t(902);legacy["start"]=uint64_t(100);legacy["end"]=uint64_t(200);legacy["frames"]=1;legacy["good"]=true;legacy["recovered"]=true;
 assert(saveJson(ClipStorage::metadata("/clips/front_legacy_protected"),legacy));put("/clips/front_legacy_protected_LOCKED.avi");++lockedCount;
 request(17,2);drive();assert(fileReply.rows[0].reason==3);
 request(16,1);drive();for(unsigned i=0;i<fileReply.count;++i)assert(fileReply.rows[i].reason==0);
 const auto retained=lockedCount;request(18,3);drive();assert(lockedCount==retained);
 assert(SD_MMC.exists(ClipStorage::interruption("/clips/front_library_900")));
 assert(SD_MMC.exists(ClipStorage::interruption("/clips/front_library_901")));
 assert(SD_MMC.exists("/clips/front_legacy_protected_LOCKED.avi"));
 request(19,4);drive();assert(lockedCount==0);
 assert(!SD_MMC.exists(ClipStorage::interruption("/clips/front_library_900")));
 assert(!SD_MMC.exists(ClipStorage::interruption("/clips/front_library_901")));
 assert(!SD_MMC.exists("/clips/front_legacy_protected_LOCKED.avi"));

 // Explicit interruption cleanup removes recovered videos and their backup
 // payloads, including orphaned recovery archives and inactive fragments.
 current.stem="/clips/front_active_cleanup";current.start=100;
 make(910,true,true);make(911,true);make(912,false);make(1001,true,true);
 const String recovered="/clips/front_library_910";
 put(ClipStorage::internal(recovered,"recovery")+".raw");put(ClipStorage::internal(recovered,"recovery")+".rec");
 const String orphan="/clips/front_recovered_aaaaaaaaaaaaaaaaaaaaaaaa";
 for(const char* ext:{".raw",".rec",".avi"})put(ClipStorage::internal(orphan,"recovery")+ext);
 for(const char* ext:{".part",".rec",".idx"})put(ClipStorage::internal("/clips/front_old_fragment","recording")+ext);
 for(const char* ext:{".part",".rec"})put(ClipStorage::internal(current.stem,"recording")+ext);
 put("/system/recovery/unknown.raw");put("/system/recording/rear_other.part");
 // A recognized archive with a surviving public recording is not an orphan.
 const String keep="/clips/front_recovered_bbbbbbbbbbbbbbbbbbbbbbbb";
 put(ClipStorage::incident(keep));put(ClipStorage::internal(keep,"recovery")+".raw");
 const auto freeBefore=SD_MMC.freeBytes;
 joint["files"]["id"]=700;joint["files"]["operation"]=6;fileRequestReady=false;pollFiles(joint);assert(!fileRequestReady);
 joint["files"]["operation"]=7;fileReplyReady=false;pollFiles(joint);assert(fileRequestReady&&fileRequest.operation==7);drive();assert(!fileReply.error[0]&&fileReply.deleted==1&&fileReply.recoveryDeleted==8);
 assert(!SD_MMC.exists(ClipStorage::interruption(recovered)));
 assert(!SD_MMC.exists(ClipStorage::internal(recovered,"recovery")+".raw"));
 assert(!SD_MMC.exists(ClipStorage::internal(orphan,"recovery")+".raw"));
 assert(!SD_MMC.exists("/system/recording/front_old_fragment.part"));
 assert(SD_MMC.exists("/system/recording/front_active_cleanup.part"));
 assert(SD_MMC.exists("/system/recovery/unknown.raw")&&SD_MMC.exists("/system/recording/rear_other.part"));
 assert(SD_MMC.exists(ClipStorage::incident(keep))&&SD_MMC.exists(ClipStorage::internal(keep,"recovery")+".raw"));
 assert(SD_MMC.exists("/clips/front_library_911_LOCKED.avi")&&SD_MMC.exists("/clips/front_library_912.avi"));
 assert(SD_MMC.exists(ClipStorage::interruption("/clips/front_library_1001"))&&SD_MMC.freeBytes>freeBefore);
 const auto removed=SD_MMC.removeCalls;request(700,7);serviceLibrary();assert(SD_MMC.removeCalls==removed);
 make(913,true,true);SD_MMC.failRemoveAt=SD_MMC.removeCalls+1;request(701,7);drive();
 assert(fileReply.error[0]&&SD_MMC.exists(ClipStorage::interruption("/clips/front_library_913")));SD_MMC.failRemoveAt=0;
 request(702,7);serviceLibrary();const auto beforeIncident=SD_MMC.removeCalls;events.push_back(Event{});drive();
 assert(fileReply.error[0]&&SD_MMC.removeCalls==beforeIncident);events.clear();
 std::cout<<"PASS production camera file worker: bounded pages, ordering/cursors, normal/protected separation, deletion exclusion, duplicate requests, active clip preservation, incident interruption and restart rejection\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'library.cpp';f.write_text(pre+clip+globals+helpers+'\n'+(root/'SmartVest_SJKS01_Camera/src/FileLibraryWorker.h').read_text().replace('#pragma once', '').replace('clockMs()', 'millis()')+'\n'+schedule+'\n'+tests);exe=p/'library.exe'
 subprocess.run(['g++','-std=c++17','-I'+str(root.parent/'testing/system/platform'),'-Wall','-Wextra','-Werror','-I'+str(root/'../testing/main/camera'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(root/'SmartVest_SJKS01_Camera/src'),str(f),'-lcrypto','-o',str(exe)],check=True)
 subprocess.run([str(exe),str(p/'card')],check=True)
