"""Run the production DNS socket lifecycle with bounded transport adapters."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
socket=r'''
#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>
using socklen_t=unsigned;
struct sockaddr{};struct sockaddr_in{int sin_family;uint16_t sin_port;struct{uint32_t s_addr;}sin_addr;};
constexpr int AF_INET=2,SOCK_DGRAM=2,IPPROTO_UDP=17,MSG_DONTWAIT=64,INADDR_ANY=0;
inline uint16_t htons(uint16_t n){return uint16_t((n<<8)|(n>>8));}inline uint32_t htonl(uint32_t n){return n;}
inline bool socketOK=true,bindOK=true,writeOK=true;inline unsigned opened=0,closed=0,reads=0,writes=0;
inline std::vector<uint8_t> input,output;
inline int socket(int family,int type,int protocol){assert(family==AF_INET&&type==SOCK_DGRAM&&protocol==IPPROTO_UDP);++opened;return socketOK?7:-1;}
inline int bind(int fd,const sockaddr* address,socklen_t){assert(fd==7);auto* a=reinterpret_cast<const sockaddr_in*>(address);assert(a->sin_family==AF_INET&&a->sin_port==htons(53));return bindOK?0:-1;}
inline int close(int fd){assert(fd==7);++closed;return 0;}
inline int recvfrom(int fd,void* buffer,size_t limit,int flags,sockaddr*,socklen_t*){assert(fd==7&&flags==MSG_DONTWAIT&&limit==513);++reads;if(input.empty())return -1;size_t size=std::min(limit,input.size());memcpy(buffer,input.data(),size);input.clear();return int(size);}
inline int sendto(int fd,const void* buffer,size_t size,int flags,const sockaddr*,socklen_t){assert(fd==7&&flags==MSG_DONTWAIT);++writes;if(!writeOK)return -1;auto* p=static_cast<const uint8_t*>(buffer);output.assign(p,p+size);return int(size);}
'''
code=r'''
#include "LocalDnsServer.h"
#include <iostream>
std::vector<uint8_t> query(){return {0x12,0x34,1,0,0,1,0,0,0,0,0,0,1,'a',0,0,1,0,1};}
int main(){LocalDnsServer dns;IPAddress ip(192,168,4,1);
 dns.processNextRequest();assert(!reads);socketOK=false;assert(!dns.start(53,ip)&&!closed);
 socketOK=true;bindOK=false;assert(!dns.start(53,ip)&&closed==1);dns.stop();assert(closed==1);
 bindOK=true;assert(dns.start(53,ip));dns.processNextRequest();assert(reads==1&&!writes);
 input=query();dns.processNextRequest();assert(reads==2&&writes==1&&output.size()==35&&output.back()==1);
 input.assign(1024,65);dns.processNextRequest();assert(writes==1&&input.empty());
 input.assign(17,65);dns.processNextRequest();assert(writes==1);
 writeOK=false;input=query();dns.processNextRequest();assert(writes==2);writeOK=true;
 input=query();dns.processNextRequest();assert(writes==3);
 assert(dns.start(53,ip)&&closed==2);dns.stop();assert(closed==3);unsigned count=reads;dns.processNextRequest();assert(reads==count);
 std::cout<<"PASS DNS bind/retry/stop, one nonblocking datagram per tick, malformed/oversized drops, send failure recovery\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'lwip').mkdir();(p/'lwip/sockets.h').write_text(socket)
 (p/'Arduino.h').write_text('#pragma once\n#include <cstdint>\nstruct IPAddress{uint8_t b[4];IPAddress(uint8_t a,uint8_t c,uint8_t d,uint8_t e):b{a,c,d,e}{}uint8_t operator[](unsigned n)const{return b[n];}};\n')
 source=p/'dns.cpp';source.write_text(code);exe=p/'dns.exe'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(p),'-I'+str(root/'Main_and_Cameras/SmartVest_SJKS01_Main/src/network'),str(source),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
