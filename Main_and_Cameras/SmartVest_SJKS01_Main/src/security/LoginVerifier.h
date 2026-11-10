#pragma once
#include "Security.h"
#include "../web/HttpMemoryBudget.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Only the password check runs on a temporary worker. Sessions, credentials,
// throttling and HTTP replies remain owned by the dashboard task.
class LoginVerifier {
 public:
  RiderCredentials credentials;
  uint32_t ticket=0,started=0,lastMs=0,maxMs=0;
  uint64_t utc=0;
  bool developer=false,valid=false;
  bool busy()const{return phase.load(std::memory_order_acquire)!=0;}
  bool ready()const{return phase.load(std::memory_order_acquire)==2;}
  __attribute__((noinline)) bool start(const String& user,const String& entered,const RiderCredentials& snapshot){
    if(busy()||!HttpMemoryBudget::available(8192))return false;
    credentials=snapshot;username=user;password=entered;developer=user==snapshot.adminUsername;
    if(username!=user||password!=entered||!matches(snapshot)){reset();return false;}
    started=millis();phase.store(1,std::memory_order_release);
    if(xTaskCreatePinnedToCore(run,"vest_login",4096,this,0,nullptr,1)!=pdPASS){reset();return false;}
    return true;
  }
  __attribute__((noinline)) bool matches(const RiderCredentials& state)const{
    return developer ? credentials.adminUsername==state.adminUsername&&credentials.adminSalt==state.adminSalt&&credentials.adminHash==state.adminHash
      : credentials.riderUsername==state.riderUsername&&credentials.salt==state.salt&&credentials.passwordHash==state.passwordHash;
  }
  __attribute__((noinline)) void reset(){
    clearPassword();username=static_cast<const char*>(nullptr);
    credentials.adminUsername=static_cast<const char*>(nullptr);
    credentials.adminSalt=static_cast<const char*>(nullptr);credentials.adminHash=static_cast<const char*>(nullptr);
    credentials.riderUsername=static_cast<const char*>(nullptr);
    credentials.salt=static_cast<const char*>(nullptr);credentials.passwordHash=static_cast<const char*>(nullptr);
    ticket=0;utc=0;phase.store(0,std::memory_order_release);
  }
 private:
  String username,password;
  std::atomic<uint8_t> phase{0};
  __attribute__((noinline)) void clearPassword(){
    volatile char* bytes=const_cast<char*>(password.c_str());
    for(size_t i=0;i<password.length();++i)bytes[i]=0;
    password=static_cast<const char*>(nullptr);
  }
  static void run(void* context){
    auto& job=*static_cast<LoginVerifier*>(context);
    job.valid=job.developer ? Security::verifyAdminPassword(job.password,job.credentials)
      : job.username==job.credentials.riderUsername&&Security::verifyRiderPassword(job.password,job.credentials)&&
        (Config::ALLOW_DEFAULT_RIDER_LOGIN||!Security::factoryRiderAccount(job.credentials));
    job.clearPassword();
    job.phase.store(2,std::memory_order_release);
    vTaskDelete(nullptr);
  }
};
