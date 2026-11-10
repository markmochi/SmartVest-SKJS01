#include "DashboardServer.h"
#include <esp_system.h>

void DashboardServer::serveCameraFiles(bool write){
  auto* session=authorize(write,Access::Role::Rider);if(!session)return;download.tick(millis());
  if(!download.active())for(auto& pending:cameraFiles)if(pending.pending&&pending.operation==5&&pending.id==download.id){pending.fail("Download stopped. Try again.");}
  for(auto& file:cameraFiles)file.expire(millis());
  const auto summary=[this](JsonObject response,int index){
    const auto& camera=cameras[index];response["camera"]=index?"rear":"front";
    response["online"]=camera.online(millis(),Config::CAMERA_OFFLINE_MS);response["sdReady"]=camera.sdReady;response["recording"]=camera.recording;
    response["ageMs"]=camera.present?millis()-camera.seen:UINT32_MAX;
    const auto t=camera.telemetry.as<JsonObjectConst>();
    response["frameFresh"]=t["schema"]==1&&t["currentFrames"].is<uint64_t>()&&t["currentFrames"].as<uint64_t>()>0&&t["frameAgeMs"].is<uint64_t>()&&t["frameAgeMs"].as<uint64_t>()<=Config::CAMERA_FRAME_FRESH_AT_REPORT_MS;
    for(const char* key:{"normalRetained","lockedRetained","freeBytesAtBoundary","totalBytes","latestUtcMs","filesSupported","downloadsSupported","downloadState","activeLocks"})response[key]=t[key];
    response["busy"]=cameraFiles[index].pending;response["requestId"]=cameraFiles[index].id;
  };
  if(!write&&!web.hasArg("camera")){auto response=makeJson();battery.writeRiderBattery(response["battery"].to<JsonObject>());writeDownload(response["download"].to<JsonObject>(),session);auto rows=response["cameras"].to<JsonArray>();for(int i=0;i<2;++i)summary(rows.add<JsonObject>(),i);sendJson(response);return;}
  const String role=web.arg("camera");
  if(role!="front"&&role!="rear"){sendError(400,"Invalid request.");return;}
  const int index=role=="rear";auto& camera=cameras[index];auto& file=cameraFiles[index];
  const auto accepted=[this,&file]() __attribute__((noinline)) {
    auto response=makeJson();response["id"]=file.id;response["pending"]=true;sendJson(response,202);
  };
  if(web.hasArg("job")){
    if(write||!file.id||web.arg("job")!=String(file.id)){sendError(409,"Request expired. Update the list.");return;}
    JsonDocument response=file.result; // sendJson consumes only this bounded copy.
    if(file.operation==5&&file.result["downloadReady"]==true&&download.active()&&download.id==file.id&&download.ownedBy(session->csrf.c_str())){
      char url[96];snprintf(url,sizeof(url),"http://%u.%u.%u.%u:8081/clip?token=%s",unsigned(camera.address[0]),unsigned(camera.address[1]),unsigned(camera.address[2]),unsigned(camera.address[3]),download.token);
      response["url"]=JsonString(url,false);
    }else response.remove("url");
    response["id"]=file.id;response["pending"]=file.pending;sendJson(response,file.pending?202:200);return;
  }
  if(!write){auto response=makeJson();summary(response.to<JsonObject>(),index);sendJson(response);return;}
  auto body=makeJson();if(!readJsonBody(body))return;
  const unsigned operation=body["operation"]|0U;
  if(!body["operation"].is<unsigned>()||operation<1||operation>7){sendError(400,"Invalid request.");return;}
  if(operation==6){
    if(!download.active()||download.camera!=index||body["id"]!=download.id){sendError(409,"Download already ended.");return;}
    if(session->role!=Access::Role::Developer&&!download.ownedBy(session->csrf.c_str())){sendError(403,"Owned by another session.");return;}
    download.state=6;auto response=makeJson();response["ok"]=true;sendJson(response);return;
  }
  if(download.active()){sendError(409,"One clip can download at a time.");return;}
  const auto telemetry=camera.telemetry.as<JsonObjectConst>();
  const unsigned filesVersion=telemetry["filesSupported"]|0U;
  if(!camera.online(millis(),Config::CAMERA_STATUS_FRESH_MS)||!camera.sdReady||filesVersion<1||filesVersion>2||(operation==7&&filesVersion<2)){sendError(409,"Connect the updated camera and its memory card first.");return;}
  if(file.pending){
    // Two dashboards asking for the same page share its in-flight SD read.
    if(operation<3&&operation==file.operation&&file.stream==telemetry["stream"]&&
       file.beforeSession==(body["beforeSession"]|UINT64_MAX)&&file.beforeStart==(body["beforeStart"]|UINT64_MAX)&&
       !strcmp(file.beforeName,body["beforeName"]|"")){
      accepted();return;
    }
    sendError(409,"Camera busy with another request.");return;
  }
  if(operation>=3&&operation!=5){
    const JsonString confirmation=body["confirm"].as<JsonString>();
    const char* expected=operation==4?"DELETE ALL":operation==7?"DELETE INTERRUPTED":"DELETE NORMAL";
    if(confirmation.size()!=strlen(expected)||memcmp(confirmation.c_str(),expected,confirmation.size())){sendError(400,"Confirm deletion.");return;}
  }
  if(operation>=3&&(!emergency||(operation==5?!emergency->canDownloadFrom(index):!emergency->canResetData())||telemetry["activeLocks"]!=0)){sendError(409,operation==5?"This camera is protecting incident footage or its incident storage is unavailable. Try again when protection finishes.":"Wait until incident protection and alerts have finished.");return;}
  const String name=body["beforeName"]|"";
  if(name.length()>80||name.indexOf("..")>=0){sendError(400,"Invalid request.");return;}
  if(operation==5){
    if(download.startup){sendError(409,"Downloads will be ready shortly.");return;}
    if(telemetry["downloadsSupported"]!=1||!body["beforeSession"].is<uint64_t>()||!body["beforeStart"].is<uint64_t>()||!name.length()||camera.address[0]!=192||camera.address[1]!=168||camera.address[2]!=4){sendError(409,"Download unavailable. Update the camera and refresh the clip list.");return;}
    for(const auto& pending:cameraFiles)if(pending.pending){sendError(409,"Camera busy with another request.");return;}
  }
  file.result.clear();file.id=esp_random();if(!file.id)file.id=1;
  file.stream=telemetry["stream"];file.at=millis();file.operation=operation;
  file.beforeSession=body["beforeSession"]|UINT64_MAX;file.beforeStart=body["beforeStart"]|UINT64_MAX;
  snprintf(file.beforeName,sizeof(file.beforeName),"%s",name.c_str());file.pending=true;
  if(operation==5){
    download=DownloadLease{};download.startup=false;download.id=file.id;download.camera=index;download.stream=file.stream;download.started=millis();download.until=millis()+45000;download.state=5;
    const String token=Security::randomHex(16);if(token.length()!=32){download.finish(4,millis());file.pending=false;sendError(503,"Memory unavailable.");return;}
    snprintf(download.token,sizeof(download.token),"%s",token.c_str());snprintf(download.owner,sizeof(download.owner),"%s",session->csrf.c_str());
  }
  accepted();
}
void DashboardServer::offerCameraFiles(int index,JsonObject out){
  download.tick(millis());
  auto& file=cameraFiles[index];file.expire(millis());const bool idle=emergency&&emergency->canResetData();
  const bool downloadIdle=emergency&&emergency->canDownloadFrom(index);
  // Terminal failed incidents are retried for reconciliation. While managing
  // clips, defer those retries; a new unfinished incident still preempts us.
  if((file.pending&&(file.operation==5?downloadIdle:idle))||(downloadIdle&&download.active()&&download.camera==index))out["command"]=nullptr;
  if(download.active()&&download.camera==index){
    if(!downloadIdle||!out["command"].isNull())download.state=6;
    auto control=out["download"].to<JsonObject>();control["id"]=download.id;control["stream"]=download.stream;control["leaseMs"]=download.allowance(millis());control["stop"]=download.state==6;
  }
  if(!file.pending)return;
  if(file.operation==5&&(!download.active()||download.id!=file.id||download.state==6)){file.fail("Download stopped. Try again.");return;}
  if(file.stream!=cameras[index].cursor.stream){
    file.fail("Request expired. Update the list.");return;
  }
  // Incident delivery is always first. A queued deletion is never sent while
  // Main has unfinished protection or alert work.
  if(file.operation>=3&&(!(file.operation==5?downloadIdle:idle)||!out["command"].isNull()))return;
  file.write(out["files"].to<JsonObject>());
  if(file.operation==5)out["files"]["token"]=JsonString(download.token,false);
}
void DashboardServer::receiveCameraFiles(){
  const int index=cameraIdentity();if(index<0)return;
  auto body=makeJson();if(!readJsonBody(body))return;auto& file=cameraFiles[index];
  if(!file.id||body["id"]!=file.id||body["stream"]!=file.stream){sendError(409,"Request expired. Update the list.");return;}
  const auto rows=body["clips"].as<JsonArrayConst>();
  if((!body["clips"].isNull()&&!body["clips"].is<JsonArray>())||rows.size()>5||measureJson(body)>2600||!body["error"].is<const char*>()||strlen(body["error"].as<const char*>())>160){sendError(400,"Invalid request.");return;}
  if(file.operation==5&&body["downloadReady"]==true&&(!body["downloadSize"].is<uint64_t>()||body["downloadSize"].as<uint64_t>()==0||body["downloadSize"].as<uint64_t>()>134217728)){sendError(400,"Invalid request.");return;}
  for(auto row:rows)if(!row["name"].is<const char*>()||strlen(row["name"].as<const char*>())>80||!row["session"].is<uint64_t>()||!row["start"].is<uint64_t>()||!row["utcMs"].is<uint64_t>()||!row["protected"].is<bool>()){sendError(400,"Invalid request.");return;}
  if(file.pending){file.result=std::move(body);file.result.shrinkToFit();if(file.result.overflowed()){file.result.clear();sendError(503,"Memory unavailable.");return;}file.pending=false;}
  if(file.operation==5&&download.id==file.id&&file.result["downloadReady"]!=true)download.finish(4,millis());
  auto response=makeJson();response["ok"]=true;sendJson(response);
}

