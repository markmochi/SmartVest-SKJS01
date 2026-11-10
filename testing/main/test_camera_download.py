"""Exercise production download streaming with deterministic transport failures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include "DownloadTransfer.h"
#include "DownloadLease.h"
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
struct Wire{
 std::string input,output;size_t at=0,readSlice=11,writeSlice=37;bool ready=true,blocked=false,failed=false,closed=true;
 bool begin(){closed=false;return !failed;}bool accept(){return ready&&!closed;}
 int read(char* p,size_t n){if(failed)return -2;if(at==input.size())return -1;n=std::min(n,std::min(readSlice,input.size()-at));memcpy(p,input.data()+at,n);at+=n;return int(n);}
 int write(const uint8_t* p,size_t n){if(failed)return -2;if(blocked)return -1;n=std::min(n,writeSlice);output.append(reinterpret_cast<const char*>(p),n);return int(n);}
 void close(){closed=true;}
};
struct Reader{
 std::string bytes;size_t at=0;bool fail=false;
 size_t read(uint8_t* p,size_t n){if(fail)return 0;n=std::min(n,bytes.size()-at);memcpy(p,bytes.data()+at,n);at+=n;return n;}
};
const char* token="abcdef0123456789abcdef0123456789";
std::string request(const char* key=token){return std::string("GET /clip?token=")+key+" HTTP/1.1\r\nHost: 192.168.4.2:8081\r\n\r\n";}
int main(){
 DownloadTransfer<Wire> named;
 const std::string longest=std::string(73,'r')+"_PROTECTED_INTERRUPTION.avi";
 assert(named.begin(99,token,longest.c_str(),100,0,10000));named.stop();
 assert(!named.begin(99,token,std::string(113,'r').c_str(),100,0,10000));
 DownloadTransfer<Wire> transfer;Reader source;source.bytes.resize(16387);for(size_t i=0;i<source.bytes.size();++i)source.bytes[i]=char(i%251);const auto original=source.bytes;
 assert(transfer.begin(1,token,"front_20261103_PROTECTED_INTERRUPTION.avi",source.bytes.size(),100,20000));transfer.socket.input=request();
 for(unsigned n=0;n<20000&&transfer.active();++n)transfer.pump(source,100+n/10);
 assert(transfer.state==3&&transfer.sent==original.size()&&transfer.socket.closed);
 const auto split=transfer.socket.output.find("\r\n\r\n");assert(split!=std::string::npos);assert(transfer.socket.output.substr(split+4)==original);assert(transfer.socket.output.find("Content-Length: 16387")!=std::string::npos);assert(source.bytes==original);assert(transfer.socket.output.find("front_20261103_PROTECTED_INTERRUPTION.avi")!=std::string::npos);
 // Slow readers never hold storage forever; cancellation and later retry work.
 transfer.socket.output.clear();transfer.socket.at=0;source.at=0;assert(transfer.begin(2,token,"front.avi",source.bytes.size(),100,20000));transfer.socket.input=request();transfer.socket.blocked=true;
 for(unsigned n=0;n<200&&transfer.state==1;++n)transfer.pump(source,100+n);transfer.pump(source,6000);assert(transfer.state==4&&source.bytes==original);
 transfer.socket.blocked=false;transfer.socket.at=0;source.at=0;assert(transfer.begin(3,token,"front.avi",source.bytes.size(),100,20000));transfer.socket.input=request("00000000000000000000000000000000");
 for(unsigned n=0;n<200&&transfer.active();++n)transfer.pump(source,100+n);assert(transfer.state==4&&source.at==0);
 transfer.socket.at=0;source.at=0;assert(transfer.begin(4,token,"front.avi",source.bytes.size(),100,20000));transfer.socket.input=std::string("GET /clip?token=")+token+" HTTP/1.1\r\nRange: bytes=123-\r\n\r\n";
 for(unsigned n=0;n<200&&transfer.active();++n)transfer.pump(source,100+n);assert(transfer.state==4&&source.at==0);
 transfer.socket.at=0;source.at=0;assert(transfer.begin(5,token,"front.avi",source.bytes.size(),100,20000));transfer.socket.input=request();
 for(unsigned n=0;n<200&&transfer.state==1;++n)transfer.pump(source,100+n);transfer.socket.failed=true;transfer.pump(source,500);assert(transfer.state==4);transfer.socket.failed=false;
 assert(transfer.begin(6,token,"front.avi",source.bytes.size(),100,1000));transfer.pump(source,1000);assert(!transfer.active());
 assert(!transfer.begin(7,token,"../secret.avi",100,0,1000));
 // Wrap-safe timeouts and bounded duration survive a transfer near millis wrap.
 transfer.socket.at=0;transfer.socket.ready=false;assert(transfer.begin(8,token,"front.avi",100,UINT32_MAX-100,1000));transfer.pump(source,1001);assert(!transfer.active());transfer.socket.ready=true;
 assert(transfer.begin(9,token,"front.avi",100,100,1000000));transfer.pump(source,900100);assert(!transfer.active());
 transfer.socket.failed=true;assert(!transfer.begin(10,token,"front.avi",100,100,10000)&&!transfer.active());transfer.socket.failed=false;
 transfer.socket.at=0;transfer.socket.output.clear();source.at=0;transfer.socket.input=std::string("GET /clip?token=")+token+" HTTP/1.1\r\nUser-Agent: "+std::string(1400,'x')+"\r\n\r\n";
 assert(transfer.begin(11,token,"front.avi",source.bytes.size(),100,20000));for(unsigned n=0;n<20000&&transfer.active();++n)transfer.pump(source,100+n/10);assert(transfer.state==3);assert(transfer.socket.output.substr(transfer.socket.output.find("\r\n\r\n")+4)==original);
 transfer.socket.at=0;source.at=0;source.fail=true;transfer.socket.input=request();assert(transfer.begin(12,token,"front.avi",source.bytes.size(),100,20000));for(unsigned n=0;n<200&&transfer.active();++n)transfer.pump(source,100+n);assert(transfer.state==4&&source.bytes==original);source.fail=false;
 transfer.socket.at=0;transfer.socket.input=std::string(1500,'x');transfer.socket.readSlice=1;assert(transfer.begin(13,token,"front.avi",source.bytes.size(),100,20000));for(unsigned n=0;n<500&&transfer.active();++n)transfer.pump(source,100+n*10);assert(transfer.state==4);
 transfer.socket.ready=false;assert(transfer.begin(14,token,"front.avi",100,100,100000));transfer.pump(source,30101);assert(transfer.state==4);transfer.socket.ready=true;
 DownloadTransfer<Wire> rebooted;assert(!rebooted.active()&&rebooted.id==0);assert(source.bytes==original);
 DownloadLease lease;lease.state=2;lease.started=100;lease.until=20100;assert(lease.allowance(100)==10000);assert(lease.allowance(19100)==0);lease.tick(20100);assert(!lease.active());
 std::cout<<"PASS exact AVI payload, bounded partial writes, backpressure, bad token/range, disconnect, cancellation lease, wrap, duration limit, reboot and original preservation\n";
}
'''
with tempfile.TemporaryDirectory() as temporary:
 p=Path(temporary);source=p/'download.cpp';source.write_text(code);binary=p/'download.exe'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(root/'Main_and_Cameras/SmartVest_SJKS01_Camera/src'),'-I'+str(root/'Main_and_Cameras/SmartVest_SJKS01_Main/src/web'),str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
