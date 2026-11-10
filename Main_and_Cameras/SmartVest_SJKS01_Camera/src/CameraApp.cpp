#include "CameraApp.h"
#include "../CameraConfig.h"
#include "EvidencePolicy.h"
#include "AviWriter.h"
#include "LinkPolicy.h"
#include "ClipNames.h"
#include "ClipStorage.h"
#include <sys/time.h>
#include <utime.h>
#include "CameraLog.h"
#include "FileLibrary.h"
#include "DownloadTransfer.h"
#include "DownloadSocket.h"
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "NetworkSession.h"
#include "CameraAuth.h"
#include "RecordingCheckpoint.h"
#include "AviVerifier.h"
#include "InterruptedAvi.h"
#include "StorageRetry.h"
#include <esp_camera.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <vector>
#include <algorithm>
namespace {
using namespace CameraConfig;
struct Command {uint32_t boot,sequence,age,requestMs; uint64_t received; bool known,test,valid;};
struct Ack {uint32_t boot,sequence; uint8_t stage;};
struct Snapshot {
  bool sd=false,recording=false;uint32_t frames=0;uint64_t lastFrameMs=0,session=0,captured=0,freeBytes=0,currentBytes=0,totalBytes=0,latestUtc=0;
  uint32_t statusRevision=1;
  uint32_t downloadId=0;uint64_t downloadSent=0,downloadSize=0;uint8_t downloadState=0;
  uint32_t finalized=0,deleted=0,normal=0,locked=0,activeLocks=0,lastFrames=0,lastDuration=0,lastGap=0;
  char current[81]{},lastClip[81]{},fault[96]{};
};
struct Clip {
  String stem,videoStem; String role; uint64_t utcStart=0; String clockSource="unsynced"; uint64_t start=0,end=0,first=0,last=0,session=0;
  uint32_t frames=0,gap=0; uint64_t bytes=0;char payload[65]{},seal[65]{};bool good=false,recovered=false,legacy=false,verified=false;
};
struct Event {
  uint32_t boot=0,sequence=0; bool test=false,complete=false;
  EvidencePolicy::Window window{}; uint64_t session=0;
  String path; std::vector<Clip> clips; uint8_t stage=1;
  uint32_t margin=LINK_MARGIN_MS;bool verifying=false,catalogPending=false;size_t verifyIndex=0;File catalog;
};
QueueHandle_t commands=nullptr,acks=nullptr,wifiNotices=nullptr;
bool servicesReady=false,startupAllowed=false;uint32_t startupRetryAt=0;
portMUX_TYPE snapshotMux=portMUX_INITIALIZER_UNLOCKED;
Snapshot snapshot;
FileRequest fileRequest;FileReply fileReply;bool fileRequestReady=false,fileReplyReady=false;
DownloadTransfer<DownloadSocket> downloadTransfer;File downloadFile;bool downloadResume=false;
uint32_t downloadControlId=0,downloadControlUntil=0;bool downloadControlStop=false;
void endDownload(bool);bool beginDownload(const FileRequest&,char*,size_t);
uint64_t latestClipUtc=0,cardTotalBytes=0,lastSavedUtc=0;
CameraLogs::Buffer<32> logBuffer;
uint32_t logStream=0,finalizedCount=0,deletedCount=0,normalCount=0,lockedCount=0;
uint64_t capturedCount=0,freeAtBoundary=0,utcAnchor=0,utcAt=0;
char utcSource[24]="unsynced";String lastClipName,lastFault;
uint32_t lastClipFrames=0,lastClipDuration=0,lastClipGap=0;
void emitLog(const char* type,const String& clip,const char* detail,uint32_t boot=0,uint32_t incident=0){
  const uint64_t at=esp_timer_get_time()/1000ULL;
  portENTER_CRITICAL(&snapshotMux);logBuffer.add(at,type,clip.c_str(),detail,boot,incident);portEXIT_CRITICAL(&snapshotMux);
}
void inventory(){
  normalCount=0;lockedCount=0;File dir=SD_MMC.open("/clips");
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){
    String name=f.name();f.close();if(!name.endsWith(".avi"))continue;
    if(!name.startsWith("/"))name="/clips/"+name;
    String stem;bool locked=false;
    if(!ClipStorage::splitVideo(name,stem,locked))continue;
    if(locked || ClipStorage::protectedClip(stem,stem))++lockedCount;else ++normalCount;
  }
  const uint64_t total=SD_MMC.totalBytes(),used=SD_MMC.usedBytes();cardTotalBytes=total;freeAtBoundary=total>=used?total-used:0;
}
std::vector<Clip> history,recent;bool recentTruncated=false;
uint64_t cardSessionMax=0;
File checkpointFile;uint32_t checkpointAt=0;
AviVerifier verifier;bool verifying=false;uint32_t verifyingBoot=0,verifyingSequence=0;
String storageWarning;
InterruptedAvi recovery;File recoveryDirectory;uint32_t recoveryScanAt=0;bool recoveryVerifying=false;Clip recoveryClip;
uint32_t cameraRetryAt=0,cameraBackoff=1000;
std::vector<Event> events;
AviWriter writer;
Clip current;
uint64_t session=0,nextFrame=0;
uint32_t clipNumber=0;
bool sd=false,camera=false,running=false,paused=false,storageFull=false;
StorageRetry storageRetry;
uint32_t nextStorageProbe=0;
uint64_t clockMs() {return esp_timer_get_time()/1000ULL;}
void publish() {
  // Never call VFS/SD functions with interrupts disabled: even position() may
  // take a filesystem lock. Only the finished fixed-size copy is protected.
  Snapshot next;
  next.sd=sd;next.recording=running;next.frames=current.frames;next.lastFrameMs=current.last;
  next.totalBytes=cardTotalBytes;next.latestUtc=latestClipUtc;
  next.downloadId=downloadTransfer.id;next.downloadState=downloadTransfer.state;next.downloadSent=downloadTransfer.sent;next.downloadSize=downloadTransfer.size;
  next.session=session;next.captured=capturedCount;next.freeBytes=freeAtBoundary;next.currentBytes=writer.size();
  next.finalized=finalizedCount;next.deleted=deletedCount;next.normal=normalCount;next.locked=lockedCount;
  next.activeLocks=events.size();next.lastFrames=lastClipFrames;next.lastDuration=lastClipDuration;next.lastGap=lastClipGap;
  snprintf(next.current,sizeof(next.current),"%s",current.stem.c_str());
  snprintf(next.lastClip,sizeof(next.lastClip),"%s",lastClipName.c_str());snprintf(next.fault,sizeof(next.fault),"%s",lastFault.length()?lastFault.c_str():storageWarning.c_str());
  portENTER_CRITICAL(&snapshotMux);
  next.statusRevision=snapshot.statusRevision;
  if(next.sd!=snapshot.sd || next.recording!=snapshot.recording || next.normal!=snapshot.normal || next.locked!=snapshot.locked ||
     next.downloadId!=snapshot.downloadId||next.downloadState!=snapshot.downloadState||strcmp(next.fault,snapshot.fault)!=0) ++next.statusRevision;
  snapshot=next;
  portEXIT_CRITICAL(&snapshotMux);
}
bool loadJson(const String& path,JsonDocument& doc) {
  File f=SD_MMC.open(path,FILE_READ);
  if(!f || f.size()>16384) return false;
  return !deserializeJson(doc,f);
}
bool saveJson(const String& path,JsonDocument& doc) {
  // A durable pin is independent of this replace operation. An interrupted
  // JSON update therefore never makes pinned footage eligible for deletion.
  if(doc.overflowed())return false;
  const size_t expected=measureJson(doc);
  if(!expected||expected>16384)return false;
  const String temp=path+".tmp";
  SD_MMC.remove(temp);
  File f=SD_MMC.open(temp,FILE_WRITE);
  if(!f || serializeJson(doc,f)!=expected) {f.close();return false;}
  f.flush(); f.close();
  JsonDocument verify;
  if(!loadJson(temp,verify)) return false;
  String a,b;
  if(serializeJson(doc,a)!=expected||serializeJson(verify,b)!=expected||a.length()!=expected||b.length()!=expected||a!=b)return false;
  if(SD_MMC.exists(path) && !SD_MMC.remove(path)) return false;
  return SD_MMC.rename(temp,path);
}
const char* clipKey(const Clip& c){return c.role=="front"?CameraPairing::FRONT_KEY:CameraPairing::REAR_KEY;}
bool clipSeal(const Clip& c,char* tag){
  if(!CameraCrypto::validHex(c.payload,64)||(c.role!="front"&&c.role!="rear"))return false;
  CameraCrypto::Mac mac;if(!mac.begin(clipKey(c))||!mac.field("SMARTVEST CLIP 2")||!mac.field(c.role.c_str())||!mac.field(c.stem.c_str())||!mac.field(c.videoStem.length()?c.videoStem.c_str():c.stem.c_str())||!mac.field(c.clockSource.c_str()))return false;
  const uint64_t values[]={c.session,c.utcStart,c.start,c.end,c.first,c.last,c.bytes,c.frames,c.gap,uint64_t(c.good),uint64_t(c.recovered),uint64_t(c.legacy)};
  char number[21];for(uint64_t value:values){snprintf(number,sizeof(number),"%llu",(unsigned long long)value);if(!mac.field(number))return false;}
  return mac.field(c.payload)&&mac.finish(tag);
}
bool sealed(const Clip& c){char expected[65];return clipSeal(c,expected)&&CameraCrypto::equal(expected,c.seal,64);}
void clipJson(JsonObject out,const Clip& c) {
  if(c.utcStart)out["utcStartMs"]=c.utcStart;else out["utcStartMs"]=nullptr;
  out["role"]=c.role;out["clockSource"]=c.clockSource;out["nameOffsetMinutes"]=NAME_UTC_OFFSET_MINUTES;
  out["schema"]=2;out["bytes"]=c.bytes;out["payloadSha256"]=c.payload;out["seal"]=c.seal;out["recovered"]=c.recovered;out["legacy"]=c.legacy;
  out["stem"]=c.stem;out["videoStem"]=c.videoStem.length()?c.videoStem:c.stem;out["session"]=c.session;
  out["start"]=c.start;out["end"]=c.end;
  out["first"]=c.first;out["last"]=c.last;
  out["frames"]=c.frames;out["maxGapMs"]=c.gap;out["good"]=c.good;
}
bool clipFromJson(JsonObjectConst o,Clip& c) {
  if(!o["stem"].is<const char*>() || !o["session"].is<uint64_t>() ||
     !o["start"].is<uint64_t>() || !o["end"].is<uint64_t>() ||
     !o["frames"].is<uint32_t>() || !o["good"].is<bool>()) return false;
  c.stem=o["stem"].as<String>();c.session=o["session"];
  c.start=o["start"];c.end=o["end"];c.first=o["first"];c.last=o["last"];
  c.frames=o["frames"];c.gap=o["maxGapMs"];c.good=o["good"];
  // Never use arbitrary JSON paths for deletion or pinning.
  if(!ClipNames::validStem(c.stem.c_str()))return false;
  c.videoStem=o["videoStem"]|c.stem;
  if(!ClipNames::validStem(c.videoStem.c_str()))return false;
  c.role=o["role"]|"";c.utcStart=o["utcStartMs"]|uint64_t(0);c.clockSource=o["clockSource"]|"unsynced";
  c.bytes=o["bytes"]|uint64_t(0);c.recovered=o["recovered"]|false;c.legacy=o["legacy"]|false;c.verified=false;
  snprintf(c.payload,sizeof(c.payload),"%s",o["payloadSha256"]|"");snprintf(c.seal,sizeof(c.seal),"%s",o["seal"]|"");
  if(c.end<c.start||c.frames>1024||c.last<c.first||c.clockSource.length()>23)return false;
  if(o["schema"]==2){
    if(!o["bytes"].is<uint64_t>()||!o["first"].is<uint64_t>()||!o["last"].is<uint64_t>()||!o["maxGapMs"].is<uint32_t>()||
       !o["recovered"].is<bool>()||!o["legacy"].is<bool>()||!CameraCrypto::validHex(o["payloadSha256"]|"",64)||
       !CameraCrypto::validHex(o["seal"]|"",64)||c.stem.length()!=strlen(c.stem.c_str())||
       c.videoStem.length()!=strlen(c.videoStem.c_str())||c.clockSource.length()!=strlen(c.clockSource.c_str()))return false;
    return c.good&&c.bytes>=224&&c.role==ROLE&&sealed(c);
  }
  c.legacy=true;return true;
}
String videoStem(const Clip& c) {return c.videoStem.length()?c.videoStem:c.stem;}
String videoPath(const Clip& c) {return ClipStorage::video(videoStem(c));}
bool isPinned(const Clip& c) {return ClipStorage::protectedClip(c.stem,videoStem(c));}
bool pin(const Clip& c) {
  const bool wasPinned=isPinned(c),saved=SD_MMC.exists(videoPath(c));
  const String before=videoPath(c);
  if(!ClipStorage::pin(c.stem,videoStem(c)))return false;
  if(lastClipName==before)lastClipName=videoPath(c);
  if(!wasPinned){
    if(saved){if(normalCount)--normalCount;++lockedCount;}
    emitLog("clip_pinned",videoPath(c),"Protection saved; finalized AVI marked LOCKED");
  }
  return true;
}
// FAT stores local wall time. The network task sets the real system clock;
// utime also corrects the modified date of a clip opened before first sync.
void stampFile(const String& path,uint64_t utcMs) {
  if(utcMs<1577836800000ULL || utcMs>=4102444800000ULL)return;
  const time_t seconds=time_t(utcMs/1000);
  struct utimbuf times{seconds,seconds};
  if(utime((String("/sdcard")+path).c_str(),&times)!=0)
    emitLog("file_date_warning",path,"Could not update SD file modified date");
}
void refreshClipTime(Clip& c) {
  uint64_t anchor,at;char source[24];
  portENTER_CRITICAL(&snapshotMux);
  anchor=utcAnchor;at=utcAt;snprintf(source,sizeof(source),"%s",utcSource);
  portEXIT_CRITICAL(&snapshotMux);
  const uint64_t now=clockMs();
  c.utcStart=ClipNames::startUtc(anchor,at,now,c.start);
  if(c.utcStart)c.clockSource=source;
}
bool eventTag(JsonDocument& doc,char* output){
  const size_t expected=measureJson(doc);
  String text;if(doc.overflowed()||expected>16000||serializeJson(doc,text)!=expected||text.length()!=expected)return false;
  CameraCrypto::Mac mac;return mac.begin(TOKEN)&&mac.field("SMARTVEST EVENT 2")&&mac.field(ROLE)&&mac.add(text.c_str(),text.length())&&mac.finish(output);
}
bool eventSealed(JsonDocument& doc){
  const String saved=doc["eventSeal"]|"";doc.remove("eventSeal");char expected[65];
  return eventTag(doc,expected)&&CameraCrypto::equal(expected,saved.c_str(),64);
}
bool saveEvent(Event& e) {
  JsonDocument d;d["schema"]=1;d["role"]=ROLE;d["boot"]=e.boot;
  d["sequence"]=e.sequence;d["session"]=e.session;d["test"]=e.test;
  d["stage"]=e.stage;d["triggerTimeKnown"]=e.window.known;
  d["prePossible"]=e.window.prePossible;d["start"]=e.window.start;
  d["end"]=e.window.end;d["trigger"]=e.window.trigger;
  d["transportMarginMs"]=e.margin;
  auto a=d["clips"].to<JsonArray>();
  for(const auto& c:e.clips) clipJson(a.add<JsonObject>(),c);
  char seal[65];if(!eventTag(d,seal))return false;d["eventSeal"]=seal;
  return saveJson(e.path,d);
}
void reply(const Event& e) {
  Ack a{e.boot,e.sequence,e.stage};xQueueSend(acks,&a,0);
}
bool addClip(Event& e,const Clip& c) {
  for(auto& old:e.clips) if(old.stem==c.stem) {old=c;return true;}
  if(e.clips.size()>=16 || !pin(c)) return false;
  e.clips.push_back(c);return true;
}
void failStorage(const char* why) {
  lastFault=why;emitLog("recording_error",current.stem,why);
  Serial.printf("[CAM] Storage stopped: %s; %s; incomplete files retained.\n",why,paused?"retry paused":"mount retry scheduled");
  sd=false;running=false;
  // Stop advertising readiness before potentially slow failed-card cleanup.
  portENTER_CRITICAL(&snapshotMux);
  const bool statusChanged=snapshot.sd || snapshot.recording || strcmp(snapshot.fault,why)!=0;
  snapshot.sd=false;snapshot.recording=false;snprintf(snapshot.fault,sizeof(snapshot.fault),"%s",why);
  if(statusChanged)++snapshot.statusRevision;
  portEXIT_CRITICAL(&snapshotMux);
  downloadTransfer.stop();downloadFile.close();downloadResume=false;
  writer.abort();checkpointFile.close();verifier.cancel();verifying=false;recovery.cancel();recoveryDirectory.close();recoveryVerifying=false;
  // No further writes to a missing/untrusted card. Pending on-card manifests
  // stay incomplete; a later command validates them under the existing rules.
  for(auto& e:events){e.stage=3;reply(e);}
  events.clear();
  storageRetry.failed(millis());
  publish();
  // Unmount on the next recovery attempt: a caller can still hold a local File
  // until it returns (e.g. acceptCommand's directory scan).
}
// Move recognized old sidecars out of the recordings folder. Rename only:
// never truncate destinations or discard interrupted/unknown user data.
bool migrateLegacy() {
  for(;;) {
    // Bound RAM use even on an old card with thousands of locked clips.
    std::vector<String> names;
    File dir=SD_MMC.open("/clips");
    if(!dir || !dir.isDirectory())return false;
    for(File f=dir.openNextFile();f;f=dir.openNextFile()) {
      String name=f.name();const bool directory=f.isDirectory();f.close();
      if(directory)continue;
      if(!name.startsWith("/"))name="/clips/"+name;
      if(ClipStorage::legacyDestination(name).length())names.push_back(name);
      if(names.size()==32)break;
    }
    dir.close();
    for(const auto& name:names)if(!ClipStorage::migrateSidecar(name))return false;
    if(names.size()<32)break;
  }
  return true;
}
bool trim();
bool collectHistory() {
  File dir=SD_MMC.open("/system/clips");if(!dir||!dir.isDirectory())return false;
  cardSessionMax=0;storageWarning="";
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){
    String name=f.name();f.close();if(!name.endsWith(".json"))continue;
    if(!name.startsWith("/"))name="/system/clips/"+name;
    JsonDocument d;Clip c;
    if(!loadJson(name,d)||!clipFromJson(d.as<JsonObjectConst>(),c)||name!=ClipStorage::metadata(c.stem)){
      if(!SD_MMC.exists(name+".bad"))SD_MMC.rename(name,name+".bad");
      storageWarning="Damaged metadata retained separately; inspect the card.";emitLog("metadata_quarantined",name,"Video retained; recording can continue");continue;
    }
    if(!SD_MMC.exists(videoPath(c)))continue;
    latestClipUtc=std::max(latestClipUtc,c.utcStart);
    cardSessionMax=std::max(cardSessionMax,c.session);
    if(isPinned(c)){if((d["schema"]==2&&(c.recovered||!c.legacy)&&!ClipStorage::labelProtected(videoStem(c),c.recovered))||!pin(c)){storageWarning="A protection marker needs card inspection; footage retained.";emitLog("protection_warning",c.stem,"Existing protected footage retained; recording continues");}continue;}
    history.push_back(c);
    std::sort(history.begin(),history.end(),[](const Clip&a,const Clip&b){return a.session<b.session||(a.session==b.session&&a.start<b.start);});
    if(history.size()>NORMAL_CLIPS&&!trim())return false;
  }
  return true;
}
bool deleteOldest(){
  if(history.empty())return false;
  const Clip c=history.front();history.erase(history.begin());
  if(isPinned(c))return true;
  if(!c.good||!SD_MMC.exists(videoPath(c))){storageWarning="Unusable ordinary clip retained; inspect the card.";return true;}
  if(!SD_MMC.remove(videoPath(c)))return false;
  if(!SD_MMC.remove(ClipStorage::metadata(c.stem)))return false;
  for(auto it=recent.begin();it!=recent.end();)if(it->stem==c.stem)it=recent.erase(it);else ++it;
  if(normalCount)--normalCount;++deletedCount;emitLog("clip_deleted",c.stem,"Oldest ordinary recording reclaimed; protected evidence retained");return true;
}
bool trim(){
  for(auto it=history.begin();it!=history.end();)if(isPinned(*it))it=history.erase(it);else ++it;
  while(history.size()>NORMAL_CLIPS)if(!deleteOldest())return false;
  return true;
}
bool spaceFor(uint64_t required){
  for(;;){
    const uint64_t total=SD_MMC.totalBytes(),used=SD_MMC.usedBytes();
    if(total>=used&&total-used>=required){freeAtBoundary=total-used;return true;}
    if(history.empty())return false;
    if(!deleteOldest())return false;
    storageWarning="Space reclaimed from ordinary clips; available prehistory may be shorter.";
  }
}
bool checkpoint(){
  if(!checkpointFile)return false;
  if(!current.utcStart)refreshClipTime(current);
  RecordingCheckpoint::State record;record.session=current.session;record.start=current.start;record.first=current.first;
  record.last=current.last;record.utc=current.utcStart;record.bytes=writer.size();record.frames=writer.count();record.gap=current.gap;record.at=clockMs();snprintf(record.video,sizeof(record.video),"%s",current.videoStem.c_str());
  uint8_t bytes[RecordingCheckpoint::SIZE];
  if(!writer.checkpoint(record.payload)||!RecordingCheckpoint::encode(record,TOKEN,ROLE,current.stem.c_str(),bytes))return false;
  if(checkpointFile.write(bytes,sizeof(bytes))!=sizeof(bytes))return false;
  checkpointFile.flush();checkpointAt=millis();return true;
}
bool openClip() {
  current=Clip{};current.role=ROLE;current.session=session;current.start=clockMs();
  refreshClipTime(current);
  char name[81];ClipNames::make(name,sizeof(name),ROLE,session,++clipNumber,current.utcStart,NAME_UTC_OFFSET_MINUTES);current.stem=name;
  if(!ClipStorage::choose(current.stem))return false;
  for(auto& e:events) if(EvidencePolicy::overlaps(current.start,current.start,e.window)) {
    if(!addClip(e,current) || !saveEvent(e)) return false;
  }
  if(!spaceFor(FREE_RESERVE_BYTES))return false;
  if(!writer.begin(ClipStorage::internal(current.stem,"recording"),640,480)) return false;
  const String checkpointPath=ClipStorage::internal(current.stem,"recording")+".rec";
  if(SD_MMC.exists(checkpointPath))return false;
  checkpointFile=SD_MMC.open(checkpointPath,FILE_WRITE);
  if(!checkpoint())return false;
  running=true;nextFrame=clockMs();publish();emitLog("clip_started",current.stem,"Recording a new one-minute segment");return true;
}
bool closeClip() {
  current.end=clockMs();current.frames=writer.count();
  // Time may have arrived during this segment. Back-calculate its start
  // using this boot's monotonic clock; keep incident/metadata identity stable.
  if(!current.utcStart)refreshClipTime(current);
  current.videoStem=current.stem;
  if(current.utcStart && current.stem.indexOf("_time-unknown_")>=0) {
    char name[81];ClipNames::make(name,sizeof(name),ROLE,session,clipNumber,current.utcStart,NAME_UTC_OFFSET_MINUTES);
    current.videoStem=name;
    if(!ClipStorage::choose(current.videoStem))return false;
  }
  const String destination=isPinned(current)?ClipStorage::incident(current.videoStem):current.videoStem+".avi";
  if(!checkpoint())return false;checkpointFile.close();
  current.good=writer.close(uint32_t(current.end-current.start),destination);
  current.bytes=writer.finalizedSize();snprintf(current.payload,sizeof(current.payload),"%s",writer.payloadDigest());
  if(!current.good||!clipSeal(current,current.seal))return false;
  JsonDocument d;clipJson(d.to<JsonObject>(),current);
  if(!current.good || !saveJson(ClipStorage::metadata(current.stem),d)) return false;
  stampFile(destination,current.utcStart);
  stampFile(ClipStorage::metadata(current.stem),current.utcStart);
  Serial.printf("[CLIP] %s durationMs=%lu frames=%lu avgFps=%.2f maxGapMs=%lu finalized=%d\n",current.stem.c_str(),(unsigned long)(current.end-current.start),(unsigned long)current.frames,current.end>current.start?1000.0*current.frames/(current.end-current.start):0.0,(unsigned long)current.gap,current.good);
  if(isPinned(current))++lockedCount;else ++normalCount;
  lastSavedUtc=current.utcStart;latestClipUtc=std::max(latestClipUtc,current.utcStart);
  ++finalizedCount;lastClipName=videoPath(current);lastClipFrames=current.frames;lastClipDuration=uint32_t(current.end-current.start);lastClipGap=current.gap;
  emitLog("clip_saved",videoPath(current),"AVI finalized; metadata flushed and verified");
  history.push_back(current);recent.push_back(current);
  if(recent.size()>40){recent.erase(recent.begin());recentTruncated=true;}
  for(auto& e:events) if(EvidencePolicy::overlaps(current.start,current.end,e.window)) {
    if(!addClip(e,current) || !saveEvent(e)) return false;
  }
  if(isPinned(current) && !pin(current))return false;
  SD_MMC.remove(ClipStorage::internal(current.stem,"recording")+".rec");
  running=false;publish();return true;
}
bool scanRecordingSessions(){
  File dir=SD_MMC.open("/system/recording");if(!dir)return false;
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){
    String path=f.name();const uint64_t size=f.size();f.close();if(!path.startsWith("/"))path="/system/recording/"+path;
    if(!path.endsWith(".rec")||size>128*RecordingCheckpoint::SIZE)continue;
    const String stem="/clips/"+path.substring(18,path.length()-4);if(!ClipNames::validStem(stem.c_str()))continue;
    File record=SD_MMC.open(path,FILE_READ);uint8_t bytes[RecordingCheckpoint::SIZE];
    while(record&&record.available()>=int(sizeof(bytes))){
      if(record.read(bytes,sizeof(bytes))!=sizeof(bytes))break;
      RecordingCheckpoint::State value;if(RecordingCheckpoint::decode(bytes,TOKEN,ROLE,stem.c_str(),value))cardSessionMax=std::max(cardSessionMax,value.session);
    }
  }
  return true;
}
void recoveryWarning(const char* why){storageWarning=why;emitLog("recovery_pending",recovery.identity(),why);recovery.cancel();recoveryVerifying=false;}
void serviceRecovery(){
  if(verifying)return;
  if(recoveryVerifying){
    verifier.step();if(!verifier.done())return;
    if(!verifier.good()){recoveryWarning("Recovery output could not be verified; original retained.");return;}
    const String video=ClipStorage::interruption(recovery.destination());
    const bool existed=SD_MMC.exists(ClipStorage::video(recovery.destination()));
    if(!ClipStorage::labelProtected(recovery.destination(),true)){recoveryWarning("Recovered recording label conflicts; both files retained.");return;}
    if(!SD_MMC.exists(video)&&!SD_MMC.rename(recovery.temporary()+".avi",video)){recoveryWarning("Recovery rename failed; original retained.");return;}
    JsonDocument doc;clipJson(doc.to<JsonObject>(),recoveryClip);
    const String meta=ClipStorage::metadata(recoveryClip.stem);
    if(SD_MMC.exists(meta)){
      JsonDocument saved;Clip old;
      if(!loadJson(meta,saved)||!clipFromJson(saved.as<JsonObjectConst>(),old)||strcmp(old.seal,recoveryClip.seal)){
        recoveryWarning("Recovery metadata conflicts; both recordings retained.");return;
      }
    }else if(!saveJson(meta,doc)){recoveryWarning("Recovery metadata could not be saved; original retained.");return;}
    if(!ClipStorage::reserveIdentity(recovery.identity())){recoveryWarning("Recovered footage saved; original identity marker needs inspection.");return;}
    const String archived=recovery.temporary()+".raw";
    if(recovery.original()!=archived&&SD_MMC.exists(recovery.original())&&(!SD_MMC.exists(archived)?!SD_MMC.rename(recovery.original(),archived):true)){
      recoveryWarning("Recovered video saved; original archive needs inspection.");return;
    }
    const String journal=ClipStorage::internal(recovery.identity(),"recording")+".rec";
    if(SD_MMC.exists(journal)&&!SD_MMC.exists(recovery.temporary()+".rec"))SD_MMC.rename(journal,recovery.temporary()+".rec");
    // The verified public copy is committed; only a redundant private copy
    // generated from the retained original can now be removed.
    if(SD_MMC.exists(recovery.temporary()+".avi"))SD_MMC.remove(recovery.temporary()+".avi");
    emitLog("clip_recovered",video,recovery.isLegacy()?"Legacy fragment protected; original timing and integrity cannot be proven":"Checkpointed fragment verified and protected; incomplete incident coverage");
    latestClipUtc=std::max(latestClipUtc,recoveryClip.utcStart);
    if(!existed){++lockedCount;ClipStorage::recordProtectionTime(recovery.destination());}recovery.cancel();recoveryVerifying=false;publish();return;
  }
  if(recovery.ready()){
    recoveryClip=Clip{};auto& c=recoveryClip;const auto& record=recovery.checkpoint();
    c.stem=c.videoStem=recovery.destination();c.role=ROLE;c.session=record.session;c.start=record.start;c.end=record.start+recovery.duration();
    c.first=record.first;c.last=record.last;c.utcStart=record.utc;c.clockSource=record.utc?"recovered":"unsynced";
    c.frames=recovery.frames();c.gap=record.gap;c.bytes=recovery.bytes();c.good=c.recovered=true;c.legacy=recovery.isLegacy();
    snprintf(c.payload,sizeof(c.payload),"%s",recovery.digest());
    if(!clipSeal(c,c.seal)){recoveryWarning("Recovery integrity seal failed; original retained.");return;}
    const String publicPath=ClipStorage::video(c.videoStem);
    const String check=SD_MMC.exists(publicPath)?publicPath:recovery.temporary()+".avi";
    if(!verifier.begin(check,c.frames,recovery.duration(),c.bytes,c.payload)){recoveryWarning("Recovery output is invalid; original retained.");return;}
    recoveryVerifying=true;return;
  }
  if(recovery.busy()){recovery.step();return;}
  if(recovery.failed()){recoveryWarning("An interrupted recording needs card inspection; original retained.");return;}
  if(!recoveryDirectory){
    if(int32_t(millis()-recoveryScanAt)<0)return;
    recoveryDirectory=SD_MMC.open("/system/recording");if(!recoveryDirectory){recoveryScanAt=millis()+30000;return;}
  }
  File file=recoveryDirectory.openNextFile();
  if(!file){recoveryDirectory.close();recoveryScanAt=millis()+30000;return;}
  String path=file.name();file.close();if(!path.startsWith("/"))path="/system/recording/"+path;
  bool journal=path.endsWith(".rec");if(!journal&&!path.endsWith(".part"))return;
  const String stem="/clips/"+path.substring(18,path.length()-(journal?4:5));
  if(!ClipNames::validStem(stem.c_str())||stem==current.stem)return;
  if(journal&&SD_MMC.exists(ClipStorage::internal(stem,"recording")+".part"))return;
  // A finalized, sealed recording with only a leftover checkpoint needs no
  // duplicate recovery. Its incident manifest still gets a fresh file check.
  JsonDocument metadata;Clip known;
  if(loadJson(ClipStorage::metadata(stem),metadata)&&clipFromJson(metadata.as<JsonObjectConst>(),known)&&known.good&&SD_MMC.exists(videoPath(known)))return;
  // Keep the recording reserve in addition to one worst-case recovery copy.
  const uint64_t total=SD_MMC.totalBytes(),used=SD_MMC.usedBytes();
  File source=SD_MMC.open(ClipStorage::internal(stem,"recording")+".part",FILE_READ);
  const uint64_t required=(source?source.size():MAX_AVI_BYTES)+FREE_RESERVE_BYTES+32768;source.close();
  if(total<used||total-used<required){storageWarning="Recovery waiting for free space; original retained.";return;}
  recovery.begin(stem,ROLE,TOKEN);
}
void serviceStorage() {
  if(paused)return; // Explicit operator stop must never auto-resume recording.
  if(sd){
    if(int32_t(millis()-nextStorageProbe)>=0){
      nextStorageProbe=millis()+2000;
      // cardType() is cached. A read checks the actual card even when recording
      // is unavailable. All card access belongs to this task.
      alignas(4) uint8_t sector[512];
      if(SD_MMC.sectorSize()!=512 || !SD_MMC.readRAW(sector,0))
        failStorage("SD card removed or unreadable");
      else if(storageFull&&camera&&!paused&&!downloadTransfer.active()&&spaceFor(FREE_RESERVE_BYTES)){
        storageFull=false;lastFault="";
        if(!openClip())failStorage("Memory card space restored but recording could not restart");
      }
    }
    return;
  }
  if(!storageRetry.due(millis(),sd,paused))return;
  writer.abort();SD_MMC.end(); // Release stale mount/handles before every retry.
  history.clear();recent.clear();recentTruncated=false;events.clear();current=Clip{};checkpointFile.close();verifier.cancel();verifying=false;recovery.cancel();recoveryDirectory.close();recoveryVerifying=false;recoveryScanAt=0;
  normalCount=lockedCount=0;freeAtBoundary=0;latestClipUtc=lastSavedUtc=cardTotalBytes=0;storageFull=false;
  lastClipName="";lastClipFrames=lastClipDuration=lastClipGap=0;
  bool mounted=SD_MMC.begin("/sdcard",true,false,SDMMC_FREQ_DEFAULT);
  if(mounted && SD_MMC.cardType()==CARD_NONE)mounted=false;
  if(mounted && !SD_MMC.exists("/clips"))mounted=SD_MMC.mkdir("/clips");
  for(const char* path:{"/system","/system/clips","/system/recording","/system/events","/system/recovery"})
    if(mounted && !SD_MMC.exists(path))mounted=SD_MMC.mkdir(path);
  if(mounted)mounted=migrateLegacy();
  if(mounted)mounted=collectHistory();
  if(mounted)mounted=scanRecordingSessions();
  if(mounted){inventory();mounted=trim();}
  if(mounted&&!spaceFor(FREE_RESERVE_BYTES)){
    const uint64_t total=SD_MMC.totalBytes(),used=SD_MMC.usedBytes();
    storageFull=total>=used&&total>0&&total-used<FREE_RESERVE_BYTES&&history.empty();
    if(!storageFull)mounted=false;
  }
  if(!mounted){
    SD_MMC.end();history.clear();
    normalCount=lockedCount=0;freeAtBoundary=0;
    lastFault="SD unavailable or not usable; automatic mount retry active";
    storageRetry.failed(millis());publish();return;
  }
  // Allocate a new persisted recording session on each successful remount.
  // Old-card history and incomplete evidence must never span this boundary.
  Preferences p;bool saved=p.begin("sv_camera",false);
  if(saved){
    const uint64_t next=std::max(p.getULong64("session",0),cardSessionMax)+1;
    saved=next && p.putULong64("session",next)==sizeof(uint64_t);
    if(saved)session=next;
    p.end();
  }
  if(!saved){failStorage("Cannot persist new SD recording session");return;}
  clipNumber=0;sd=true;lastFault=camera?"":"Camera initialization failed";
  if(storageFull)lastFault="Memory card is full. Delete saved clips to resume recording.";
  if(camera && !storageFull && !openClip()){failStorage("SD mounted but cannot start recording");return;}
  storageRetry.healthy();nextStorageProbe=millis()+2000;publish();
  emitLog("sd_ready","","SD mounted; new recording session started when camera is ready");
  Serial.printf("[SD] ready session=%llu recording=%d\n",(unsigned long long)session,running);
}