void DashboardServer::writeDownload(JsonObject out,Security::Session* session){
  download.tick(millis());const bool visible=download.active()||(download.id&&uint32_t(millis()-download.ended)<30000);
  out["state"]=visible?download.state:0;if(!visible)return;
  out["id"]=download.id;out["camera"]=download.camera?"rear":"front";out["sent"]=download.sent;out["size"]=download.size;
  out["canCancel"]=download.active()&&session&&(session->role==Access::Role::Developer||download.ownedBy(session->csrf.c_str()));
}
void DashboardServer::observeDownload(int index){
  if(!download.active()||download.camera!=index)return;
  const auto t=cameras[index].telemetry.as<JsonObjectConst>();
  if(t["stream"]!=download.stream){download.finish(4,millis());return;}
  if(t["downloadId"]!=download.id)return;
  const unsigned state=t["downloadState"]|0U;
  download.sent=t["downloadSent"]|uint64_t(0);download.size=t["downloadSize"]|uint64_t(0);
  if(state==3||state==4){download.finish(state,millis());return;}
  bool owner=false;for(const auto& session:security.sessions)if(download.ownedBy(session.csrf.c_str()))owner=true;
  if(!owner)download.state=6;
  if((state==1||state==2)&&download.state!=6){download.state=state;download.until=millis()+20000;}
}
