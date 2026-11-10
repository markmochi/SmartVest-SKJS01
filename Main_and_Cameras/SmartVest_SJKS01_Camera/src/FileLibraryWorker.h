#pragma once
// Included inside CameraApp's private namespace after recorder helpers.
File libraryDirectory;
FileRequest libraryWork;
FileReply libraryPage;
bool libraryBusy=false;uint8_t libraryPhase=0;char libraryActive[81]{};
uint64_t libraryCutoffSession=0,libraryCutoffStart=0;
uint32_t libraryStarted=0,libraryExpected=0,librarySeen=0;
uint64_t libraryLatest=0;

// Cleanup is explicit and stays on the recorder task. Remove source/staging
// copies before the public video, so an interrupted deletion retains a playable
// copy and cannot mistake an old recovery archive for a fresh recording.
bool clearInterruptionFiles(const String& stem,bool archive){
  const String base=ClipStorage::internal(stem,archive?"recovery":"recording");
  for(const char* suffix:{".raw",".part",".idx",".rec",".avi"}){
    if(!archive&&(!strcmp(suffix,".raw")||!strcmp(suffix,".avi")))continue;
    const String path=base+suffix;
    if(SD_MMC.exists(path)){if(!SD_MMC.remove(path))return false;++libraryPage.recoveryDeleted;}
  }
  return true;
}
bool interruptionArtifact(const String& path,String& stem){
  const char* suffix=nullptr;
  for(const char* ext:{".raw",".part",".idx",".rec",".avi"})if(path.endsWith(ext)){suffix=ext;break;}
  if(!suffix)return false;
  stem=String("/clips/")+path.substring(path.lastIndexOf('/')+1,path.length()-strlen(suffix));
  if(!ClipNames::validStem(stem.c_str())||!stem.startsWith(String("/clips/")+ROLE+"_"))return false;
  if(libraryPhase==1){
    const String prefix=String("/clips/")+ROLE+"_recovered_";
    if(!stem.startsWith(prefix)||stem.length()!=prefix.length()+24)return false;
    for(size_t i=prefix.length();i<stem.length();++i)if(!((stem[i]>='0'&&stem[i]<='9')||(stem[i]>='a'&&stem[i]<='f')))return false;
  }else if(!strcmp(suffix,".raw")||!strcmp(suffix,".avi"))return false;
  return stem!=libraryActive&&(!running||stem!=current.stem)&&!SD_MMC.exists(ClipStorage::video(stem));
}
void finishLibrary(const char* error=""){
  libraryDirectory.close();libraryBusy=false;
  if(libraryWork.operation>=3&&libraryPage.deleted)latestClipUtc=error[0]?0:std::max(libraryLatest,lastSavedUtc);
  snprintf(libraryPage.error,sizeof(libraryPage.error),"%s",error);
  portENTER_CRITICAL(&snapshotMux);fileReply=libraryPage;fileReplyReady=true;portEXIT_CRITICAL(&snapshotMux);
  publish();
}
bool serviceLibrary(){
  if(!libraryBusy){
    FileRequest incoming;bool have=false;
    portENTER_CRITICAL(&snapshotMux);
    if(fileRequestReady){incoming=fileRequest;fileRequestReady=false;have=true;}
    portEXIT_CRITICAL(&snapshotMux);
    if(!have)return false;
    if(incoming.id==libraryPage.id)return true; // Lost HTTP reply cannot replay deletion.
    libraryWork=incoming;libraryPage=FileReply{};libraryPage.id=incoming.id;libraryPage.stream=incoming.stream;
    if(incoming.stream!=logStream){finishLibrary("Camera restarted. Refresh the list.");return true;}
    if(uint32_t(millis()-incoming.receivedAt)>10000){finishLibrary("Camera request expired before it could start. Try again.");return true;}
    if(!sd){finishLibrary("Memory card unavailable.");return true;}
    if(incoming.operation==5){
      libraryPage.downloadReady=beginDownload(incoming,libraryPage.error,sizeof(libraryPage.error));
      libraryPage.downloadSize=libraryPage.downloadReady?downloadTransfer.size:0;
      // finishLibrary copies its error into the same reply, so pass a separate buffer.
      char message[161];snprintf(message,sizeof(message),"%s",libraryPage.error);finishLibrary(message);return true;
    }
    if(downloadTransfer.active()){finishLibrary("A clip is downloading. Try again when it finishes.");return true;}
    libraryDirectory=SD_MMC.open("/system/clips");
    if(!libraryDirectory){finishLibrary("Cannot read the recording list.");return true;}
    libraryCutoffSession=session;libraryCutoffStart=running?current.start:clockMs();libraryStarted=millis();libraryBusy=true;libraryPhase=0;snprintf(libraryActive,sizeof(libraryActive),"%s",running?current.stem.c_str():"");
    libraryExpected=libraryWork.operation==7?0:libraryWork.operation==2?lockedCount:normalCount+(libraryWork.operation==4?lockedCount:0);libraryLatest=0;librarySeen=0;
  }
  if(!sd){finishLibrary("Memory card became unavailable. Refresh before trying again.");return true;}
  if(session!=libraryCutoffSession){finishLibrary("Recording session changed. Refresh before trying again.");return true;}
  if(uint32_t(millis()-libraryStarted)>90000){finishLibrary("Request stopped before completion. Refresh the list to check saved recordings.");return true;}
  // Protection and recovery have priority, including a LOCK arriving after the
  // user confirmed deletion. Never touch the active or subsequently saved clip.
  if(libraryWork.operation>=3&&(!events.empty()||verifying||recoveryVerifying||recovery.busy()||recovery.ready())){
    finishLibrary("Deletion stopped because incident protection or recovery is active.");return true;
  }
  File item=libraryDirectory.openNextFile();
  if(!item){
    if(libraryWork.operation==7&&libraryPhase<2){
      libraryDirectory.close();++libraryPhase;
      libraryDirectory=SD_MMC.open(libraryPhase==1?"/system/recovery":"/system/recording");
      if(!libraryDirectory){finishLibrary("Some interruption files could not be checked. Try again.");return true;}
      return true;
    }
    const uint64_t total=SD_MMC.totalBytes(),used=SD_MMC.usedBytes();freeAtBoundary=total>=used?total-used:0;
    if(libraryWork.operation<=2&&librarySeen<libraryExpected)libraryPage.skipped=std::max(libraryPage.skipped,libraryExpected-librarySeen);
    if((libraryWork.operation==3||libraryWork.operation==4)&&libraryPage.deleted<libraryExpected)libraryPage.skipped=std::max(libraryPage.skipped,libraryExpected-libraryPage.deleted);
    finishLibrary(libraryPage.skipped?"Some recordings need memory card inspection and were left unchanged.":"");return true;
  }
  String path=item.name();const bool directory=item.isDirectory();item.close();
  if(directory)return true;
  if(libraryPhase){
    String stem;if(!interruptionArtifact(path,stem))return true;
    if(!clearInterruptionFiles(stem,libraryPhase==1)){finishLibrary("Some interruption files could not be deleted. Try again.");return true;}
    return true;
  }
  if(!path.endsWith(".json"))return true;
  if(!path.startsWith("/"))path="/system/clips/"+path;
  JsonDocument doc;Clip clip;
  if(!loadJson(path,doc)||!clipFromJson(doc.as<JsonObjectConst>(),clip)||path!=ClipStorage::metadata(clip.stem)){
    ++libraryPage.skipped;return true;
  }
  const String video=videoPath(clip);if(!SD_MMC.exists(video))return true;
  const bool locked=isPinned(clip);
  if(libraryWork.operation>=3){
    if(clip.session>libraryCutoffSession||(clip.session==libraryCutoffSession&&clip.start>=libraryCutoffStart)){libraryLatest=std::max(libraryLatest,clip.utcStart);return true;}
    if(libraryWork.operation==3&&locked){libraryLatest=std::max(libraryLatest,clip.utcStart);return true;}
    if(libraryWork.operation==7){
      if(doc["schema"]!=2||!clip.recovered){libraryLatest=std::max(libraryLatest,clip.utcStart);return true;}
      if(!clearInterruptionFiles(clip.stem,true)){finishLibrary("Some interruption files could not be deleted. Try again.");return true;}
    }
    if(!SD_MMC.remove(video)){finishLibrary("A recording could not be deleted. Refresh the list before trying again.");return true;}
    ++libraryPage.deleted;++deletedCount;
    if(lastClipName==video){lastClipName="";lastSavedUtc=0;}
    if(locked){if(lockedCount)--lockedCount;}else if(normalCount)--normalCount;
    for(auto it=history.begin();it!=history.end();)if(it->stem==clip.stem)it=history.erase(it);else ++it;
    for(auto it=recent.begin();it!=recent.end();)if(it->stem==clip.stem)it=recent.erase(it);else ++it;
    // Video is removed first. A partial cleanup never unprotects a surviving AVI.
    const String pin=ClipStorage::pinPath(clip.stem),time=ClipStorage::internal(clip.stem)+".protected";
    if(!SD_MMC.remove(path)||(SD_MMC.exists(pin)&&!SD_MMC.remove(pin))||(SD_MMC.exists(time)&&!SD_MMC.remove(time))){finishLibrary("Video deleted; its supporting record needs memory card inspection.");return true;}
    emitLog("clip_deleted",video,"Deleted by dashboard request");return true;
  }
  latestClipUtc=std::max(latestClipUtc,clip.utcStart);
  if(locked!=(libraryWork.operation==2))return true;
  ++librarySeen;
  FileEntry entry;snprintf(entry.name,sizeof(entry.name),"%s",clip.stem.c_str());
  entry.session=clip.session;entry.start=clip.start;entry.utc=clip.utcStart;entry.locked=locked;entry.reason=locked?(doc["schema"]!=2?3:clip.recovered?2:clip.legacy?3:1):0;
  if(locked){File time=SD_MMC.open(ClipStorage::internal(clip.stem)+".protected",FILE_READ);if(time&&time.size()==sizeof(uint64_t))time.read(reinterpret_cast<uint8_t*>(&entry.protectedUtc),sizeof(uint64_t));}
  FileEntry cursor;cursor.session=libraryWork.beforeSession;cursor.start=libraryWork.beforeStart;
  snprintf(cursor.name,sizeof(cursor.name),"%s",libraryWork.beforeName);
  if(fileBefore(entry,cursor))addFileEntry(libraryPage,entry);
  return true;
}
