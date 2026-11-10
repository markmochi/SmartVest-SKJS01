#pragma once
#include <stdint.h>
#include <string.h>
// Fixed-size exchange between the network and recorder tasks. Only the recorder
// touches SD; paging never collects an unbounded vector of protected recordings.
struct FileRequest {
  uint32_t id=0,stream=0,receivedAt=0;uint8_t operation=0;
  uint64_t beforeSession=UINT64_MAX,beforeStart=UINT64_MAX;
  char beforeName[81]{};
  char token[33]{};
};
struct FileEntry {
  char name[81]{};uint64_t session=0,start=0,utc=0,protectedUtc=0;bool locked=false;uint8_t reason=0; // 1 incident/test, 2 interruption, 3 legacy unknown
};
struct FileReply {
  uint32_t id=0,stream=0,deleted=0,skipped=0,recoveryDeleted=0;uint8_t count=0;bool more=false;
  char error[161]{};FileEntry rows[5]{};
  bool downloadReady=false;uint64_t downloadSize=0;
};
inline bool fileBefore(const FileEntry& a,const FileEntry& b){
  if(a.session!=b.session)return a.session<b.session;
  if(a.start!=b.start)return a.start<b.start;
  return strcmp(a.name,b.name)<0;
}
inline void addFileEntry(FileReply& reply,const FileEntry& entry){
  unsigned at=0;while(at<reply.count&&fileBefore(entry,reply.rows[at]))++at;
  if(reply.count==5)reply.more=true;
  if(at>=5)return;
  if(reply.count<5)++reply.count;
  for(unsigned i=reply.count-1;i>at;--i)reply.rows[i]=reply.rows[i-1];
  reply.rows[at]=entry;
}
