#pragma once
#include "AviWriter.h"
#include "RecordingCheckpoint.h"
#include "ClipNames.h"
#include "ClipStorage.h"
#include <algorithm>
// Each step reads one checkpoint or copies at most 8 KiB. Source data stays
// immutable until the caller verifies and commits the protected replacement.
class InterruptedAvi {
 public:
  bool begin(const String& identity,const char* role,const char* key){
    cancel();stem=identity;cameraRole=role;secret=key;record=RecordingCheckpoint::State{};
    if(!ClipNames::validStem(stem.c_str())||!workspace.reserve(8192))return false;buffer=workspace.data();
    char id[65];CameraCrypto::Hash hash;
    if(!hash.begin()||!hash.add(stem.c_str(),stem.length())||!hash.finish(id))return false;
    target=String("/clips/")+role+"_recovered_"+String(id).substring(0,24);
    scratch=String("/system/recovery/")+target.substring(7);
    journal=SD_MMC.open(String("/system/recording/")+stem.substring(7)+".rec",FILE_READ);
    legacy=!journal;phase=legacy?2:1;return true;
  }
  void step(){
    if(phase==1){
      uint8_t bytes[RecordingCheckpoint::SIZE];
      if(journal.available()>=int(sizeof(bytes))){
        if(++records>128||journal.read(bytes,sizeof(bytes))!=sizeof(bytes)){fail();return;}
        RecordingCheckpoint::State next;
        if(RecordingCheckpoint::decode(bytes,secret,cameraRole,stem.c_str(),next)&&
           (!next.video[0]||ClipNames::validStem(next.video))&&
           (!haveRecord||(next.session==record.session&&next.start==record.start&&next.frames>=record.frames&&next.bytes>=record.bytes&&next.at>=record.at))){record=next;haveRecord=true;}
        return;
      }
      journal.close();if(!haveRecord||!record.frames){fail();return;}phase=2;return;
    }
    if(phase==2){
      source=String("/system/recording/")+stem.substring(7)+".part";
      if(!SD_MMC.exists(source)&&record.video[0]){
        source=ClipStorage::video(String(record.video));
      }
      if(!SD_MMC.exists(source)&&SD_MMC.exists(scratch+".raw"))source=scratch+".raw";
      raw=SD_MMC.open(source,FILE_READ);if(!raw||raw.size()<224||raw.size()>128ULL*1024*1024){fail();return;}
      limit=legacy?raw.size():record.bytes;
      uint8_t header[224];
      if(limit>raw.size()||raw.read(header,sizeof(header))!=sizeof(header)||memcmp(header,"RIFF",4)||memcmp(header+8,"AVI ",4)||memcmp(header+220,"movi",4)){fail();return;}
      // Only this derived staging file is expendable; the immutable original
      // must exist before restarting an interrupted recovery attempt.
      if(SD_MMC.exists(scratch+".part")&&!SD_MMC.remove(scratch+".part")){fail();return;}
      if(!output.begin(scratch,640,480)){fail();return;}phase=3;return;
    }
    if(phase!=3)return;
    if(!left){
      if((!legacy&&copied==record.frames)||(legacy&&(raw.position()+8>limit||copied==1024))){finish();return;}
      const uint64_t at=raw.position();uint8_t chunk[8];
      if(raw.read(chunk,8)!=8||memcmp(chunk,"00dc",4)){if(legacy&&copied)finish();else fail();return;}
      length=get(chunk+4);const uint64_t end=at+8+length+(length&1);
      uint8_t edge[2],pad=0;
      bool valid=length>=4&&length<=1024*1024&&end<=limit;
      if(valid){valid=raw.read(edge,2)==2&&edge[0]==0xff&&edge[1]==0xd8&&raw.seek(at+8+length-2)&&raw.read(edge,2)==2&&edge[0]==0xff&&edge[1]==0xd9;}
      if(valid&&(length&1))valid=raw.read(&pad,1)==1&&pad==0;
      if(!valid){if(legacy&&copied)finish();else fail();return;}
      if(!raw.seek(at+8)||!output.startFrame(length)){fail();return;}left=length;
    }
    const size_t count=std::min(size_t(left),size_t(8192));
    if(raw.read(buffer,count)!=count||!output.appendFrame(buffer,count)){fail();return;}left-=count;
    if(!left){if((length&1)&&raw.read()<0){fail();return;}if(!output.endFrame()){fail();return;}++copied;}
  }
  bool ready()const{return phase==4;}
  bool failed()const{return phase==5;}
  bool busy()const{return phase>0&&phase<4;}
  bool isLegacy()const{return legacy;}
  uint32_t frames()const{return copied;}
  uint32_t duration()const{return elapsed;}
  const char* digest()const{return digestValue;}
  uint64_t bytes()const{return output.finalizedSize();}
  const RecordingCheckpoint::State& checkpoint()const{return record;}
  const String& identity()const{return stem;}
  const String& destination()const{return target;}
  const String& original()const{return source;}
  const String& temporary()const{return scratch;}
  void cancel(){journal.close();raw.close();output.abort();phase=0;records=copied=left=0;haveRecord=false;digestValue[0]=0;}
 private:
  File journal,raw;AviWriter output;RecordingCheckpoint::State record;
  String stem,target,scratch,source;const char* cameraRole="",*secret="";
  MediaBuffer workspace;uint8_t* buffer=nullptr;uint8_t phase=0;uint32_t records=0,copied=0,left=0,length=0,elapsed=0;
  uint64_t limit=0;bool legacy=false,haveRecord=false;char digestValue[65]{};
  static uint32_t get(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
  void fail(){journal.close();raw.close();output.abort();phase=5;}
  void finish(){
    char payload[65];
    if(!copied||(!legacy&&(raw.position()!=record.bytes||copied!=record.frames))||!output.checkpoint(payload)||
       (!legacy&&!CameraCrypto::equal(payload,record.payload,64))){fail();return;}
    elapsed=legacy?std::max<uint32_t>(1,copied*125):uint32_t(record.at-record.start);
    if(!elapsed||(!legacy&&record.at-record.start>120000)){fail();return;}
    // Finalize into the private staging directory. An existing public output
    // is independently verified by the caller before anything is replaced.
    if(SD_MMC.exists(scratch+".avi")&&!SD_MMC.remove(scratch+".avi")){fail();return;}
    if(!output.close(elapsed,scratch+".avi")){fail();return;}
    snprintf(digestValue,sizeof(digestValue),"%s",output.payloadDigest());raw.close();phase=4;
  }
};
