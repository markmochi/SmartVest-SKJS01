#pragma once
#include <cstdint>
// Recorder-task-only state. No radio or filesystem calls, no busy retry loop.
class StorageRetry {
 public:
  bool due(uint32_t now,bool ready,bool paused)const{
    return !ready && !paused && (!waiting || int32_t(now-next)>=0);
  }
  void failed(uint32_t now){next=now+backoff;waiting=true;backoff=backoff>=15000?30000:backoff*2;}
  void healthy(){waiting=false;backoff=5000;}
 private:
  uint32_t next=0,backoff=5000;bool waiting=false;
};