bool verifyEvent(const Event& e) {
  uint64_t first=UINT64_MAX,last=0;bool continuous=true,files=!e.clips.empty();
  for(const auto& c:e.clips){
    if(!c.good||!c.frames||!c.verified||c.legacy||c.recovered||!sealed(c)||!isPinned(c))files=false;
    if(c.session!=e.session||c.gap>MAX_FRAME_GAP_MS||(last&&c.first>last+MAX_FRAME_GAP_MS))continuous=false;
    first=std::min(first,c.first);last=std::max(last,c.last);
  }
  return EvidencePolicy::canProtect(e.window,first,last,continuous,files);
}
void finishEvents(){
  for(auto& e:events)if(!e.verifying&&!e.catalogPending&&current.end>=e.window.end+MAX_FRAME_GAP_MS){
    std::sort(e.clips.begin(),e.clips.end(),[](const Clip&a,const Clip&b){return a.first<b.first;});
    e.verifying=true;e.verifyIndex=0;
  }
}
bool serviceCatalog(){
  static size_t cursor=0;
  for(size_t n=0;n<events.size();++n){
    const size_t slot=(cursor+n)%events.size();auto& e=events[slot];if(!e.catalogPending)continue;
    cursor=(slot+1)%events.size();
    if(!e.catalog){e.catalog=SD_MMC.open("/system/clips");if(!e.catalog){failStorage("Cannot inspect retained evidence catalog");return true;}}
    File entry=e.catalog.openNextFile();
    if(entry){
      String path=entry.name();const bool directory=entry.isDirectory();entry.close();
      if(directory||!path.endsWith(".json"))return true;if(!path.startsWith("/"))path="/system/clips/"+path;
      JsonDocument doc;Clip clip;
      if(loadJson(path,doc)&&clipFromJson(doc.as<JsonObjectConst>(),clip)&&path==ClipStorage::metadata(clip.stem)&&
         clip.good&&clip.session==e.session&&EvidencePolicy::overlaps(clip.start,clip.end,e.window)){
        if(!addClip(e,clip)||!saveEvent(e)){failStorage("Cannot retain delayed incident footage");return true;}
      }
      return true;
    }
    e.catalog.close();e.catalogPending=false;
    if(!recent.empty()&&recent.back().end>=e.window.end+MAX_FRAME_GAP_MS){
      std::sort(e.clips.begin(),e.clips.end(),[](const Clip&a,const Clip&b){return a.first<b.first;});e.verifying=true;e.verifyIndex=0;
    }
    return true;
  }
  return false;
}
bool serviceEvidence(){
  if(recoveryVerifying)return false;
  for(auto it=events.begin();it!=events.end();++it){
    if(!it->verifying)continue;
    if(verifying){
      if(it->boot!=verifyingBoot||it->sequence!=verifyingSequence)continue;
      verifier.step();if(!verifier.done())return true;
      if(!verifier.good()){it->stage=3;it->verifyIndex=it->clips.size();}
      else{it->clips[it->verifyIndex].verified=true;++it->verifyIndex;}
      verifying=false;
    }
    if(it->verifyIndex<it->clips.size()){
      const Clip& c=it->clips[it->verifyIndex];
      if(c.good&&!c.legacy&&!c.recovered&&sealed(c)&&isPinned(c)&&c.end>c.start&&c.end-c.start<=120000&&
         verifier.begin(videoPath(c),c.frames,uint32_t(c.end-c.start),c.bytes,c.payload)){
        verifying=true;verifyingBoot=it->boot;verifyingSequence=it->sequence;return true;
      }
      it->stage=3;it->verifyIndex=it->clips.size();
    }
    it->stage=it->stage!=3&&verifyEvent(*it)?2:3;
    if(!saveEvent(*it)){failStorage("manifest finalization failed");return true;}
    emitLog(it->stage==2?"evidence_protected":"evidence_failed","",it->stage==2?"Full coverage and file integrity verified":"Incomplete or damaged coverage; partial evidence retained",it->boot,it->sequence);
    reply(*it);events.erase(it);publish();return true;
  }
  return false;
}
void acceptCommand(const Command& c) {
  if(downloadTransfer.active())endDownload(false);
  if(!c.boot || !c.sequence) return;
  Event e;e.boot=c.boot;e.sequence=c.sequence;e.test=c.test;e.session=session;
  e.path="/system/events/e_"+String(c.boot)+"_"+String(c.sequence)+".json";
  const String legacy="/events/e_"+String(c.boot)+"_"+String(c.sequence)+".json";
  if(SD_MMC.exists(legacy) || SD_MMC.exists(legacy+".tmp"))e.path=legacy;
  for(const auto& active:events) if(active.path==e.path) {reply(active);return;}
  if(!sd || !running || !c.valid || events.size()>=MAX_EVENTS) {e.stage=3;emitLog("lock_rejected","","Recorder/SD unavailable, incompatible command or event limit",c.boot,c.sequence);reply(e);return;}
  if(SD_MMC.exists(e.path) || SD_MMC.exists(e.path+".tmp")) {
    JsonDocument d;
    if(loadJson(e.path,d) && d["schema"]==1 && d["boot"]==c.boot &&
       d["sequence"]==c.sequence && d["stage"].is<uint8_t>()) {
      e.stage=d["stage"];const bool authentic=eventSealed(d);
      if(e.stage==2) {
        e.session=d["session"]|uint64_t(0);
        e.margin=d["transportMarginMs"]|LINK_MARGIN_MS;
        e.window={d["start"]|uint64_t(0),d["end"]|uint64_t(0),
                  d["trigger"]|uint64_t(0),d["triggerTimeKnown"]|false,
                  d["prePossible"]|false};
        bool valid=d["clips"].is<JsonArray>() && d["clips"].size()<=16;
        for(JsonObjectConst o:d["clips"].as<JsonArrayConst>()) {
          Clip x;if(!clipFromJson(o,x))valid=false;else e.clips.push_back(x);
        }
        if(valid&&authentic){
          std::sort(e.clips.begin(),e.clips.end(),[](const Clip&a,const Clip&b){return a.first<b.first;});
          e.stage=1;e.verifying=true;e.verifyIndex=0;events.push_back(e);reply(e);return;
        }
        d["stage"]=3;if(!saveJson(e.path,d)){failStorage("saved evidence verification failed");return;}e.stage=3;
      }
      // Pending state cannot resume across camera reboot: report incomplete,
      // retain all pins, never restart an old incident with a new trigger.
      if(e.stage==1) {d["stage"]=3; if(!saveJson(e.path,d)) {failStorage("reboot journal failure");return;} e.stage=3;}
      if(e.stage==2 || e.stage==3) {reply(e);return;}
    }
    e.stage=3;reply(e);return;
  }
  const uint64_t now=clockMs();
  const uint64_t age=uint64_t(c.age)+(now-c.received);
  // Main generated the elapsed age somewhere inside this measured HTTP
  // round trip. Cover that entire uncertainty interval, with a 2s minimum
  // allowance, instead of assuming an unmeasured one-way delivery delay.
  e.margin=std::max(LINK_MARGIN_MS,c.requestMs);
  e.window=EvidencePolicy::window(now,age,c.known,PRE_MS,POST_MS,e.margin);
  std::vector<Clip> candidates;
  auto consider=[&](const Clip& clip){
    if(clip.session!=session||!clip.good||!EvidencePolicy::overlaps(clip.start,clip.end,e.window))return;
    for(const auto& old:candidates)if(old.stem==clip.stem)return;
    if(candidates.size()<16)candidates.push_back(clip);
  };
  for(const auto& clip:history)consider(clip);
  for(const auto& clip:recent)consider(clip);
  for(const auto& active:events)for(const auto& clip:active.clips)consider(clip);
  std::sort(candidates.begin(),candidates.end(),[](const Clip&a,const Clip&b){return a.start<b.start;});
  for(const auto& x:candidates) if(x.session==session && EvidencePolicy::overlaps(x.start,x.end,e.window))
    if(!addClip(e,x)) {failStorage("cannot pin pre-footage");return;}
  if(EvidencePolicy::overlaps(current.start,now,e.window) && !addClip(e,current))
    {failStorage("cannot pin current footage");return;}
  if(!saveEvent(e)) {failStorage("cannot save lock intent");return;}
  e.catalogPending=recentTruncated&&e.window.known&&!recent.empty()&&e.window.start<recent.front().start;
  events.push_back(e);publish();emitLog("lock_received","","Lock intent persisted; collecting requested pre/post footage",e.boot,e.sequence);reply(e);
  Serial.printf("[CAM] LOCK %lu/%lu persisted, triggerKnown=%d prePossible=%d\n",
    (unsigned long)c.boot,(unsigned long)c.sequence,e.window.known,e.window.prePossible);
  // A delayed command may refer entirely to already closed footage.
  if(!recent.empty()){Clip saved=current;current.end=recent.back().end;finishEvents();current=saved;}
}
CameraAuth::Exchange authenticated{};
uint32_t authenticatedAt=0;
int sessionCode=-3;
bool readReply(HTTPClient& http,String& output) {
  const int size=http.getSize();if(size<0||size>4096||!output.reserve(size))return false;
  auto* stream=http.getStreamPtr();if(!stream)return false;
  const uint32_t began=millis();uint8_t bytes[128];
  while(output.length()<unsigned(size)&&uint32_t(millis()-began)<HTTP_READ_TIMEOUT_MS){
    const int available=stream->available();
    if(available>0){const size_t count=std::min(size_t(available),std::min(sizeof(bytes),size_t(size)-output.length()));
      const int n=stream->read(bytes,count);if(n<=0)return false;
      if(!output.concat(reinterpret_cast<const char*>(bytes),size_t(n)))return false;
    }else if(!stream->connected())break;else vTaskDelay(pdMS_TO_TICKS(1));
  }
  return output.length()==unsigned(size);
}
void clientNonce(){uint8_t bytes[16];for(size_t i=0;i<16;i+=4){const uint32_t value=esp_random();memcpy(bytes+i,&value,4);}CameraCrypto::hex(bytes,16,authenticated.client);authenticated.session[0]=0;authenticated.sequence=0;}
#include "FileLibraryWorker.h"
#include "DownloadRecorder.h"

