#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Captive DNS: one ordinary IN question; never scan outside the datagram.
// Compression in a question, truncated labels and oversized packets are dropped.
namespace DnsReply {
inline size_t build(uint8_t* packet,size_t size,size_t capacity,const uint8_t ip[4]) {
  if(size<17||size>512||capacity<size||packet[2]&0xf8||packet[3]&0x0f||
     packet[4]!=0||packet[5]!=1||packet[6]||packet[7]||packet[8]||packet[9])return 0;
  size_t at=12;
  for(;;){
    if(at>=size)return 0;
    const uint8_t length=packet[at++];
    if(!length)break;
    if(length>63||length>size-at)return 0;
    at+=length;
    if(at-12>254)return 0;
  }
  if(at-12>255||size-at<4)return 0;
  const unsigned type=(unsigned(packet[at])<<8)|packet[at+1];
  if(packet[at+2]!=0||packet[at+3]!=1)return 0;
  at+=4;
  const bool address=type==1||type==255;
  if(address&&capacity-at<16)return 0;
  packet[2]=uint8_t(0x84|(packet[2]&1));packet[3]=0;
  packet[6]=0;packet[7]=address?1:0;
  packet[8]=packet[9]=packet[10]=packet[11]=0;
  if(address){
    const uint8_t answer[]={0xc0,0x0c,0,1,0,1,0,0,0,1,0,4};
    std::memcpy(packet+at,answer,sizeof(answer));at+=sizeof(answer);
    std::memcpy(packet+at,ip,4);at+=4;
  }
  return at;
}
}
