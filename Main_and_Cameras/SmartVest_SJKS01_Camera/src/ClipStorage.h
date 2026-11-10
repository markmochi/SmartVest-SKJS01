#pragma once
#include <SD_MMC.h>
#include "ClipNames.h"
#include <time.h>

// Stable metadata/pin identity is independent of the displayed video name.
// That lets the first clip receive a date at finalization if time arrived
// while it was recording, without changing pending incident references.
namespace ClipStorage {
inline String internal(const String& stem,const char* area="clips") {
  return String("/system/")+area+"/"+stem.substring(7);
}
inline String metadata(const String& stem) {return internal(stem)+".json";}
inline String legacyDestination(const String& path) {
  for(const char* suffix:{".json.tmp",".json",".lock",".part",".idx"}) {
    const size_t length=strlen(suffix);
    if(path.length()<length || strcmp(path.c_str()+path.length()-length,suffix))continue;
    const String stem=path.substring(0,path.length()-length);
    if(!ClipNames::validStem(stem.c_str()))return String();
    const char* area=(!strcmp(suffix,".part") || !strcmp(suffix,".idx"))?"recording":"clips";
    return internal(stem,area)+suffix;
  }
  return String();
}
inline bool migrateSidecar(const String& path) {
  const String destination=legacyDestination(path);
  if(!destination.length())return true;
  // Refuse conflicting data; never replace an existing recovery/pin file.
  return !SD_MMC.exists(destination) && SD_MMC.rename(path,destination);
}
inline String pinPath(const String& stem) {
  const String legacy=stem+".lock";
  return SD_MMC.exists(legacy)?legacy:internal(stem)+".lock";
}
inline String incident(const String& stem){return stem+"_PROTECTED_INCIDENT.avi";}
inline String interruption(const String& stem){return stem+"_PROTECTED_INTERRUPTION.avi";}
inline String protectedVideo(const String& stem){
  for(const char* suffix:{"_PROTECTED_INTERRUPTION.avi","_PROTECTED_INCIDENT.avi","_LOCKED.avi"}){
    const String path=stem+suffix;if(SD_MMC.exists(path))return path;
  }
  return String();
}
inline String video(const String& stem){
  const String saved=protectedVideo(stem);return saved.length()?saved:stem+".avi";
}
inline bool protectedClip(const String& identity,const String& videoStem){
  return SD_MMC.exists(pinPath(identity))||protectedVideo(videoStem).length();
}
inline bool splitVideo(const String& path,String& stem,bool& locked){
  for(const char* suffix:{"_PROTECTED_INTERRUPTION.avi","_PROTECTED_INCIDENT.avi","_LOCKED.avi",".avi"}){
    if(!path.endsWith(suffix))continue;
    stem=path.substring(0,path.length()-strlen(suffix));locked=strcmp(suffix,".avi")!=0;
    return ClipNames::validStem(stem.c_str());
  }
  return false;
}
// Only a filename suffix changes. Sealed metadata retains its stable videoStem.
// Both spellings remain readable if power is lost before/after this rename.
inline bool labelProtected(const String& stem,bool recovered){
  const String old=stem+"_LOCKED.avi";if(!SD_MMC.exists(old))return true;
  const String next=recovered?interruption(stem):incident(stem);
  return !SD_MMC.exists(next)&&SD_MMC.rename(old,next);
}
inline void recordProtectionTime(const String& identity){
  const time_t now=time(nullptr);const String recorded=internal(identity)+".protected";
  if(now<1577836800LL||now>=4102444800LL||SD_MMC.exists(recorded))return;
  const uint64_t utc=uint64_t(now)*1000;File f=SD_MMC.open(recorded,FILE_WRITE);
  if(f){f.write(reinterpret_cast<const uint8_t*>(&utc),sizeof(utc));f.flush();}
}
inline bool reserveIdentity(const String& identity){
  const String path=pinPath(identity);
  if(!SD_MMC.exists(path)){
    File f=SD_MMC.open(path,FILE_WRITE);if(!f||f.write(uint8_t('L'))!=1)return false;f.flush();f.close();
  }
  File check=SD_MMC.open(path,FILE_READ);const bool good=check&&check.read()=='L';check.close();return good;
}
inline bool pin(const String& identity,const String& videoStem) {
  const bool alreadyProtected=protectedClip(identity,videoStem);
  const String path=pinPath(identity);
  const String ordinary=videoStem+".avi",locked=incident(videoStem);
  if(protectedVideo(videoStem).length() && !SD_MMC.exists(ordinary) && !SD_MMC.exists(path))return true;
  if(!reserveIdentity(identity))return false;
  if(!alreadyProtected)recordProtectionTime(identity);
  // Persist the protection marker BEFORE changing the user's filename.
  // On interrupted/failed rename, the original AVI remains protected.
  if(SD_MMC.exists(ordinary)) {
    if(protectedVideo(videoStem).length() || !SD_MMC.rename(ordinary,locked))return false;
  }
  // A finalized LOCKED filename is itself a durable protection marker.
  // Keep .lock only for open recordings or if a failed rename needs it.
  if(protectedVideo(videoStem).length() && !SD_MMC.remove(path))return false;
  return true;
}
inline bool occupied(const String& stem) {
  for(const char* suffix:{".avi","_LOCKED.avi","_PROTECTED_INCIDENT.avi","_PROTECTED_INTERRUPTION.avi",".json",".json.tmp",".part",".idx",".rec",".lock"})
    if(SD_MMC.exists(stem+suffix))return true;
  for(const char* suffix:{".json",".json.tmp",".lock",".protected"})
    if(SD_MMC.exists(internal(stem)+suffix))return true;
  return SD_MMC.exists(internal(stem,"recording")+".part") ||
         SD_MMC.exists(internal(stem,"recording")+".idx") || SD_MMC.exists(internal(stem,"recording")+".rec");
}
inline bool choose(String& stem) {
  const String original=stem;
  // Clock rollback, quick restarts and erased NVS must never overwrite footage.
  // A suffix is shown only when an actual name collision occurs.
  for(unsigned n=1;n<=10000;++n) {
    stem=n==1?original:original+"_"+String(n);
    if(!ClipNames::validStem(stem.c_str()))return false;
    if(!occupied(stem))return true;
  }
  return false;
}
}