bool establishSession() {
  sessionCode=-3;
  if(!authenticated.client[0])clientNonce();
  char signature[65];if(!CameraAuth::hello(TOKEN,ROLE,authenticated.client,signature))return false;
  WiFiClient client;HTTPClient http;
  if(!http.begin(client,String(MAIN_URL)+"/api/v1/cameras/session"))return false;
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);http.setTimeout(HTTP_READ_TIMEOUT_MS);http.setReuse(false);
  http.addHeader("X-SV-Camera",ROLE);http.addHeader("X-SV-Client",authenticated.client);http.addHeader("X-SV-MAC",signature);
  const int code=http.GET();sessionCode=code;String text;const bool read=code==200&&readReply(http,text);http.end();
  JsonDocument doc;if(!read||deserializeJson(doc,text)||doc["apiVersion"]!=2||doc["id"].as<String>()!=ROLE||doc["client"].as<String>()!=authenticated.client)return false;
  const char* session=doc["session"]|"";const char* proof=doc["proof"]|"";
  if(!CameraCrypto::validHex(session,32))return false;
  CameraAuth::Exchange next=authenticated;memcpy(next.session,session,33);next.sequence=0;
  if(!CameraAuth::proof(TOKEN,ROLE,next,signature)||!CameraCrypto::equal(signature,proof,64))return false;
  authenticated=next;authenticatedAt=millis();return true;
}
int request(const char* path,JsonDocument* body,JsonDocument* response) {
  if(authenticated.session[0]&&(uint32_t(millis()-authenticatedAt)>=CameraAuth::REKEY_MS||authenticated.sequence==UINT32_MAX))clientNonce();
  if(!authenticated.session[0]&&!establishSession())return sessionCode==200?-3:sessionCode;
  String payload;if(body){if(body->overflowed()||measureJson(*body)>4096)return -2;serializeJson(*body,payload);}
  ++authenticated.sequence;const char* method=body?"POST":"GET";char signature[65],sequence[11];
  if(!CameraAuth::message(TOKEN,ROLE,authenticated,method,path,payload.c_str(),payload.length(),0,signature))return -3;
  snprintf(sequence,sizeof(sequence),"%lu",(unsigned long)authenticated.sequence);
  WiFiClient client;HTTPClient http;
  if(!http.begin(client,String(MAIN_URL)+path))return -1;
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);http.setTimeout(HTTP_READ_TIMEOUT_MS);http.setReuse(false);
  const char* collected[]={"X-SV-MAC"};http.collectHeaders(collected,1);
  http.addHeader("X-SV-Camera",ROLE);http.addHeader("X-SV-Client",authenticated.client);http.addHeader("X-SV-Session",authenticated.session);
  http.addHeader("X-SV-Seq",sequence);http.addHeader("X-SV-MAC",signature);
  int code;if(body){http.addHeader("Content-Type","application/json");code=http.POST(payload);}else code=http.GET();
  String text;const bool complete=code>0&&readReply(http,text);
  const bool valid=complete&&CameraAuth::message(TOKEN,ROLE,authenticated,method,path,text.c_str(),text.length(),code,signature)&&CameraCrypto::equal(signature,http.header("X-SV-MAC").c_str(),64);
  http.end();
  if(!valid){
    // A lost reply does not invalidate our key/session. Sequence numbers may
    // skip safely; Main will return 401 if it rebooted or forgot the session.
    // Error codes affect pacing only. Unverified bodies never become commands.
    if(code<=0)return code;
    if(!complete)return -11;
    if(code==429||code>=500)return code;
    authenticated.session[0]=0;
    portENTER_CRITICAL(&snapshotMux);downloadControlStop=true;portEXIT_CRITICAL(&snapshotMux);
    return code==401||code==403?code:-3;
  }
  if(code==200&&response&&deserializeJson(*response,text))return -2;
  return code;
}
// Event callbacks run on the Wi-Fi event task. Only copy a small notice;
// the network task owns all connection changes, HTTP and diagnostic output.
struct WifiNotice {uint32_t at; uint8_t kind,reason;};
void wifiEvent(WiFiEvent_t event,WiFiEventInfo_t info) {
  WifiNotice n{millis(),0,0};
  if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){n.kind=1;n.reason=info.wifi_sta_disconnected.reason;}
  else if(event==ARDUINO_EVENT_WIFI_STA_GOT_IP)n.kind=2;
  else if(event==ARDUINO_EVENT_WIFI_STA_LOST_IP)n.kind=3;
  if(n.kind && wifiNotices)xQueueSend(wifiNotices,&n,0);
}
const char* reasonName(uint8_t r){
  switch(r){case 8:return "ASSOC_LEAVE";case 200:return "BEACON_TIMEOUT";
    case 201:return "NO_AP_FOUND";case 202:return "AUTH_FAIL";
    case 203:return "ASSOC_FAIL";case 204:return "HANDSHAKE_TIMEOUT";default:return "OTHER";}
}
bool wifiReady(){return WiFi.status()==WL_CONNECTED && uint32_t(WiFi.localIP())!=0;}
void networkTask(void*) {
  WiFi.onEvent(wifiEvent);
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(false);
  CameraLink::Retry link(WIFI_JOIN_TIMEOUT_MS,WIFI_RETRY_INITIAL_MS,WIFI_RETRY_MAX_MS);
  const uint32_t phase=strcmp(ROLE,"rear")==0?350:0;
  CameraHttp::Session httpSession(HEARTBEAT_MS,HEARTBEAT_RETRY_MS,POLL_MS,phase);
  uint32_t now=millis(),nextDiagnostic=now+DIAGNOSTIC_MS;
  uint32_t lastHeartbeatOk=0,disconnects=0,joins=0,httpErrors=0;
  uint8_t lastReason=0;
  int heartbeatCode=0,pollCode=0;
  bool haveHeartbeat=false,haveAck=false,driverFirstRetryAvailable=true,driverRetryActive=false;
  uint32_t sentStatusRevision=0;
  bool fileReplyTurn=false;
  Ack pending{};link.start(now,phase);
  for(;;){
    now=millis();WifiNotice n;bool gotIpNotice=false;
    while(xQueueReceive(wifiNotices,&n,0)==pdTRUE){
      if(n.kind==1){
        ++disconnects;lastReason=n.reason;char reason[80];snprintf(reason,sizeof(reason),"reason=%u %s",n.reason,reasonName(n.reason));emitLog("wifi_disconnected","",reason);
        Serial.printf("[WIFI] role=%s atMs=%lu disconnected reason=%u %s\n",ROLE,(unsigned long)n.at,n.reason,reasonName(n.reason));
        // Arduino ESP32 3.3.12 performs one initial fallback even with
        // autoReconnect disabled. Let that in-flight attempt finish.
        if(n.reason!=8 && driverFirstRetryAvailable){
          driverFirstRetryAvailable=false;driverRetryActive=true;link.awaitDriverRetry(now);
          Serial.println("[WIFI] allowing core's first fallback attempt");
        }else if(!(driverRetryActive && n.reason==8)){
          driverRetryActive=false;link.failed(now,esp_random()%501);
        }
      }else if(n.kind==2){
        driverRetryActive=false;emitLog("wifi_connected","","Associated with main AP and obtained an IP address");
        Serial.printf("[WIFI] role=%s gotIP=%s RSSI=%d channel=%d\n",ROLE,WiFi.localIP().toString().c_str(),WiFi.RSSI(),int(WiFi.channel()));
        gotIpNotice=true;
      }else Serial.printf("[WIFI] role=%s lost IP atMs=%lu\n",ROLE,(unsigned long)n.at);
    }
    const bool up=wifiReady();
    // Observe live state as well as events, so a dropped queue notice cannot
    // prevent recovery. A new lease also refreshes HTTP after a rapid rejoin.
    if(httpSession.observe(now,up,gotIpNotice)){
      haveHeartbeat=false;heartbeatCode=0;pollCode=0;
      Serial.println("[LINK] IP ready; scheduling fresh camera heartbeat");
    }
    const auto action=link.tick(now,up,esp_random()%501);
    if(action==CameraLink::Action::Begin){++joins;Serial.printf("[WIFI] join attempt=%lu\n",(unsigned long)joins);WiFi.begin(SSID,PASSWORD);}
    else if(action==CameraLink::Action::Cancel){driverRetryActive=false;Serial.println("[WIFI] join timed out after 30s");WiFi.disconnect();}
    // Never reset/rejoin the radio in response to an HTTP failure.
    // One request per iteration; first heartbeat on join, then bounded ACK
    // bursts with fair heartbeat/command service and shared error cooldown.
    if(!up){portENTER_CRITICAL(&snapshotMux);downloadControlStop=true;portEXIT_CRITICAL(&snapshotMux);}
    if(up){
      uint32_t currentStatusRevision=0;
      portENTER_CRITICAL(&snapshotMux);currentStatusRevision=snapshot.statusRevision;portEXIT_CRITICAL(&snapshotMux);
      if(currentStatusRevision!=sentStatusRevision)httpSession.statusChanged(now);
      if(!haveAck)haveAck=xQueueReceive(acks,&pending,0)==pdTRUE;
      const auto operation=httpSession.next(now,haveAck);
      if(operation==CameraHttp::Request::Heartbeat){
        Snapshot snap;portENTER_CRITICAL(&snapshotMux);snap=snapshot;portEXIT_CRITICAL(&snapshotMux);
        JsonDocument d;d["firmware"]=FIRMWARE;d["recording"]=snap.recording;
        d["sdReady"]=snap.sd;d["lockSupported"]=true;d["frames"]=snap.frames;
        d["uptimeMs"]=now;d["rssi"]=WiFi.RSSI();d["disconnects"]=disconnects;
        JsonObject t=d["telemetry"].to<JsonObject>();t["schema"]=1;t["stream"]=logStream;t["session"]=snap.session;
        t["uptimeMs"]=clockMs();t["capturedFrames"]=snap.captured;t["currentClip"]=snap.current;t["currentFrames"]=snap.frames;
        t["currentBytes"]=snap.currentBytes;t["frameAgeMs"]=snap.lastFrameMs?clockMs()-snap.lastFrameMs:uint64_t(0);
        t["finalizedThisBoot"]=snap.finalized;t["deletedThisBoot"]=snap.deleted;t["normalRetained"]=snap.normal;t["lockedRetained"]=snap.locked;
        t["filesSupported"]=2;t["downloadsSupported"]=1;t["totalBytes"]=snap.totalBytes;t["latestUtcMs"]=snap.latestUtc;
        t["downloadId"]=snap.downloadId;t["downloadState"]=snap.downloadState;t["downloadSent"]=snap.downloadSent;t["downloadSize"]=snap.downloadSize;
        t["activeLocks"]=snap.activeLocks;t["freeBytesAtBoundary"]=snap.freeBytes;t["lastClip"]=snap.lastClip;
        t["lastFrames"]=snap.lastFrames;t["lastDurationMs"]=snap.lastDuration;t["lastMaxGapMs"]=snap.lastGap;t["fault"]=snap.fault;
        t["rssi"]=WiFi.RSSI();t["disconnects"]=disconnects;t["lastReason"]=lastReason;t["httpErrors"]=httpErrors;
        CameraLogs::Entry entries[6];size_t count;uint32_t lost;
        char clockSourceCopy[24];uint64_t clockBase,clockAt;
        portENTER_CRITICAL(&snapshotMux);count=logBuffer.copy(entries,6);lost=logBuffer.overwritten;clockBase=utcAnchor;clockAt=utcAt;snprintf(clockSourceCopy,sizeof(clockSourceCopy),"%s",utcSource);portEXIT_CRITICAL(&snapshotMux);
        t["logOverwritten"]=lost;t["clockSource"]=clockBase && clockMs()-clockAt<86400000ULL?clockSourceCopy:"unsynced";
        auto logs=d["events"].to<JsonArray>();
        for(size_t i=0;i<count;++i){const auto& e=entries[i];auto o=logs.add<JsonObject>();o["seq"]=e.seq;o["atMs"]=e.at;o["type"]=e.type;o["clip"]=e.clip;o["detail"]=e.detail;o["boot"]=e.boot;o["incident"]=e.incident;}
        const uint64_t heartbeatStart=clockMs();
        JsonDocument result;heartbeatCode=request("/api/v1/cameras/heartbeat",&d,&result);
        const bool matched=heartbeatCode==200 && result["id"].as<String>()==ROLE;
        if(matched){
          lastHeartbeatOk=millis();haveHeartbeat=true;
          sentStatusRevision=snap.statusRevision;
          if(result["logStream"]==logStream && result["logAck"].is<uint32_t>()){
            portENTER_CRITICAL(&snapshotMux);logBuffer.acknowledge(result["logAck"]);portEXIT_CRITICAL(&snapshotMux);
          }
          const uint64_t received=clockMs(),epoch=result["clock"]["utcMs"]|uint64_t(0);
          if(epoch>=1577836800000ULL && epoch<4102444800000ULL && received-heartbeatStart<=4000){
            char kind[24];snprintf(kind,sizeof(kind),"%s",result["clock"]["source"]|"device");
            // Copy fixed source text and anchor together under the snapshot lock.
            bool changed;portENTER_CRITICAL(&snapshotMux);changed=!utcAnchor || strcmp(utcSource,kind)!=0;utcAnchor=epoch+(received-heartbeatStart)/2;utcAt=received;snprintf(utcSource,sizeof(utcSource),"%s",kind);portEXIT_CRITICAL(&snapshotMux);
            const uint64_t utc=epoch+(received-heartbeatStart)/2;
            struct timeval tv{time_t(utc/1000),suseconds_t((utc%1000)*1000)};
            if(settimeofday(&tv,nullptr)!=0)emitLog("file_date_warning","","Could not set filesystem clock");
            if(changed)emitLog("clock_synchronized","",kind);
          }
        }
        else{++httpErrors;Serial.printf("[HTTP] heartbeat code=%d roleMatched=%d\n",heartbeatCode,matched);}
        httpSession.completed(operation,millis(),heartbeatCode,matched,esp_random());
      }else if(operation==CameraHttp::Request::Ack){
        JsonDocument d;d["boot"]=pending.boot;d["sequence"]=pending.sequence;
        d["stage"]=pending.stage==2?"protected":pending.stage==3?"failed":"received";
        const int code=request("/api/v1/cameras/ack",&d,nullptr);
        if(code==200 || code==409)haveAck=false;else{++httpErrors;Serial.printf("[HTTP] ack code=%d\n",code);}
        httpSession.completed(operation,millis(),code,false,esp_random());
      }else if(operation==CameraHttp::Request::Poll){
        FileReply resultPage;bool sendPage=false;
        portENTER_CRITICAL(&snapshotMux);sendPage=fileReplyReady&&!fileReplyTurn;if(sendPage)resultPage=fileReply;portEXIT_CRITICAL(&snapshotMux);
        if(sendPage){
          fileReplyTurn=true;
          JsonDocument result;result["id"]=resultPage.id;result["stream"]=resultPage.stream;result["error"]=resultPage.error;
          result["deleted"]=resultPage.deleted;result["recoveryFilesDeleted"]=resultPage.recoveryDeleted;result["skipped"]=resultPage.skipped;result["more"]=resultPage.more;
          result["downloadReady"]=resultPage.downloadReady;result["downloadSize"]=resultPage.downloadSize;
          auto rows=result["clips"].to<JsonArray>();
          for(unsigned i=0;i<resultPage.count;++i){const auto& row=resultPage.rows[i];auto out=rows.add<JsonObject>();out["name"]=row.name;out["session"]=row.session;out["start"]=row.start;out["utcMs"]=row.utc;out["protectedUtcMs"]=row.protectedUtc;out["protected"]=row.locked;out["protectionReason"]=row.reason;}
          const int code=request("/api/v1/cameras/files/result",&result,nullptr);
          if(code==200||code==409||code==400){portENTER_CRITICAL(&snapshotMux);if(fileReply.id==resultPage.id)fileReplyReady=false;portEXIT_CRITICAL(&snapshotMux);}
          httpSession.completed(operation,millis(),code,false,esp_random());
          delay(1);continue;
        }
        fileReplyTurn=false;
        JsonDocument d;const uint64_t requestStart=clockMs();
        pollCode=request("/api/v1/cameras/commands",nullptr,&d);
        if(pollCode==200&&d["apiVersion"]==1&&d["id"].as<String>()==ROLE&&d["download"]["stream"]==logStream){
          const auto grant=d["download"].as<JsonObjectConst>();const uint32_t elapsed=uint32_t(clockMs()-requestStart),allowed=grant["leaseMs"]|0U;
          portENTER_CRITICAL(&snapshotMux);downloadControlId=grant["id"];downloadControlStop=grant["stop"]==true||allowed<=elapsed+250||allowed>10000;downloadControlUntil=millis()+(allowed>elapsed+250?allowed-elapsed-250:0);portEXIT_CRITICAL(&snapshotMux);
        }
        if(pollCode!=200){++httpErrors;Serial.printf("[HTTP] poll code=%d\n",pollCode);}
        if(pollCode==200 && d["apiVersion"]==1 && d["id"].as<String>()==ROLE && d["command"].is<JsonObject>()){
          auto o=d["command"].as<JsonObject>();
          if(o["boot"].is<uint32_t>() && o["sequence"].is<uint32_t>()){
            Command c{};c.boot=o["boot"];c.sequence=o["sequence"];
            c.received=clockMs();c.known=o["triggerTimeKnown"]==true && o["elapsedSinceDetectionMs"].is<uint32_t>();
            c.requestMs=uint32_t(c.received-requestStart);
            c.age=o["elapsedSinceDetectionMs"]|0U;c.test=o["test"]|false;
            c.valid=o["type"].as<String>()=="LOCK" && o["preClips"]==5 && o["postClips"]==5 && o["segmentSeconds"]==60 && o["normalClips"]==30;
            xQueueSend(commands,&c,0);
          }
        }
        if(pollCode==200&&d["id"].as<String>()==ROLE&&d["files"].is<JsonObject>()){
          const auto f=d["files"].as<JsonObjectConst>();const unsigned op=f["operation"]|0;
          if(f["id"].is<uint32_t>()&&f["stream"]==logStream&&((op>=1&&op<=5)||op==7)&&strlen(f["beforeName"]|"")<=80){
            FileRequest incoming;incoming.id=f["id"];incoming.stream=logStream;incoming.operation=op;incoming.receivedAt=millis();
            incoming.beforeSession=f["beforeSession"]|UINT64_MAX;incoming.beforeStart=f["beforeStart"]|UINT64_MAX;snprintf(incoming.beforeName,sizeof(incoming.beforeName),"%s",f["beforeName"]|"");snprintf(incoming.token,sizeof(incoming.token),"%s",f["token"]|"");
            portENTER_CRITICAL(&snapshotMux);if(!fileRequestReady&&incoming.id!=fileReply.id){fileRequest=incoming;fileRequestReady=true;}portEXIT_CRITICAL(&snapshotMux);
          }
        }
        httpSession.completed(operation,millis(),pollCode,false,esp_random());
      }
    }
    now=millis();
    if(CameraLink::due(now,nextDiagnostic)){
      Snapshot snap;portENTER_CRITICAL(&snapshotMux);snap=snapshot;portEXIT_CRITICAL(&snapshotMux);
      const uint64_t age=snap.lastFrameMs?clockMs()-snap.lastFrameMs:0;
      Serial.printf("[LINK] role=%s uptimeMs=%lu wifi=%s RSSI=%d hbAgeMs=%lu hbOK=%d hbCode=%d pollCode=%d disconnects=%lu lastReason=%u joins=%lu httpErrors=%lu recording=%d frames=%lu frameAgeMs=%llu heap=%u largest=%u\n",
        ROLE,(unsigned long)now,wifiReady()?"up":"down",wifiReady()?WiFi.RSSI():0,
        (unsigned long)(haveHeartbeat?now-lastHeartbeatOk:now),haveHeartbeat,heartbeatCode,pollCode,
        (unsigned long)disconnects,lastReason,(unsigned long)joins,(unsigned long)httpErrors,
        snap.recording,(unsigned long)snap.frames,(unsigned long long)age,
        (unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      nextDiagnostic=now+DIAGNOSTIC_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
bool initCamera();
bool startServices() {
  commands=xQueueCreate(4,sizeof(Command));acks=xQueueCreate(16,sizeof(Ack));
  wifiNotices=xQueueCreate(16,sizeof(WifiNotice));
  if(commands&&acks&&wifiNotices){
    camera=initCamera();
    if(xTaskCreatePinnedToCore(networkTask,"camera_link",8192,nullptr,1,nullptr,0)==pdPASS){
      servicesReady=true;return true;
    }
    if(camera){esp_camera_deinit();camera=false;}
  }
  // No recorder or radio task owns these queues until startup succeeds.
  for(QueueHandle_t* queue:{&commands,&acks,&wifiNotices})if(*queue){vQueueDelete(*queue);*queue=nullptr;}
  startupRetryAt=millis()+1000;lastFault="Camera service allocation failed; retry scheduled";
  emitLog("startup_error","",lastFault.c_str());publish();
  Serial.println("[CAM] Service allocation failed; retry in 1s.");return false;
}
bool initCamera() {
  if(!psramFound()) {Serial.println("[CAM] PSRAM required.");return false;}
  camera_config_t c{};
  c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
  // Espressif AI Thinker layout, independent of sensor type. User must confirm.
  c.pin_d0=5;c.pin_d1=18;c.pin_d2=19;c.pin_d3=21;
  c.pin_d4=36;c.pin_d5=39;c.pin_d6=34;c.pin_d7=35;
  c.pin_xclk=0;c.pin_pclk=22;c.pin_vsync=25;c.pin_href=23;
  c.pin_sccb_sda=26;c.pin_sccb_scl=27;c.pin_pwdn=32;c.pin_reset=-1;
  c.xclk_freq_hz=20000000;c.pixel_format=PIXFORMAT_JPEG;
  c.frame_size=FRAMESIZE_VGA;c.jpeg_quality=12;c.fb_count=2;
  c.fb_location=CAMERA_FB_IN_PSRAM;c.grab_mode=CAMERA_GRAB_LATEST;
  esp_err_t err=esp_camera_init(&c);
  if(err!=ESP_OK) {Serial.printf("[CAM] init error 0x%x\n",err);return false;}
  sensor_t* s=esp_camera_sensor_get();
  Serial.printf("[CAM] sensor PID 0x%04x (OV3660 expected 0x3660)\n",s?s->id.PID:0);
  if(!s || s->id.PID!=0x3660) {esp_camera_deinit();return false;}
  // Apply at every initialization, including recovery, before recording starts.
  // Sensor-side correction also makes the JPEG frames inside each AVI upright.
  if(!s->set_vflip || s->set_vflip(s,VERTICAL_FLIP?1:0)!=0) {
    Serial.println("[CAM] Vertical orientation setup failed; retry required.");
    esp_camera_deinit();return false;
  }
  Serial.printf("[CAM] role=%s verticalFlip=%d\n",ROLE,VERTICAL_FLIP?1:0);
  return true;
}
}
void CameraApp::begin() {
  Serial.begin(115200);
  // POSIX TZ signs are reversed: Philippine UTC+08 is UTC-08.
  char timezone[24];const int offset=NAME_UTC_OFFSET_MINUTES;
  snprintf(timezone,sizeof(timezone),"UTC%s%02d:%02d",offset>=0?"-":"+",abs(offset)/60,abs(offset)%60);
  setenv("TZ",timezone,1);tzset();
  logStream=esp_random();if(!logStream)logStream=1;
  const auto reset=esp_reset_reason();
  Serial.printf("[BOOT] firmware=%s reset=%d%s\n",FIRMWARE,int(reset),reset==ESP_RST_BROWNOUT?" BROWNOUT":"");
  if(!BOARD_LAYOUT_CONFIRMED || strlen(TOKEN)!=64 ||
     (strcmp(ROLE,"front") && strcmp(ROLE,"rear"))) {
    Serial.println("[CAM] Configure role/token and confirm board layout first.");return;
  }
  for(const char* p=TOKEN;*p;++p)if(!isxdigit(*p))return;
  startupAllowed=true;if(!startServices())return;
  char startup[80];snprintf(startup,sizeof(startup),"Firmware %s; reset reason=%d",FIRMWARE,int(reset));emitLog("camera_boot","",startup);
  if(!camera){lastFault="Camera initialization failed";emitLog("recording_error","",lastFault.c_str());}
  // Start networking before mounting/scanning SD. Mount/recovery runs only in
  // the recorder task so no-card boot still sends camera heartbeats.
  if(camera)lastFault="SD not mounted; automatic mount retry active";
  publish();
  Serial.printf("[CAM] role=%s session=%llu camera=%d sd=%d recording=%d; 's' closes recording for card removal.\n",ROLE,(unsigned long long)session,camera,sd,running);
}
void CameraApp::tick() {
  if(!servicesReady){
    if(!startupAllowed||int32_t(millis()-startupRetryAt)<0||!startServices()){delay(10);return;}
    cameraRetryAt=millis();lastFault="";publish();
  }
  Command c;if(xQueueReceive(commands,&c,0)==pdTRUE)acceptCommand(c);
  if(Serial.available() && Serial.read()=='s') {
    if(downloadTransfer.active()){downloadResume=false;endDownload(false);}
    if(running && !closeClip())failStorage("stop finalization failed");
    for(auto& e:events){e.stage=3;saveEvent(e);reply(e);}events.clear();
    paused=true;running=false;verifier.cancel();verifying=false;recovery.cancel();recoveryDirectory.close();recoveryVerifying=false;emitLog("recording_stopped","","Clean stop requested; power off before removing SD");publish();Serial.println("[CAM] STOPPED: power off before removing SD.");
  }
  if(!camera&&!paused&&!downloadTransfer.active()&&int32_t(millis()-cameraRetryAt)>=0){
    esp_camera_deinit();camera=initCamera();
    if(camera){cameraBackoff=1000;lastFault="";if(sd&&!openClip())failStorage("Camera recovered but recording could not restart");emitLog("camera_recovered","","Camera initialized; recording resumes in a new session");}
    else{cameraRetryAt=millis()+cameraBackoff;cameraBackoff=std::min<uint32_t>(30000,cameraBackoff*2);lastFault="Camera unavailable; automatic recovery active";}
    publish();
  }
  serviceStorage();
  if(serviceDownload()){delay(1);return;}
  if(!running || !sd || paused) {serviceLibrary();delay(10);return;}
  uint64_t now=clockMs();
  if(now-current.start>=SEGMENT_MS) {
    if(!closeClip()) {failStorage("AVI finalization failed");return;}
    finishEvents();
    if(!sd || !trim() || !openClip()) {if(sd)failStorage("FIFO/free-space/new clip failed");return;}
  }
  now=clockMs();
  if(now>=nextFrame) {
    nextFrame=now+1000/FPS;
    camera_fb_t* f=esp_camera_fb_get();
    if(!f){camera=false;cameraRetryAt=millis()+cameraBackoff;failStorage("Camera capture unavailable; automatic recovery active");esp_camera_deinit();return;}
    // Driver timestamp is the first DMA buffer's boot-relative exposure time,
    // not the later SD-write time. Reject stale/nonmonotonic buffered frames.
    const uint64_t captured=uint64_t(f->timestamp.tv_sec)*1000ULL+
                            uint64_t(f->timestamp.tv_usec)/1000ULL;
    const uint64_t received=clockMs();
    const bool fresh=captured>=current.start&&captured<=received&&received-captured<=MAX_FRAME_GAP_MS&&(!current.frames||captured>current.last);
    if(!fresh){esp_camera_fb_return(f);if(received-(current.last?current.last:current.start)>2000){camera=false;cameraRetryAt=millis()+cameraBackoff;failStorage("Camera frames stalled; automatic recovery active");esp_camera_deinit();}return;}
    if(f->format!=PIXFORMAT_JPEG||f->len<4||f->buf[0]!=0xff||f->buf[1]!=0xd8||f->buf[f->len-2]!=0xff||f->buf[f->len-1]!=0xd9){esp_camera_fb_return(f);camera=false;cameraRetryAt=millis()+cameraBackoff;failStorage("Camera image invalid; automatic recovery active");esp_camera_deinit();return;}
    if(freeAtBoundary<2ULL*1024*1024+f->len&&!spaceFor(FREE_RESERVE_BYTES)){esp_camera_fb_return(f);failStorage("No reclaimable recording space; protected evidence retained");return;}
    const size_t written=f->len+8+(f->len&1);const bool ok=writer.frame(f->buf,f->len);esp_camera_fb_return(f);
    if(!ok||writer.size()>MAX_AVI_BYTES){failStorage("frame write/size failed");return;}
    freeAtBoundary=freeAtBoundary>written?freeAtBoundary-written:0;
    if(!current.frames)current.first=captured;
    if(current.frames)current.gap=std::max(current.gap,uint32_t(captured-current.last));
    current.last=captured;current.frames=writer.count();++capturedCount;publish();
  }
  now=clockMs();
  if(now+12<nextFrame){
    if(uint32_t(millis()-checkpointAt)>=1000){if(!checkpoint()){failStorage("Recording checkpoint failed");return;}}
    else {
      if(!serviceEvidence()&&sd&&!serviceCatalog()&&sd)serviceRecovery();
      // One catalog entry per turn, even during lengthy evidence verification.
      // The worker itself refuses deletion/download while protection is active.
      if(sd)serviceLibrary();
    }
  }
  delay(1);
}
