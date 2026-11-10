#pragma once
#include <ArduinoJson.h>
// One requested page per camera. The complete SD catalog never lives on Main.
struct CameraFiles {
  uint32_t id=0,stream=0,at=0;
  uint64_t beforeSession=UINT64_MAX,beforeStart=UINT64_MAX;
  char beforeName[81]{};
  uint8_t operation=0; // 1 normal, 2 protected, 3 delete normal, 4 delete all, 5 download, 7 clear interrupted recordings
  bool pending=false;
  JsonDocument result;
  __attribute__((noinline)) void fail(const char* message) {
    pending=false;result.clear();result["error"]=message;
  }
  __attribute__((noinline)) void expire(uint32_t now) {
    if(!id||uint32_t(now-at)<120000)return;
    if(pending)fail("Request expired. Update the list.");
    else {id=0;result.clear();}
  }
  void write(JsonObject out) const {
    out["id"]=id;out["stream"]=stream;out["operation"]=operation;
    out["beforeSession"]=beforeSession;out["beforeStart"]=beforeStart;out["beforeName"]=beforeName;
  }
};
