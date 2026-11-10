#pragma once
// No persistent mode flags: reboot always starts ordinary recording.
void endDownload(bool complete){
  if(downloadTransfer.active())downloadTransfer.stop(complete);
  downloadFile.close();
  const bool resume=downloadResume;downloadResume=false;
  if(resume&&sd&&camera&&!paused&&!running){
    if(!trim()||!openClip())failStorage("Recording could not resume after download");
  }
  publish();
}
bool beginDownload(const FileRequest& request,char* error,size_t capacity){
  auto fail=[&](const char* message){snprintf(error,capacity,"%s",message);return false;};
  if(downloadTransfer.active()||!events.empty()||verifying||recoveryVerifying||recovery.busy()||recovery.ready())return fail("Wait until camera protection and recovery finish.");
  if(!CameraCrypto::validHex(request.token,32)||!ClipNames::validStem(request.beforeName))return fail("Invalid download permission.");
  uint32_t controlId,deadline;bool cancelled;
  portENTER_CRITICAL(&snapshotMux);controlId=downloadControlId;deadline=downloadControlUntil;cancelled=downloadControlStop;portEXIT_CRITICAL(&snapshotMux);
  if(controlId!=request.id||cancelled||int32_t(deadline-millis())<=0)return fail("Download permission expired. Try again.");
  Clip clip;JsonDocument record;
  if(!loadJson(ClipStorage::metadata(request.beforeName),record)||!clipFromJson(record.as<JsonObjectConst>(),clip)||!clip.good||clip.session!=request.beforeSession||clip.start!=request.beforeStart||clip.stem!=request.beforeName)return fail("This clip is no longer available. Update the list.");
  if(running&&!current.frames)return fail("The camera is starting a clip. Try again shortly.");
  const bool resume=running;
  if(running&&!closeClip()){failStorage("Cannot finalize recording before download");return fail("Recording could not pause safely.");}
  const String path=videoPath(clip);downloadFile=SD_MMC.open(path,FILE_READ);
  const String filename=path.substring(path.lastIndexOf('/')+1);
  if(!downloadFile||downloadFile.size()!=clip.bytes||clip.bytes>MAX_AVI_BYTES||!downloadTransfer.begin(request.id,request.token,filename.c_str(),clip.bytes,millis(),deadline)){
    downloadResume=resume;endDownload(false);return fail("Cannot open this clip for download.");
  }
  downloadResume=resume;publish();return true;
}
bool serviceDownload(){
  if(!downloadTransfer.active())return false;
  uint32_t controlId,deadline;bool cancelled;
  portENTER_CRITICAL(&snapshotMux);controlId=downloadControlId;deadline=downloadControlUntil;cancelled=downloadControlStop;portEXIT_CRITICAL(&snapshotMux);
  if(controlId!=downloadTransfer.id||cancelled||!sd||!events.empty()||verifying||recoveryVerifying){endDownload(false);return false;}
  downloadTransfer.lease=deadline;
  downloadTransfer.pump(downloadFile,millis());
  if(!downloadTransfer.active()){endDownload(downloadTransfer.state==3);return false;}
  publish();return true;
}
