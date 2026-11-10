#include "EmergencyCoordinator.h"
#include <iostream>
#include <cassert>
uint32_t clockMs=1000;
CrashProtocol::Frame frame(uint32_t seq,bool test=true){
 CrashProtocol::Frame f;f.boot=42;f.sequence=seq;
 f.type=test?CrashProtocol::Type::Test:CrashProtocol::Type::Crash;return f;
}
int main(){
 StateStore s;NEO6MService gps;s.data.contacts[0].phone="+6391234567890";
 EmergencyCoordinator e(s,gps);e.begin();assert(e.writable&&e.readyForCrash());
 assert(e.canDownloadFrom(0)&&e.canDownloadFrom(1));
 assert(!e.canDownloadFrom(-1)&&!e.canDownloadFrom(2));
 s.writable=false;assert(!e.readyForCrash());s.writable=true;assert(e.readyForCrash());
 assert(e.accept(frame(1),true,0,true));
 assert(!e.canDownloadFrom(0)&&!e.canDownloadFrom(1));
 JsonDocument j;e.writeJson(j.to<JsonObject>());
 assert(j["incidents"][0]["recipients"].size()==0);
 assert(e.accept(frame(1),true,0,true)); // replay has one record
 e.writeJson(j.to<JsonObject>());assert(j["incidents"].size()==1);
 assert(!e.cameraAck(-1,42,1,"protected"));assert(!e.cameraAck(0,42,1,"bogus"));
 assert(e.cameraAck(0,42,1,"received"));
 assert(e.cameraAck(0,42,1,"protected"));
 assert(e.canDownloadFrom(0)&&!e.canDownloadFrom(1)&&!e.canResetData());
 assert(!e.cameraAck(0,42,1,"received")); // monotonic
 assert(e.cameraAck(0,42,1,"protected"));
 JsonDocument cmd;e.cameraCommand(0,cmd.to<JsonObject>());assert(cmd["command"].isNull());
 e.cameraCommand(1,cmd.to<JsonObject>());assert(cmd["command"]["sequence"]==1);
 assert(e.accept(frame(2),true,0,true));assert(e.accept(frame(3),true,0,true));
 assert(!e.readyForCrash()&&e.readyForCrash(false));
 assert(!e.accept(frame(4),true,0,true)); // one real incident slot reserved
 assert(e.accept(frame(4,false),true,0));
 assert(!e.accept(frame(5,false),true,0,true)); // cameraOnly cannot suppress real SMS
 failWrite=true;assert(!e.cameraAck(1,42,1,"protected"));
 assert(!e.cameraAck(1,42,1,"protected"));
 failWrite=false;clockMs+=5000;e.tick();assert(e.cameraAck(1,42,1,"protected"));
 EmergencyCoordinator reboot(s,gps);reboot.begin();
 reboot.cameraCommand(0,cmd.to<JsonObject>());
 assert(cmd["command"]["triggerTimeKnown"]==false);
 assert(cmd["command"]["elapsedSinceDetectionMs"].isNull());
 clockMs+=Config::CAMERA_COMMAND_DEADLINE_MS;reboot.tick();
 reboot.writeJson(j.to<JsonObject>());
 assert(j["incidents"][1]["front"]=="pending");
 assert(reboot.readyForCrash());

 // New observations survive reboot; later context changes never rewrite an alert.
 blob.clear();StateStore records;records.data.contacts[0].phone="+639170000001";records.data.contacts[0].name="Jamie";
 EmergencyCoordinator captured(records,gps);captured.begin();
 Incidents::Evidence context;context.utcMs=1793188862000ULL;context.timeSource=2;context.gps=1;context.modem=1;context.camera[0]=2;context.camera[1]=1;context.battery=2;context.batteryPercent=60;
 captured.setEvidenceContext(context);auto real=frame(7,false);real.flags=CrashProtocol::ImuReady|CrashProtocol::PiezoReady|CrashProtocol::Armed;real.accelerationMg=2800;real.rotationDps=156;real.uptimeMs=100;
 assert(captured.accept(real,true,200));const auto withoutImpact=blob;
 assert(!captured.canDownloadFrom(0));
 captured.observeImpact(43,100,1);captured.observeImpact(42,1000,1);
 assert(blob==withoutImpact);captured.observeImpact(42,100,1);
 auto original=blob;Incidents::Journal saved;assert(Incidents::decode(blob.data(),saved));
 assert(std::strcmp(saved.entries[0].recipients[0].name,"Jamie")==0);
 context.utcMs+=5000;context.modem=2;captured.setEvidenceContext(context);
 assert(captured.accept(real,true,0));assert(blob==original);
 EmergencyCoordinator restored(records,gps);restored.begin();j.clear();restored.writeJson(j.to<JsonObject>());
 auto event=j["incidents"][0];assert(event["occurredAtUtcMs"].as<uint64_t>()==1793188861800ULL);
 assert(event["movement"]["accelerationG"].as<float>()==2.8f);assert(event["mobile"]["state"]=="not_detected");
 assert(event["recipients"][0]["name"]=="Jamie");assert(event["impact"]["front"]==true);
 assert(event["cameraPresence"]["rear"]=="not_detected");
 // A valid v1 journal migrates without losing recipients, pending work or identities.
 std::vector<uint8_t> legacy(blob.begin(),blob.begin()+Incidents::LEGACY_ENCODED_SIZE);legacy[2]=1;
 CrashProtocol::put16(legacy.data()+legacy.size()-2,CrashProtocol::crc16(legacy.data(),legacy.size()-2));
 Incidents::Journal decoded;assert(Incidents::decode(legacy.data(),decoded,legacy.size()));assert(decoded.entries[0].boot==42&&decoded.entries[0].count==1);
 assert(!decoded.entries[0].evidence.utcMs&&!decoded.entries[0].recipients[0].name[0]);
 blob=legacy;EmergencyCoordinator migrated(records,gps);migrated.begin();assert(migrated.writable&&blob.size()==Incidents::ENCODED_SIZE);
 j.clear();migrated.writeJson(j.to<JsonObject>(),false);assert(j["incidents"].size()==1);assert(j["incidents"][0]["occurredAtUtcMs"].isNull());assert(j["incidents"][0]["message"].isNull());
 auto bad=legacy;bad.back()^=1;blob=bad;EmergencyCoordinator corrupt(records,gps);corrupt.begin();assert(!corrupt.isLoaded()&&blob==bad);
 uint8_t bytes[Incidents::ENCODED_SIZE+2]={};bytes[Incidents::ENCODED_SIZE]=0xa5;bytes[Incidents::ENCODED_SIZE+1]=0x5a;
 Incidents::encode(decoded,bytes);assert(bytes[Incidents::ENCODED_SIZE]==0xa5&&bytes[Incidents::ENCODED_SIZE+1]==0x5a);assert(Incidents::decode(bytes,decoded));
 // Durable cancellation must survive reboot and cannot be undone by old history.
 blob=original;EmergencyCoordinator cancelRecords(records,gps);cancelRecords.begin();
 assert(cancelRecords.cameraAck(0,42,7,"protected"));
 assert(cancelRecords.canDownloadFrom(0)&&!cancelRecords.canDownloadFrom(1)&&!cancelRecords.canResetData());
 assert(cancelRecords.pendingTextMask(42,7,0));
 records.writable=false;assert(!cancelRecords.canDownloadFrom(0));records.writable=true;
 records.data.dataResetPending=true;assert(!cancelRecords.canDownloadFrom(0));records.data.dataResetPending=false;
 const auto mask=cancelRecords.pendingTextMask(42,7,0);assert(mask);
 failWrite=true;assert(!cancelRecords.cancelPendingTexts(mask));
 failWrite=false;clockMs+=5000;cancelRecords.tick();assert(cancelRecords.pendingTextMask(42,7,0)==mask);
 assert(cancelRecords.cancelPendingTexts(mask));
 EmergencyCoordinator cancelledAfterReboot(records,gps);cancelledAfterReboot.begin();
 j.clear();cancelledAfterReboot.writeJson(j.to<JsonObject>());assert(j["incidents"][0]["recipients"][0]["state"]=="cancelled");
 assert(!cancelledAfterReboot.pendingTextMask(42,7,0));
 auto& oldJob=records.data.jobs[0];oldJob.id=500;oldJob.eventBoot=42;oldJob.eventSequence=7;oldJob.recipientIndex=0;oldJob.state="queued";
 cancelledAfterReboot.tick();j.clear();cancelledAfterReboot.writeJson(j.to<JsonObject>());assert(j["incidents"][0]["recipients"][0]["state"]=="cancelled");
 assert(oldJob.state=="cancelled"); // Stale ordinary history cannot occupy the sending queue again.


 assert(!cancelledAfterReboot.canResetData());
 assert(cancelledAfterReboot.cameraAck(0,42,7,"protected"));assert(cancelledAfterReboot.cameraAck(1,42,7,"protected"));
 assert(cancelledAfterReboot.canResetData());
 records.data.dataResetPending=true;failWrite=true;
 assert(!cancelledAfterReboot.finishDataReset());assert(records.data.dataResetPending&&!cancelledAfterReboot.readyForCrash());
 assert(!cancelledAfterReboot.accept(real,true,0));
 failWrite=false;records.failCommit=true;clockMs+=5001;cancelledAfterReboot.tick();assert(records.data.dataResetPending);
 records.failCommit=false;EmergencyCoordinator resetReboot(records,gps);resetReboot.begin();
 assert(!records.data.dataResetPending);j.clear();resetReboot.writeJson(j.to<JsonObject>());assert(j["incidents"].size()==0);
 const auto afterReset=blob;assert(resetReboot.accept(real,true,0));assert(blob==afterReset);
 j.clear();resetReboot.writeJson(j.to<JsonObject>());assert(j["incidents"].size()==0);
 std::cout<<"PASS reset refuses pending camera work; storage failure remains pending; reboot completes reset; tombstones prevent old crash replay\n";
}
