#pragma once
#if defined(SJKS_HTTP_REAL_SOCKET)
#include "../http_host/SocketModel.h"
#else
#include "Arduino.h"
#include <deque>
#include <memory>
#include <unordered_map>
struct HostSocket {
  bool alive=true,writable=true,remoteClosed=false,noDelay=false,delayedAck=false;
  uint32_t ackAt=0;size_t sendCalls=0;
  size_t sendLimit=1460,readLimit=512,peekLimit=1;
  std::string input,output;
};
inline size_t hostOwners=0,hostPeakOwners=0;
inline int hostNextFd=1;
inline std::unordered_map<int,std::weak_ptr<HostSocket>> hostSockets;
class NetworkClient {
  struct Owner {
    int fd;std::shared_ptr<HostSocket> state;
    Owner(int descriptor,std::shared_ptr<HostSocket> socket):fd(descriptor),state(std::move(socket)){++hostOwners;hostPeakOwners=std::max(hostPeakOwners,hostOwners);}
    ~Owner(){state->alive=false;hostSockets.erase(fd);--hostOwners;}
  };
  std::shared_ptr<Owner> owner;
 public:
  NetworkClient()=default;
  explicit NetworkClient(const std::shared_ptr<HostSocket>& socket){int descriptor=hostNextFd++;hostSockets[descriptor]=socket;owner=std::make_shared<Owner>(descriptor,socket);}
  int fd()const{return owner?owner->fd:-1;}
  explicit operator bool()const{return owner&&owner->state->alive;}
  bool connected()const{return owner&&owner->state->alive&&!owner->state->remoteClosed;}
  int setNoDelay(bool value){if(!owner)return -1;owner->state->noDelay=value;return 0;}
  void stop(){if(owner)owner->state->alive=false;}
};
class NetworkServer {
 public:
  bool ready=true;operator bool(){return ready;}
  std::deque<NetworkClient> pending;
  NetworkClient accept(){if(pending.empty())return {};auto c=pending.front();pending.pop_front();return c;}
};
#endif
