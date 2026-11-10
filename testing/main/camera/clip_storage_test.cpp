#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/ClipStorage.h"
#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/AviWriter.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
static void put(const String& path,const char* data="L") {
  std::ofstream f(SD_MMC.path(path),std::ios::binary);f<<data;assert(f.good());
}
int main(int argc,char** argv) {
  assert(argc==2);SD_MMC.root=argv[1];
  for(const char* dir:{"/clips","/system/clips","/system/recording"})fs::create_directories(SD_MMC.root+dir);
  char name[81];
  for(const char* role:{"front","rear"}) {
    ClipNames::make(name,sizeof(name),role,53,1,1790908414000ULL,480);
    String stem=name;assert(ClipNames::validStem(name));
    assert(stem==String("/clips/")+role+"_2026-10-02_10-33-34");
    put(stem+".avi","video");
    assert(ClipStorage::pin(stem,stem));
    assert(SD_MMC.exists(ClipStorage::incident(stem)) && !SD_MMC.exists(stem+".avi"));
    assert(!SD_MMC.exists(ClipStorage::pinPath(stem))); // no permanent .lock
    assert(ClipStorage::protectedClip(stem,stem));
    const String timePath=ClipStorage::internal(stem)+".protected";
    File timestamp=SD_MMC.open(timePath,FILE_READ);uint64_t protectedAt=0;
    assert(timestamp&&timestamp.size()==sizeof(protectedAt));
    assert(timestamp.read(reinterpret_cast<uint8_t*>(&protectedAt),sizeof(protectedAt))==sizeof(protectedAt));timestamp.close();
    assert(protectedAt>=1577836800000ULL&&protectedAt<4102444800000ULL);
    assert(ClipStorage::pin(stem,stem)); // overlapping incidents are idempotent
    String duplicate=stem;assert(ClipStorage::choose(duplicate));assert(duplicate==stem+"_2");
    put(ClipStorage::internal(duplicate,"recording")+".part","interrupted");
    duplicate=stem;assert(ClipStorage::choose(duplicate));assert(duplicate==stem+"_3");
  }
  // Old LOCKED files stay readable; classification-based migration is a
  // rename only and does not overwrite a conflicting protected destination.
  const String recovered="/clips/rear_recovered_test";put(recovered+"_LOCKED.avi","original recovered bytes");
  assert(ClipStorage::protectedClip(recovered,recovered));
  assert(ClipStorage::labelProtected(recovered,true));assert(ClipStorage::video(recovered)==ClipStorage::interruption(recovered));
  assert(ClipStorage::pin(recovered,recovered));assert(!SD_MMC.exists(ClipStorage::incident(recovered)));
  String parsed;bool protectedFile=false;
  assert(ClipStorage::splitVideo(ClipStorage::interruption(recovered),parsed,protectedFile)&&parsed==recovered&&protectedFile);
  assert(ClipStorage::splitVideo(ClipStorage::incident(recovered),parsed,protectedFile)&&protectedFile);
  assert(ClipStorage::splitVideo(recovered+"_LOCKED.avi",parsed,protectedFile)&&protectedFile);
  assert(ClipStorage::splitVideo(recovered+".avi",parsed,protectedFile)&&!protectedFile);
  put(recovered+"_LOCKED.avi","conflict retained");assert(!ClipStorage::labelProtected(recovered,true));
  assert(SD_MMC.exists(recovered+"_LOCKED.avi")&&SD_MMC.exists(ClipStorage::interruption(recovered)));
  const String interruptedRename="/clips/front_recovered_failure";put(interruptedRename+"_LOCKED.avi","retained");
  SD_MMC.failRename=true;assert(!ClipStorage::labelProtected(interruptedRename,true));assert(ClipStorage::protectedClip(interruptedRename,interruptedRename));
  SD_MMC.failRename=false;assert(ClipStorage::labelProtected(interruptedRename,true));
  // Pending recordings retain a small durable pin; a later dated video uses
  // the same internal identity and is protected without rewriting old events.
  const String identity="/clips/front_time-unknown_session-53_clip-1";
  const String dated="/clips/front_2026-10-02_10-34-34";
  assert(ClipStorage::pin(identity,identity));
  assert(SD_MMC.exists(ClipStorage::pinPath(identity)));
  put(dated+"_LOCKED.avi","video");
  assert(ClipStorage::pin(identity,dated));
  assert(!SD_MMC.exists(ClipStorage::pinPath(identity)));
  assert(ClipStorage::protectedClip(identity,dated));
  // Failed rename must keep the original AVI and durable pin.
  const String failed="/clips/rear_2026-10-02_11-00-00";
  put(failed+".avi","original");SD_MMC.failRename=true;
  assert(!ClipStorage::pin(failed,failed));
  assert(SD_MMC.exists(failed+".avi") && ClipStorage::protectedClip(failed,failed));
  SD_MMC.failRename=false;assert(ClipStorage::pin(failed,failed));
  assert(SD_MMC.exists(ClipStorage::incident(failed)));
  // Legacy pins remain honored; corrupt markers block deletion but do not
  // qualify as a successful new pin operation.
  const String legacy="/clips/front_20261002_103334_s53_c1";
  put(legacy+".avi","legacy");put(legacy+".lock");
  put(legacy+".json","metadata");put(legacy+".idx","old recovery index");put(legacy+".part","interrupted footage");
  for(const char* ext:{".lock",".json",".idx",".part"})assert(ClipStorage::migrateSidecar(legacy+ext));
  assert(SD_MMC.exists(ClipStorage::metadata(legacy)));
  assert(SD_MMC.exists(ClipStorage::internal(legacy,"recording")+".part"));
  assert(SD_MMC.exists(ClipStorage::internal(legacy,"recording")+".idx"));
  assert(SD_MMC.exists(legacy+".avi"));
  put(legacy+".json","conflicting metadata");
  assert(!ClipStorage::migrateSidecar(legacy+".json"));assert(SD_MMC.exists(legacy+".json"));
  assert(!ClipStorage::legacyDestination("/clips/holiday.json").length());
  assert(!ClipStorage::legacyDestination("/clips/front_../../data.json").length());
  assert(ClipStorage::pin(legacy,legacy));
  assert(SD_MMC.exists(ClipStorage::incident(legacy)) && !SD_MMC.exists(legacy+".lock"));
  const String corrupt="/clips/c_54_1";
  put(ClipStorage::pinPath(corrupt),"?");
  assert(ClipStorage::protectedClip(corrupt,corrupt));assert(!ClipStorage::pin(corrupt,corrupt));
  // Unknown names explain counters, and late time is derived from monotonic
  // elapsed time rather than assigning synchronization time to clip start.
  ClipNames::make(name,sizeof(name),"rear",53,1,0,480);
  assert(!strcmp(name,"/clips/rear_time-unknown_session-53_clip-1"));
  const uint64_t epoch=1790908414000ULL;
  assert(ClipNames::startUtc(epoch,30000,60000,1000)==epoch-29000);
  assert(ClipNames::startUtc(epoch,30000,60000,40000)==epoch+10000);
  assert(ClipNames::startUtc(epoch,30000,29999,0)==0);
  assert(ClipNames::startUtc(epoch,30000,30000+86400000ULL,0)==0);
  assert(ClipNames::startUtc(0,0,1000,0)==0);
  assert(ClipNames::startUtc(epoch,0,0,1)==0);
  assert(ClipNames::startUtc(epoch,4294967296ULL,4294968296ULL,4294966296ULL)==epoch-1000);
  assert(!ClipNames::validStem("/clips/front_../../outside"));
  assert(!ClipNames::validStem("/clips/front_name/other"));
  assert(!ClipNames::validStem("/clips/front_x.avi"));
  // Index bounds, scratch-file collisions and destination collisions must
  // fail closed, retaining recoverable data without overwriting any AVI.
  const uint8_t jpeg[]={0xff,0xd8,0xff,0xd9};
  AviWriter writer;
  const String work="/system/recording/front_test";
  assert(writer.begin(work,640,480));
  for(int n=0;n<1024;++n)assert(writer.frame(jpeg,sizeof(jpeg)));
  assert(!writer.frame(jpeg,sizeof(jpeg)));assert(!writer.close(60000));
  assert(SD_MMC.exists(work+".part") && !SD_MMC.exists(work+".idx"));
  assert(!writer.begin(work,640,480));
  assert(writer.begin(work+"_2",640,480));assert(writer.frame(jpeg,sizeof(jpeg)));
  put("/clips/front_existing.avi","do not overwrite");
  assert(!writer.close(125,"/clips/front_existing.avi"));
  std::ifstream preserved(SD_MMC.path("/clips/front_existing.avi"));
  std::string bytes((std::istreambuf_iterator<char>(preserved)),{});assert(bytes=="do not overwrite");
  assert(SD_MMC.exists(work+"_2.part"));
  std::cout<<"PASS: both roles, readable names, clock/backdating, collision safety, pending/finalized locks, rename failure, legacy/corrupt pins, bounded index and overwrite protection\n";
}
