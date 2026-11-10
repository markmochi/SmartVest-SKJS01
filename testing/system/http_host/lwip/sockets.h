#pragma once
#include <sys/socket.h>
#include <algorithm>
// Model lwIP exposing just the first queued pbuf to MSG_PEEK, even when
// further request bytes are available to consuming reads. POSIX alone does
// not reproduce this limitation.
inline size_t hostPeekLimit=1;
inline ssize_t hostRecv(int fd,void* bytes,size_t count,int flags){
 return ::recv(fd,bytes,(flags&MSG_PEEK)?std::min(count,hostPeekLimit):count,flags);
}
#define recv hostRecv
