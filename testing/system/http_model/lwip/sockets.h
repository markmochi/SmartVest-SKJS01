#pragma once
#include <cerrno>
#include <cstddef>
#include <algorithm>
#include "../SocketModel.h"
#define MSG_PEEK 1
#define MSG_DONTWAIT 2
inline int recv(int fd,void* bytes,size_t length,int flags){
 auto it=hostSockets.find(fd);auto state=it==hostSockets.end()?nullptr:it->second.lock();
 if(!state||!state->alive){errno=ECONNRESET;return -1;}
 if(state->input.empty()){if(state->remoteClosed)return 0;errno=EAGAIN;return -1;}
 const auto count=std::min({length,state->input.size(),flags&MSG_PEEK?state->peekLimit:state->readLimit});
 std::memcpy(bytes,state->input.data(),count);
 if(!(flags&MSG_PEEK))state->input.erase(0,count);
 return int(count);
}
inline int send(int fd,const void* bytes,size_t length,int){
 auto it=hostSockets.find(fd);auto state=it==hostSockets.end()?nullptr:it->second.lock();
 if(!state||!state->alive||state->remoteClosed){errno=ECONNRESET;return -1;}
 if(!state->writable){errno=EAGAIN;return -1;}
  if(state->delayedAck&&!state->noDelay&&state->sendCalls&&int32_t(hostNow-state->ackAt)<0){errno=EAGAIN;return -1;}
  ++state->sendCalls;state->ackAt=hostNow+200;
 const auto count=std::min(length,state->sendLimit);state->output.append(static_cast<const char*>(bytes),count);return int(count);
}
