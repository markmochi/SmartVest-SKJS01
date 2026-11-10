#pragma once
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
class DownloadSocket {
 public:
  bool begin(){
    close();listener=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener<0)return false;
    int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(8081);address.sin_addr.s_addr=htonl(INADDR_ANY);
    if(fcntl(listener,F_SETFL,O_NONBLOCK)<0||::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0||::listen(listener,1)<0){close();return false;}return true;
  }
  bool accept(){
    if(peer>=0)return true;
    peer=::accept(listener,nullptr,nullptr);if(peer<0)return false;
    int yes=1;if(fcntl(peer,F_SETFL,O_NONBLOCK)<0){close();return false;}
    setsockopt(peer,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    ::close(listener);listener=-1;return true;
  }
  int read(char* bytes,size_t size){return result(::recv(peer,bytes,size,MSG_DONTWAIT));}
  int write(const uint8_t* bytes,size_t size){return result(::send(peer,bytes,size,MSG_DONTWAIT));}
  void close(){if(peer>=0)::close(peer);if(listener>=0)::close(listener);peer=listener=-1;}
 private:
  int listener=-1,peer=-1;
  static int result(int n){return n>0?n:n==0?-2:errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?-1:-2;}
};
