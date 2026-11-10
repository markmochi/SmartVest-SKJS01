#include <cassert>
#include <iostream>
#include "src/storage/StateStore.h"
#include "src/security/Security.h"
#include "src/security/CameraPairingKeys.h"
#include "allocation_fault.h"
uint32_t clockMs=100;
void seedForgottenAccount(){
 MockNvs::values.clear();MockNvs::writes=0;MockNvs::failSettings=false;
 JsonDocument record;
 record["schema"]=1;record["credentialVersion"]=2;
 record["revision"]=7;record["nextJob"]=21;record["rider"]="Alex";
 record["username"]="rider-old";record["notifyAdded"]=false;
 record["salt"]="0123456789abcdef0123456789abcdef";
 record["passwordHash"]=Security::hashPassword("forgotten-test-password","0123456789abcdef0123456789abcdef");
 record["defaultPassword"]=false;
 record["accountResetRevision"]=Config::RIDER_ACCOUNT_RESET_REVISION==2?1:0;
 record["cameraKeys"][0]=std::string(64,'a');record["cameraKeys"][1]=std::string(64,'b');
 record["contacts"][0]["name"]="Jamie";record["contacts"][0]["phone"]="+639170000001";
 auto job=record["jobs"][0].to<JsonObject>();
 job["id"]=20;job["phone"]="+639170000001";job["text"]="Stored alert";job["kind"]="crash";job["state"]="accepted";job["detail"]="Stored outcome";
 String encoded;serializeJson(record,encoded);MockNvs::values["state"]=encoded;
}
void preserved(const StateStore& store){
 assert(store.data.rider=="Alex");assert(store.data.riderSetupComplete);assert(store.data.notifyAdded==false);
 assert(store.data.contacts[0].name=="Jamie");assert(store.data.contacts[0].phone=="+639170000001");
 assert(store.data.jobs[0].id==20);assert(store.data.jobs[0].text=="Stored alert");assert(store.data.jobs[0].state=="accepted");
 assert(store.data.nextJob==21);assert(store.data.cameraKeys[0]==String(CameraPairing::FRONT_KEY));assert(store.data.cameraKeys[1]==String(CameraPairing::REAR_KEY));
}
int main(){
 seedForgottenAccount();StateStore initial;assert(initial.begin());preserved(initial);
 if(Config::RIDER_ACCOUNT_RESET_REVISION){
  assert(initial.data.riderUsername==Config::DEFAULT_RIDER_USERNAME);
  assert(Security::verifyRiderPassword(Config::DEFAULT_RIDER_PASSWORD,initial.data));
  assert(Security::verifyRiderPassword(Config::LEGACY_DEFAULT_PASSWORD,initial.data));
  assert(!Security::verifyRiderPassword("forgotten-test-password",initial.data));
  assert(initial.data.riderAccountResetRevision==Config::RIDER_ACCOUNT_RESET_REVISION);
  assert(initial.data.revision==8);
 }else{assert(initial.data.riderUsername=="rider-old");assert(initial.data.revision==7);}
 DeviceState custom=initial.data;custom.riderUsername="my-rider";custom.salt=Security::randomHex(16);
 custom.passwordHash=Security::hashPassword("chosen-private-password",custom.salt);custom.defaultPassword=false;++custom.revision;
 assert(initial.commit(custom));const auto writes=MockNvs::writes;
 StateStore restarted;assert(restarted.begin());preserved(restarted);
 assert(restarted.data.riderUsername=="my-rider");assert(Security::verifyRiderPassword("chosen-private-password",restarted.data));
 assert(!Security::verifyRiderPassword(Config::DEFAULT_RIDER_PASSWORD,restarted.data));
 assert(MockNvs::writes==writes); // Same revision does not write/reset again.
 // Repeated identical saves must not consume flash endurance during normal
 // operation. Exercise the real serializer and changed-record comparison.
 for(int i=0;i<1000;++i)assert(restarted.commit(restarted.data));
 assert(MockNvs::writes==writes);
 if(Config::RIDER_ACCOUNT_RESET_REVISION){
  seedForgottenAccount();MockNvs::failSettings=true;StateStore failed;
  assert(!failed.begin());assert(failed.data.riderUsername=="rider-old");preserved(failed);
  assert(Security::verifyRiderPassword("forgotten-test-password",failed.data));
  assert(!Security::verifyRiderPassword(Config::DEFAULT_RIDER_PASSWORD,failed.data));
  assert(MockNvs::values.find("config")==MockNvs::values.end());
  MockNvs::failSettings=false;StateStore retry;assert(retry.begin());preserved(retry);
  assert(retry.data.riderUsername==Config::DEFAULT_RIDER_USERNAME);
 }
 MockNvs::values.clear();StateStore fresh;assert(fresh.begin());
 assert(!fresh.data.riderSetupComplete);assert(fresh.data.riderAccountResetRevision==Config::RIDER_ACCOUNT_RESET_REVISION);
 assert(Security::verifyRiderPassword(Config::DEFAULT_RIDER_PASSWORD,fresh.data));
 assert(fresh.data.cameraKeys[0]==CameraPairing::FRONT_KEY&&fresh.data.cameraKeys[1]==CameraPairing::REAR_KEY);
 assert(fresh.data.cameraKeys[0]!=fresh.data.cameraKeys[1]);
 const String front=fresh.data.cameraKeys[0],rear=fresh.data.cameraKeys[1];
 DeviceState changed=fresh.data;changed.rider="New rider";changed.contacts[0].phone="+639170000001";changed.cameraKeys[0]="bad";changed.cameraKeys[1]="bad";
 assert(fresh.commit(changed));StateStore unfinished;assert(unfinished.begin());assert(!unfinished.data.riderSetupComplete);assert(fresh.data.cameraKeys[0]==front&&fresh.data.cameraKeys[1]==rear);
 MockNvs::values.clear();StateStore erased;assert(erased.begin());
 assert(erased.data.cameraKeys[0]==front&&erased.data.cameraKeys[1]==rear);
 const auto fixedWrites=MockNvs::writes;StateStore nextFirmware;assert(nextFirmware.begin());
 assert(nextFirmware.data.cameraKeys[0]==front&&nextFirmware.data.cameraKeys[1]==rear);
 assert(MockNvs::writes==fixedWrites);
 // Incomplete trees, output reservation failure, and truncated writes must
 // never replace durable settings/history. Exercise the real commit path.
 DeviceState revised=nextFirmware.data;revised.rider="Changed rider";++revised.revision;
 revised.jobs[0].id=1;revised.jobs[0].phone="+639170000001";
 revised.jobs[0].text="Saved history";revised.jobs[0].state="accepted";revised.nextJob=2;
 const auto saved=MockNvs::values;const auto savedWrites=MockNvs::writes;
 for(int failingDocument:{1,2}){
  documentNumber=0;failDocumentAt=failingDocument;
  assert(!nextFirmware.commit(revised));
  assert(!nextFirmware.writable&&MockNvs::writes==savedWrites);
  assert(nextFirmware.data.rider!=revised.rider&&MockNvs::values==saved);
 }
 failDocumentAt=0;documentNumber=0;String::failReserve=true;
 assert(!nextFirmware.commit(revised));String::failReserve=false;
 assert(MockNvs::writes==savedWrites&&MockNvs::values==saved);
 documentNumber=0;String::shortWrite=true;
 assert(!nextFirmware.commit(revised));String::shortWrite=false;
 assert(MockNvs::writes==savedWrites&&MockNvs::values==saved);
 documentNumber=0;assert(nextFirmware.commit(revised));assert(nextFirmware.writable);
 assert(nextFirmware.data.rider==revised.rider);
 std::cout<<"PASS: actual StateStore.cpp; reset revision "<<Config::RIDER_ACCOUNT_RESET_REVISION<<", contacts/history preserved, legacy camera keys migrated to fixed project identity, keys survive erase/reboot, accidental rotation rejected, private change survives reboot, 1000 identical saves cause zero writes, atomic failure/retry, new installation\n";

 DeviceState accounts=nextFirmware.data;accounts.adminUsername="owner";accounts.adminSalt="0123456789abcdef0123456789abcdef";
 accounts.adminHash=Security::hashPassword("Owner-private-2026",accounts.adminSalt);accounts.dataResetPending=true;accounts.riderSetupComplete=true;
 assert(nextFirmware.commit(accounts));StateStore accountReboot;assert(accountReboot.begin());
 assert(accountReboot.data.riderSetupComplete);assert(accountReboot.data.adminUsername=="owner"&&accountReboot.data.dataResetPending);
 assert(Security::verifyAdminPassword("Owner-private-2026",accountReboot.data));
 assert(!Security::verifyAdminPassword(Config::DEVELOPER_PASSWORD,accountReboot.data));
 assert(accountReboot.data.riderUsername==accounts.riderUsername&&accountReboot.data.passwordHash==accounts.passwordHash);
 DeviceState empty=accountReboot.data;empty.contacts[0]=Contact{};assert(accountReboot.commit(empty));StateStore emptyReboot;assert(emptyReboot.begin());assert(emptyReboot.data.riderSetupComplete);
 std::cout<<"PASS administrator credentials and reset intent survive settings reboot without changing rider credentials\n";
}
