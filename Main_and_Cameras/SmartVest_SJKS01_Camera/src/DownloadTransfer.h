#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <new>
#include <memory>
// Transport returns -1 for retry and -2 for a closed/failed connection.
// The recorder owns this object and the read-only file, so SD never crosses tasks.
template<class Transport> class DownloadTransfer {
 public:
  uint32_t id=0,started=0,lease=0;uint64_t sent=0,size=0;
  uint8_t state=0; // 1 ready, 2 sending, 3 sent, 4 stopped
  Transport socket;
  bool active()const{return state==1||state==2;}
  bool begin(uint32_t job,const char* capability,const char* filename,uint64_t length,uint32_t now,uint32_t deadline){
    stop();id=job;sent=0;size=length;started=progress=now;lease=deadline;
    received=headerAt=bufferAt=bufferSize=0;peer=false;requestDone=false;
    if(!job||strlen(capability)!=32||!length||!filename[0]||strlen(filename)>112||int32_t(deadline-now)<=0)return false;
    for(const char* p=filename;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='.'||*p=='-'))return false;
    memcpy(token,capability,33);
    const int n=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: video/x-msvideo\r\nContent-Length: %llu\r\nContent-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",(unsigned long long)size,filename);
    if(n<=0||size_t(n)>=sizeof(header))return false;headerSize=size_t(n);
    buffer.reset(new(std::nothrow) uint8_t[4096]);
    if(!buffer||!socket.begin()){stop();return false;}state=1;return true;
  }
  void stop(bool complete=false){socket.close();buffer.reset();token[0]=0;state=complete?3:4;}
  template<class Reader> void pump(Reader& file,uint32_t now){
    if(!active())return;
    if(int32_t(now-lease)>=0||uint32_t(now-started)>=900000||uint32_t(now-progress)>=5000+(peer?0:25000)){stop();return;}
    if(!peer){if(!socket.accept())return;peer=true;accepted=progress=now;}
    if(!requestDone){
      if(uint32_t(now-accepted)>=3000){stop();return;}
      const int n=socket.read(request+received,sizeof(request)-1-received);
      if(n==-2){stop();return;}if(n<0)return;
      received+=size_t(n);request[received]=0;
      if(!strstr(request,"\r\n\r\n")){if(received==sizeof(request)-1)stop();return;}
      char first[96];snprintf(first,sizeof(first),"GET /clip?token=%s HTTP/1.",token);
      const size_t prefix=strlen(first);unsigned mismatch=0;
      if(received<prefix+5){stop();return;}
      for(size_t i=0;i<prefix;++i)mismatch|=uint8_t(request[i])^uint8_t(first[i]);
      if(mismatch||(request[prefix]!='0'&&request[prefix]!='1')||request[prefix+1]!='\r'||request[prefix+2]!='\n'){stop();return;}
      // This is a single-use whole-file transfer. A browser retry must request
      // a new permission; never silently append a fresh clip to a partial file.
      for(size_t i=0;i+6<received;++i)if(request[i]=='\n'&&(request[i+1]=='r'||request[i+1]=='R')&&(request[i+2]=='a'||request[i+2]=='A')&&(request[i+3]=='n'||request[i+3]=='N')&&(request[i+4]=='g'||request[i+4]=='G')&&(request[i+5]=='e'||request[i+5]=='E')&&request[i+6]==':'){stop();return;}
      requestDone=true;state=2;progress=now;
    }
    if(headerAt<headerSize){
      const int n=socket.write(reinterpret_cast<const uint8_t*>(header)+headerAt,headerSize-headerAt);
      if(n==-2){stop();return;}if(n>0){headerAt+=size_t(n);progress=now;}return;
    }
    if(bufferAt==bufferSize){
      if(sent==size){stop(true);return;}
      bufferSize=size_t(size-sent>4096?4096:size-sent);bufferAt=0;
      if(file.read(buffer.get(),bufferSize)!=bufferSize){stop();return;}
    }
    const int n=socket.write(buffer.get()+bufferAt,bufferSize-bufferAt);
    if(n==-2){stop();return;}if(n>0){bufferAt+=size_t(n);sent+=size_t(n);progress=now;}
  }
 private:
  std::unique_ptr<uint8_t[]> buffer;
  char request[2048]{},header[384]{},token[33]{};
  size_t received=0,headerSize=0,headerAt=0,bufferAt=0,bufferSize=0;
  uint32_t accepted=0,progress=0;bool peer=false,requestDone=false;
};
