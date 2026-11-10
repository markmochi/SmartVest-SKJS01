#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/AviVerifier.h"
#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/InterruptedAvi.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
uint32_t clockMs=0;
constexpr const char* KEY="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
std::vector<uint8_t> read(const String& path){std::ifstream f(SD_MMC.path(path),std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const String& path,const std::vector<uint8_t>& bytes){std::ofstream f(SD_MMC.path(path),std::ios::binary);f.write((const char*)bytes.data(),bytes.size());assert(f.good());}
bool check(const String& path,uint32_t frames,uint32_t duration,uint64_t size,const char* hash){
 AviVerifier v;if(!v.begin(path,frames,duration,size,hash))return false;
 unsigned steps=0;while(!v.done()){v.step();assert(++steps<5000);}return v.good();
}
void run(InterruptedAvi& job){unsigned steps=0;while(job.busy()){job.step();assert(++steps<10000);}}
int main(int argc,char** argv){
 assert(argc==3);SD_MMC.root=argv[1];
 for(const auto* p:{"/clips","/system/recording","/system/recovery"})std::filesystem::create_directories(SD_MMC.root+p);
 std::ifstream input(argv[2],std::ios::binary);std::vector<uint8_t> jpeg{std::istreambuf_iterator<char>(input),{}};
 AviWriter w;assert(w.begin("/clips/front_test",640,480));
 for(int i=0;i<24;++i)assert(w.frame(jpeg.data(),jpeg.size()));
 assert(w.close(3000));const auto original=read("/clips/front_test.avi");
 assert(w.finalizedSize()==original.size());assert(check("/clips/front_test.avi",24,3000,original.size(),w.payloadDigest()));
 for(size_t at=0;at<224;++at){auto damaged=original;damaged[at]^=1;put("/clips/front_damaged.avi",damaged);assert(!check("/clips/front_damaged.avi",24,3000,original.size(),w.payloadDigest()));}
 for(size_t at:{size_t(0),size_t(4),size_t(32),size_t(48),size_t(64),size_t(128),size_t(132),size_t(140),size_t(144),size_t(216),size_t(224),size_t(232),size_t(400),original.size()-12}){
  auto damaged=original;damaged[at]^=1;put("/clips/front_damaged.avi",damaged);
  assert(!check("/clips/front_damaged.avi",24,3000,original.size(),w.payloadDigest()));
 }
 put("/clips/front_damaged.avi",std::vector<uint8_t>(original.size(),0));assert(!check("/clips/front_damaged.avi",24,3000,original.size(),w.payloadDigest()));
 auto shortFile=original;shortFile.resize(shortFile.size()-1);put("/clips/front_damaged.avi",shortFile);assert(!check("/clips/front_damaged.avi",24,3000,0,w.payloadDigest()));
 assert(!check("/clips/front_test.avi",24,3000,original.size(),"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
 std::cout<<"PASS actual AVI writer/verifier: final size, headers, index, timing, payload corruption, all-zero file, truncation and wrong digest\n";
 const String identity="/clips/front_interrupted",base="/system/recording/front_interrupted";
 AviWriter interrupted;assert(interrupted.begin(base,640,480));RecordingCheckpoint::State state;
 state.session=41;state.start=100;state.first=100;state.utc=1792800000000ULL;
 uint8_t record[RecordingCheckpoint::SIZE],newer[RecordingCheckpoint::SIZE];
 for(int i=0;i<12;++i)assert(interrupted.frame(jpeg.data(),jpeg.size()));
 state.frames=12;state.last=1475;state.at=1600;state.bytes=interrupted.size();assert(interrupted.checkpoint(state.payload));
 assert(RecordingCheckpoint::encode(state,KEY,"front",identity.c_str(),record));
 for(int i=0;i<12;++i)assert(interrupted.frame(jpeg.data(),jpeg.size()));
 state.frames=24;state.last=2975;state.at=3100;state.bytes=interrupted.size();assert(interrupted.checkpoint(state.payload));
 assert(RecordingCheckpoint::encode(state,KEY,"front",identity.c_str(),newer));
 for(int i=0;i<3;++i)assert(interrupted.frame(jpeg.data(),jpeg.size()));interrupted.abort();
 const auto raw=read(base+".part");
 for(size_t cut=0;cut<RecordingCheckpoint::SIZE;++cut){
  std::vector<uint8_t> journal(record,record+sizeof(record));journal.insert(journal.end(),newer,newer+cut);put(base+".rec",journal);
  InterruptedAvi job;assert(job.begin(identity,"front",KEY));run(job);assert(job.ready()&&!job.isLegacy()&&job.frames()==12);
  assert(check(job.temporary()+".avi",12,job.duration(),job.bytes(),job.digest()));assert(read(base+".part")==raw);
 }
 for(size_t at=0;at<RecordingCheckpoint::SIZE;++at){
  std::vector<uint8_t> journal(record,record+sizeof(record));journal.insert(journal.end(),newer,newer+sizeof(newer));journal[sizeof(record)+at]^=1;put(base+".rec",journal);
  InterruptedAvi job;assert(job.begin(identity,"front",KEY));run(job);assert(job.ready()&&job.frames()==12);assert(read(base+".part")==raw);
 }
 put(base+".rec",{newer,newer+sizeof(newer)});
 for(unsigned cut=0;cut<50;++cut){
  InterruptedAvi job;assert(job.begin(identity,"front",KEY));for(unsigned i=0;i<cut&&job.busy();++i)job.step();job.cancel();
  assert(read(base+".part")==raw);assert(job.begin(identity,"front",KEY));run(job);assert(job.ready()&&job.frames()==24);
  assert(check(job.temporary()+".avi",24,job.duration(),job.bytes(),job.digest()));
 }
 // A power cut after public rename must recover from every supported name.
 snprintf(state.video,sizeof(state.video),"%s","/clips/front_renamed");uint8_t renamed[RecordingCheckpoint::SIZE];
 assert(RecordingCheckpoint::encode(state,KEY,"front",identity.c_str(),renamed));
 for(const char* suffix:{"_PROTECTED_INCIDENT.avi","_PROTECTED_INTERRUPTION.avi","_LOCKED.avi",".avi"}){
  const String published=String(state.video)+suffix;put(base+".rec",{renamed,renamed+sizeof(renamed)});
  assert(SD_MMC.rename(base+".part",published));InterruptedAvi job;assert(job.begin(identity,"front",KEY));run(job);
  assert(job.ready()&&job.frames()==24&&job.original()==published&&job.checkpoint().utc==1792800000000ULL);
  job.cancel();assert(SD_MMC.rename(published,base+".part"));assert(read(base+".part")==raw);
 }
 put(base+".rec",{newer,newer+sizeof(newer)});
 // Authenticated records are bound to both the camera and the original identity.
 RecordingCheckpoint::State decoded;assert(!RecordingCheckpoint::decode(newer,KEY,"rear",identity.c_str(),decoded));assert(!RecordingCheckpoint::decode(newer,KEY,"front","/clips/front_other",decoded));
 auto corrupted=raw;corrupted[500]^=1;put(base+".part",corrupted);
 InterruptedAvi invalid;assert(invalid.begin(identity,"front",KEY));run(invalid);assert(invalid.failed());put(base+".part",raw);
 // Legacy salvage is explicitly structural and never supplies trusted timing.
 assert(SD_MMC.remove(base+".rec"));auto torn=raw;torn.resize(torn.size()-jpeg.size()/2);put(base+".part",torn);
 InterruptedAvi legacy;assert(legacy.begin(identity,"front",KEY));run(legacy);assert(legacy.ready()&&legacy.isLegacy()&&legacy.frames()==26&&legacy.checkpoint().session==0);
 assert(check(legacy.temporary()+".avi",26,legacy.duration(),legacy.bytes(),legacy.digest()));assert(read(base+".part")==torn);
 hostWriteBudget=230;AviWriter full;assert(full.begin("/clips/front_shortwrite",640,480));assert(!full.frame(jpeg.data(),jpeg.size()));assert(!full.close(125));hostWriteBudget=SIZE_MAX;
 std::cout<<"PASS 304 checkpoint cut points, 304 authenticated-record corruptions, 50 recovery interruptions, committed-only frames, wrong identity, corrupt payload, legacy partial salvage, source preservation and short writes\n";
}
