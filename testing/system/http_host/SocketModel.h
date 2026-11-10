#pragma once
#include "../http_model/Arduino.h"
#include <deque>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
class NetworkClient {
  struct Socket {int fd;explicit Socket(int value):fd(value){}~Socket(){if(fd>=0)::close(fd);}};
  std::shared_ptr<Socket> socket;
 public:
  NetworkClient()=default;explicit NetworkClient(int fd):socket(std::make_shared<Socket>(fd)){}
  int fd()const{return socket?socket->fd:-1;}
  explicit operator bool()const{return fd()>=0;}
  bool connected()const{return fd()>=0;}
  int setNoDelay(bool value){const int flag=value;return setsockopt(fd(),IPPROTO_TCP,TCP_NODELAY,&flag,sizeof(flag));}
  void stop(){if(socket&&socket->fd>=0){::close(socket->fd);socket->fd=-1;}}
};
class NetworkServer {
 public:
  bool ready=true;operator bool(){return ready;}
  std::deque<NetworkClient> pending;
  NetworkClient accept(){if(pending.empty())return {};auto client=pending.front();pending.pop_front();return client;}
};
