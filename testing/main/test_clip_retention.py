"""Run the actual recorder FIFO function with real files and storage helpers."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
app=(root/'SmartVest_SJKS01_Camera/src/CameraApp.cpp').read_text(encoding='utf8')
paths=app[app.index('String videoStem('):app.index('bool pin(const Clip&')]
trim=app[app.index('bool deleteOldest()'):app.index('bool checkpoint()')]
prefix=r'''
#include "ClipStorage.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>
struct Clip {String stem,videoStem;bool good=true;};
std::vector<Clip> history,recent;String storageWarning;uint64_t freeAtBoundary=0;
constexpr size_t NORMAL_CLIPS=30;constexpr uint64_t FREE_RESERVE_BYTES=64ULL*1024*1024;
unsigned normalCount=32,lockedCount=3,deletedCount=0;
void emitLog(const char*,const String&,const char*){}
void put(const String& path,const char* data="video"){
 std::ofstream file(SD_MMC.path(path),std::ios::binary);file<<data;assert(file.good());file.close();if(path.endsWith(".avi"))std::filesystem::resize_file(SD_MMC.path(path),4ULL*1024*1024);
}
'''
checks=r'''
int main(int argc,char** argv){
 assert(argc==2);SD_MMC.root=argv[1];
 std::filesystem::create_directories(SD_MMC.root+"/clips");
 std::filesystem::create_directories(SD_MMC.root+"/system/clips");
 std::vector<Clip> all;
 for(unsigned i=0;i<35;++i){
  Clip c;c.stem=String("/clips/front_queue_")+String(i);c.videoStem=c.stem;
  if(i==0)c.videoStem="/clips/front_2026-10-10_14-35-00"; // late-clock alias
  put(c.videoStem+(i<2?"_LOCKED.avi":".avi"));
  if(i==2)put(ClipStorage::pinPath(c.stem),"?"); // even a corrupt pin prevents FIFO deletion
  put(ClipStorage::metadata(c.stem),"metadata");
  history.push_back(c);all.push_back(c);
 }
 assert(trim());assert(history.size()==30 && normalCount==30 && deletedCount==2 && lockedCount==3);
 for(unsigned i=0;i<35;++i){
  const bool retained=i<3 || i>=5;
  assert(SD_MMC.exists(videoPath(all[i]))==retained);
  assert(SD_MMC.exists(ClipStorage::metadata(all[i].stem))==retained);
 }
 assert(trim());assert(deletedCount==2); // repeated cleanup is stable
 SD_MMC.freeBytes=62ULL*1024*1024;assert(spaceFor(FREE_RESERVE_BYTES));assert(history.size()==29&&deletedCount==3);
 put("/clips/front_unknown.avi");SD_MMC.freeBytes=0;
 assert(spaceFor(FREE_RESERVE_BYTES));assert(SD_MMC.exists("/clips/front_unknown.avi"));
 for(unsigned i=0;i<3;++i)assert(SD_MMC.exists(videoPath(all[i])));
 SD_MMC.freeBytes=0;assert(!spaceFor(512ULL*1024*1024));assert(history.empty());
 for(unsigned i=0;i<3;++i)assert(SD_MMC.exists(videoPath(all[i])));
 assert(SD_MMC.exists("/clips/front_unknown.avi"));
 std::cout<<"PASS low-space reclaim below count limit, repeated reclaim, all-normal exhaustion, protected/unknown retention\n";
 std::cout<<"PASS actual FIFO: keep 30 normal clips, preserve LOCKED aliases and corrupt pins, delete matching metadata only, repeated cleanup stable\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    directory=Path(tmp)
    source=directory/'retention.cpp';source.write_text(prefix+paths+'\n'+trim+'\n'+checks,encoding='utf8')
    binary=directory/'retention.exe'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-I'+str(root/'../testing/main/camera'),
                    '-I'+str(root/'SmartVest_SJKS01_Camera/src'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary),str(directory/'card')],check=True)
