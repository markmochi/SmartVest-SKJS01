#pragma once
#include <Arduino.h>
#include <lwip/sockets.h>
#include "DnsReply.h"

// Dashboard-task owned: one nonblocking datagram per tick, no AsyncUDP task,
// callback lifetime race, per-query heap buffer or unbounded string parser.
class LocalDnsServer {
 public:
  bool start(uint16_t port,const IPAddress& address){
    stop();for(unsigned i=0;i<4;++i)ip[i]=address[i];
    fd=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0)return false;
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(port);local.sin_addr.s_addr=htonl(INADDR_ANY);
    if(::bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local))!=0){stop();return false;}
    return true;
  }
  void stop(){if(fd>=0){::close(fd);fd=-1;}}
  void processNextRequest(){
    if(fd<0)return;
    uint8_t packet[544];sockaddr_in peer{};socklen_t length=sizeof(peer);
    const int received=::recvfrom(fd,packet,513,MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&peer),&length);
    if(received<=0)return;
    const size_t reply=DnsReply::build(packet,size_t(received),sizeof(packet),ip);
    if(reply)::sendto(fd,packet,reply,MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&peer),length);
  }
 private:
  int fd=-1;uint8_t ip[4]{};
};
