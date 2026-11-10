"""Exercise bounded captive-DNS parsing, including inaccessible packet tails."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include "DnsReply.h"
#include <cassert>
#include <vector>
#include <algorithm>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
std::vector<uint8_t> query(unsigned type=1){return {0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'w','w','w',7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,uint8_t(type),0,1};}
int main(int argc,char**){
 const uint8_t ip[]={192,168,4,1};uint8_t packet[544];
 auto q=query();std::copy(q.begin(),q.end(),packet);
 const auto n=DnsReply::build(packet,q.size(),sizeof(packet),ip);
 assert(n==q.size()+16&&packet[0]==0x12&&packet[1]==0x34&&packet[7]==1);
 assert(std::equal(ip,ip+4,packet+n-4));assert(packet[n-7]==1); // TTL
 for(unsigned type:{28,65}){q=query(type);std::copy(q.begin(),q.end(),packet);assert(DnsReply::build(packet,q.size(),sizeof(packet),ip)==q.size()&&packet[7]==0);}
 q=query(255);std::copy(q.begin(),q.end(),packet);assert(DnsReply::build(packet,q.size(),sizeof(packet),ip)==q.size()+16);
 // EDNS extras are omitted from the reply, counts agree with returned bytes.
 q=query();q[11]=1;q.insert(q.end(),{0,0,41,4,208,0,0,0,0,0,0});std::copy(q.begin(),q.end(),packet);
 assert(DnsReply::build(packet,q.size(),sizeof(packet),ip)==query().size()+16&&packet[11]==0);
 q=query();for(size_t length=0;length<q.size();++length){std::copy(q.begin(),q.end(),packet);assert(!DnsReply::build(packet,length,sizeof(packet),ip));}
 for(unsigned offset:{2,4,6,8,12}){q=query();q[offset]=0xc0;std::copy(q.begin(),q.end(),packet);assert(!DnsReply::build(packet,q.size(),sizeof(packet),ip));}
 q=query();q.back()=3;std::copy(q.begin(),q.end(),packet);assert(!DnsReply::build(packet,q.size(),sizeof(packet),ip));
 q=query();std::copy(q.begin(),q.end(),packet);assert(!DnsReply::build(packet,q.size(),q.size(),ip));
 assert(!DnsReply::build(packet,513,sizeof(packet),ip));
 // No NUL anywhere in a received question. The following page cannot be read.
#ifdef _WIN32
 SYSTEM_INFO info;GetSystemInfo(&info);size_t page=info.dwPageSize;
 auto* pages=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(pages);
 DWORD old;assert(VirtualProtect(pages+page,page,PAGE_NOACCESS,&old));
#else
 size_t page=size_t(sysconf(_SC_PAGESIZE));auto* pages=static_cast<uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));assert(pages!=MAP_FAILED);assert(!mprotect(pages+page,page,PROT_NONE));
#endif
 auto* tail=pages+page-17;std::fill(tail,tail+17,0x41);std::fill(tail,tail+12,0);tail[5]=1;
 if(argc>1){volatile auto* end=std::strchr(reinterpret_cast<const char*>(tail)+12,0);return end?0:1;}
 assert(!DnsReply::build(tail,17,17,ip));
 // Deterministic fuzz at a protected boundary catches both read/write overruns.
 uint32_t seed=123;
 for(unsigned i=0;i<10000;++i){size_t size=i%513;auto* data=pages+page-size;for(size_t j=0;j<size;++j){seed=1664525*seed+1013904223;data[j]=uint8_t(seed>>24);}assert(DnsReply::build(data,size,size,ip)<=size);}
#ifdef _WIN32
 VirtualFree(pages,0,MEM_RELEASE);
#else
 munmap(pages,page*2);
#endif
 std::cout<<"PASS DNS A/ANY/AAAA/HTTPS, EDNS, truncation, labels, compression, class, capacity and 10000 protected-boundary packets\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);source=p/'dns.cpp';source.write_text(code);exe=p/'dns.exe'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'Main_and_Cameras/SmartVest_SJKS01_Main/src/network'),str(source),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 # The SDK's unbounded strchr operation faults on the same packet.
 old=subprocess.run([str(exe),'old-parser'],capture_output=True)
 assert old.returncode!=0,'Expected the old unbounded scan to reach the protected page'
 print('PASS old unbounded DNS scan reproduces a fault on an unterminated packet')
