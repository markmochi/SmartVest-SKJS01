"""Compile the actual serviceStorage/failStorage functions against deterministic
SD/Preferences adapters. No radio, SD hardware or complete camera sketch used."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
app=(root/'SmartVest_SJKS01_Camera/src/CameraApp.cpp').read_text(encoding="utf8")
failure=app[app.index('void failStorage('):app.index('\nbool migrateLegacy')]
service=app[app.index('void serviceStorage()'):app.index('\nbool verifyEvent')]
pre=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include "StorageRetry.h"
uint32_t hostNow=0;uint32_t millis(){return hostNow;}
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
struct {bool sd=false,recording=false;uint32_t statusRevision=1;char fault[96]{};} snapshot;
struct Clip{std::string stem;};struct Event{int stage=1;};
struct {template<class... A> void printf(const char*,A...){} } Serial;
void emitLog(const char*,const std::string&,const char*){}
unsigned replies=0;void reply(const Event& e){assert(e.stage==3);++replies;}
unsigned aborts=0;struct {void abort(){++aborts;}} writer;
struct Close {void close(){}} checkpointFile,recoveryDirectory;struct Cancel{void cancel(){}} verifier,recovery;bool verifying=false,recoveryVerifying=false,recentTruncated=false;uint32_t recoveryScanAt=0;
std::vector<int> history,recent;std::vector<Event> events;Clip current;
bool sd=false,camera=true,running=false,paused=false,storageFull=false;
uint32_t nextStorageProbe=0,normalCount=0,lockedCount=0,lastClipFrames=0,lastClipDuration=0,lastClipGap=0,clipNumber=0;
uint64_t freeAtBoundary=0,session=0,cardSessionMax=0,latestClipUtc=0,lastSavedUtc=0,cardTotalBytes=0;
std::string lastClipName,lastFault;
StorageRetry storageRetry;
constexpr uint64_t FREE_RESERVE_BYTES=64*1024*1024;
constexpr int CARD_NONE=0,SDMMC_FREQ_DEFAULT=20000;
unsigned begins=0,ends=0,probes=0,opens=0,nvsWrites=0;
bool allProtected=false;
bool cardInserted=false,validHistory=true,canTrim=true,canOpen=true,canSave=true,canMkdir=true,canMigrate=true;
struct {
 bool mounted=false;uint64_t free=128*1024*1024;
 void end(){mounted=false;++ends;}
 bool begin(const char*,bool one,bool format,int){assert(one&&!format);assert(!mounted);++begins;return mounted=cardInserted;}
 int cardType(){return mounted?1:CARD_NONE;}
 bool exists(const char*){return false;}
 bool mkdir(const char*){return canMkdir;}
 uint64_t totalBytes(){return 512ULL*1024*1024;}uint64_t usedBytes(){return totalBytes()-free;}
 int sectorSize(){return 512;}
 bool readRAW(uint8_t*,uint32_t sector){assert(sector==0);++probes;return cardInserted;}
} SD_MMC;
uint64_t savedSession=0;
struct Preferences {
 bool begin(const char*,bool){return true;}
 uint64_t getULong64(const char*,uint64_t){return savedSession;}
 size_t putULong64(const char*,uint64_t n){++nvsWrites;if(!canSave)return 0;savedSession=n;return sizeof(n);}
 void end(){}
};
struct DownloadStub{void stop(){assert(!snapshot.sd&&!snapshot.recording);}bool active(){return false;}}downloadTransfer;struct DownloadFileStub{void close(){}}downloadFile;bool downloadResume=false;
bool migrateLegacy(){return canMigrate;}
bool scanRecordingSessions(){return true;}
bool spaceFor(uint64_t n){return SD_MMC.free>=n;}
bool collectHistory(){assert(history.empty());if(!allProtected)history.push_back(1);return validHistory;}
void inventory(){normalCount=allProtected?0:1;lockedCount=2;freeAtBoundary=SD_MMC.free;}
bool trim(){return canTrim;}
bool openClip(){++opens;assert(events.empty());if(!canOpen)return false;running=true;return true;}
void publish(){snapshot.sd=sd;snapshot.recording=running;}
'''
checks=r'''
int main(){
 serviceStorage();assert(!sd&&!running&&begins==1&&nvsWrites==0);
 cardInserted=true;hostNow=4999;serviceStorage();assert(begins==1);
 hostNow=5000;serviceStorage();assert(sd&&running&&session==1&&opens==1&&nvsWrites==1);
 hostNow=6999;serviceStorage();assert(probes==0);
 hostNow=7000;serviceStorage();assert(probes==1&&sd);
 events.push_back({});events.push_back({});cardInserted=false;
 hostNow=9000;serviceStorage();assert(!sd&&!running&&!snapshot.sd&&!snapshot.recording&&replies==2&&events.empty());
 assert(nvsWrites==1);unsigned before=begins;
 cardInserted=true;hostNow=13999;serviceStorage();assert(begins==before);
 hostNow=14000;serviceStorage();assert(sd&&running&&session==2&&nvsWrites==2);
 paused=true;cardInserted=false;hostNow=16000;serviceStorage();assert(begins==before+1&&probes==2);
 paused=false;serviceStorage();assert(!sd&&!running);
 // Full / invalid / unwritable cards cannot become recording-ready.
 cardInserted=true;SD_MMC.free=1;hostNow=21000;serviceStorage();assert(!sd&&nvsWrites==2);
 SD_MMC.free=128*1024*1024;validHistory=false;hostNow=31000;serviceStorage();assert(!sd&&nvsWrites==2);
 validHistory=true;canSave=false;hostNow=51000;serviceStorage();assert(!sd&&savedSession==2);
 canSave=true;canOpen=false;hostNow=81000;serviceStorage();assert(!sd&&!running&&savedSession==3);
 canOpen=true;hostNow=111000;serviceStorage();assert(sd&&running&&session==4);
 // Mounted card and camera initialization failure remain separate.
 cardInserted=false;hostNow=113000;serviceStorage();camera=false;cardInserted=true;
 hostNow=118000;serviceStorage();assert(sd&&!running&&lastFault=="Camera initialization failed");
 // A conflicting legacy migration fails closed instead of overwriting files.
 cardInserted=false;hostNow=120000;serviceStorage();cardInserted=true;canMigrate=false;
 hostNow=125000;serviceStorage();assert(!sd&&!running);canMigrate=true;
 hostNow=135000;serviceStorage();assert(sd&&!running);
 cardSessionMax=UINT64_MAX;sd=false;running=false;storageRetry.healthy();SD_MMC.end();const auto writesBefore=nvsWrites;serviceStorage();assert(!sd&&!running&&nvsWrites==writesBefore);cardSessionMax=0;
 // A readable full card with only protected files stays mounted for browsing
 // and explicit deletion, but cannot advertise recording until space returns.
 sd=false;running=false;camera=true;allProtected=true;SD_MMC.free=1;storageRetry.healthy();serviceStorage();
 assert(sd&&!running&&storageFull&&lockedCount==2&&normalCount==0);
 hostNow+=2000;serviceStorage();assert(sd&&!running&&storageFull);
 SD_MMC.free=128*1024*1024;hostNow+=2000;serviceStorage();assert(sd&&running&&!storageFull);
 std::cout<<"PASS protected-only full card remains accessible; recording resumes after space is freed without remount or restart\n";
 // Backoff is capped and pause/ready gates and millis wrap work.
 StorageRetry retry;assert(retry.due(0,false,false));
 uint32_t now=0;for(uint32_t gap:{5000U,10000U,20000U,30000U,30000U}){
  retry.failed(now);assert(!retry.due(now+gap-1,false,false));now+=gap;assert(retry.due(now,false,false));
 }
 retry.healthy();retry.failed(0xfffffff0U);assert(!retry.due(4983,false,false));assert(retry.due(4984,false,false));
 assert(!retry.due(4984,true,false)&&!retry.due(4984,false,true));
 std::cout<<"PASS actual SD recovery functions: missing boot, late insertion, removal, reinsertion/new session, pending evidence failure, no formatting, pause, full/invalid/unwritable card, camera fault distinction, bounded retry/wrap\n";
}
'''
with tempfile.TemporaryDirectory() as d:
    src=Path(d)/'test.cpp';src.write_text(pre+failure+'\n'+service+checks,encoding='utf8')
    exe=Path(d)/'test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'SmartVest_SJKS01_Camera/src'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
