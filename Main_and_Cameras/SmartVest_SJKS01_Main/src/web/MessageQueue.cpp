#include "DashboardServer.h"

// Called under the same state mutex as the modem service. Eligibility cannot
// change between inspection, durable cancellation and abandoning a prompt.
void DashboardServer::writeMessageQueue(JsonObject output, bool includeContacts){
  auto items=output["items"].to<JsonArray>();
  auto recipients=includeContacts?items:output["incidents"].to<JsonArray>();
  if(emergency)emergency->writePendingTexts(recipients,!includeContacts);
  size_t cancellableCount=0;
  for(auto item:recipients){
    const uint32_t boot=includeContacts?item["boot"].as<uint32_t>():item[0].as<uint32_t>();
    const uint32_t sequence=includeContacts?item["sequence"].as<uint32_t>():item[1].as<uint32_t>();
    const uint8_t recipient=includeContacts?item["recipient"].as<uint8_t>():item[2].as<uint8_t>();
    bool cancellable=includeContacts?item["state"]=="queued":item[3].as<bool>();
    for(const auto& job:store.data.jobs)if(job.id&&job.eventBoot==boot&&job.eventSequence==sequence&&job.recipientIndex==recipient){
      cancellable=sim800l.canCancel(job.id);break;
    }
    if(includeContacts)item["cancellable"]=cancellable;else item[3]=cancellable;
    if(cancellable)++cancellableCount;
  }
  for(const auto& job:store.data.jobs){
    if(!job.id||job.eventBoot||(job.state!="queued"&&job.state!="retry"&&job.state!="sending"))continue;
    const String* name=nullptr;for(const auto& contact:store.data.contacts)if(contact.phone==job.phone){name=&contact.name;break;}
    const bool cancellable=sim800l.canCancel(job.id);
    if(includeContacts){
      auto item=items.add<JsonObject>();item["id"]=job.id;item["phone"]=job.phone;item["state"]=job.state;
      if(name)item["name"]=*name;item["cancellable"]=cancellable;
    }else{
      auto item=items.add<JsonArray>();item.add(job.id);item.add(job.phone);
      item.add(JsonString(name?name->c_str():"",false));item.add(job.state);item.add(cancellable);
    }
    if(cancellable)++cancellableCount;
  }
  output["cancellable"]=cancellableCount;
  output["reason"]=!sim800l.online()?"Waiting for mobile service":!sim800l.simReady()?"Waiting for a ready SIM card":!sim800l.registered()?"Waiting for mobile coverage":"Waiting for the next sending turn";
  if(!cancellableCount&&(items.size()||recipients.size()))output["reason"]="Waiting for a send result from the mobile network";
}
void DashboardServer::cancelMessages(){
  auto request=makeJson();if(!readJsonBody(request))return;
  cancelMessageSelection(request.as<JsonObjectConst>());
}
void DashboardServer::cancelMessageSelection(JsonObjectConst selection){
  const bool all=selection.size()==1&&selection["all"].is<bool>()&&selection["all"]==true;
  const bool byId=selection.size()==1&&selection["id"].is<uint32_t>()&&selection["id"].as<uint32_t>()>0;
  const bool byIncident=selection.size()==3&&selection["boot"].is<uint32_t>()&&selection["boot"].as<uint32_t>()>0&&selection["sequence"].is<uint32_t>()&&selection["sequence"].as<uint32_t>()>0&&selection["recipient"].is<uint8_t>()&&selection["recipient"].as<uint8_t>()<Incidents::CONTACTS;
  if(!all&&!byId&&!byIncident){sendError(400,"Choose one pending text or cancel all pending texts.");return;}
  auto queue=makeJson();writeMessageQueue(queue.to<JsonObject>());
  uint32_t mask=0;uint32_t ids[Config::MAX_SMS_JOBS]={};size_t idCount=0,incidentCount=0,inProgress=0;
  for(auto item:queue["items"].as<JsonArray>()){
    const bool selected=all||(byId&&item["id"]==selection["id"])||(byIncident&&item["boot"]==selection["boot"]&&item["sequence"]==selection["sequence"]&&item["recipient"]==selection["recipient"]);
    if(!selected)continue;
    if(item["cancellable"]!=true){++inProgress;continue;}
    if(item["boot"].is<uint32_t>()){
      mask|=emergency->pendingTextMask(item["boot"],item["sequence"],item["recipient"]);++incidentCount;
    }else if(idCount<Config::MAX_SMS_JOBS)ids[idCount++]=item["id"];
  }
  if(!all&&!mask&&!idCount){sendError(409,inProgress?"This text is with the mobile network and cannot be cancelled. Check its send result.":"This text is no longer waiting to send. Refresh to see its result.");return;}
  size_t cancelled=0,failed=0;
  if(mask){
    if(emergency->cancelPendingTexts(mask)){
      cancelled+=incidentCount;
      for(const auto& job:store.data.jobs)if(job.eventBoot&&job.state=="cancelled")sim800l.cancellationCommitted(job.id);
    }else failed+=incidentCount;
  }
  if(idCount){
    DeviceState next=store.data;
    for(auto& job:next.jobs)for(size_t i=0;i<idCount;++i)if(job.id==ids[i]){job.state="cancelled";job.detail="Cancelled from dashboard before submission.";job.changed=millis();}
    if(store.commit(next)){
      cancelled+=idCount;for(size_t i=0;i<idCount;++i)sim800l.cancellationCommitted(ids[i]);
    }else failed+=idCount;
  }
  auto response=makeJson();response["ok"]=failed==0;response["cancelled"]=cancelled;response["notCancelled"]=failed;response["alreadySending"]=inProgress;
  response["message"]=failed?"Some cancellations could not be saved. Review the updated queue.":inProgress?"Pending texts cancelled. Submitted texts cannot be recalled.":cancelled?"Pending texts cancelled.":"No texts are waiting to be cancelled.";
  if(inProgress&&!cancelled&&!failed)response["message"]="These texts are with the mobile network and cannot be recalled. Check send results.";
  if(failed&&!cancelled)response["error"]=response["message"];
  sendJson(response,failed&&!cancelled?503:200);
}
