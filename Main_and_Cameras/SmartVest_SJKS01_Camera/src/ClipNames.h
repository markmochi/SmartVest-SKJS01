#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
namespace ClipNames {
inline uint64_t startUtc(uint64_t anchor,uint64_t at,uint64_t now,uint64_t start) {
  if(anchor<1577836800000ULL || anchor>=4102444800000ULL ||
     now<at || now<start || now-at>=86400000ULL)return 0;
  const uint64_t elapsed=now-start,utcNow=anchor+now-at;
  if(utcNow<elapsed+1577836800000ULL)return 0;
  const uint64_t result=utcNow-elapsed;
  return result<4102444800000ULL?result:0;
}
inline bool validStem(const char* s){
  if(!s || strlen(s)>80 || strncmp(s,"/clips/",7))return false;
  const char* p=s+7;
  if(!strncmp(p,"c_",2))p+=2;
  else if(!strncmp(p,"front_",6))p+=6;
  else if(!strncmp(p,"rear_",5))p+=5;
  else return false;
  if(!*p)return false;
  for(;*p;++p)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||*p=='_'||*p=='-'))return false;
  return true;
}
inline void make(char* out,size_t size,const char* role,uint64_t session,uint32_t number,uint64_t utcMs,int offsetMinutes){
  if(utcMs>=1577836800000ULL && utcMs<4102444800000ULL){
    time_t local=time_t(utcMs/1000)+offsetMinutes*60;struct tm t{};
#ifdef _WIN32
    gmtime_s(&t,&local); // Native Windows verification runner.
#else
    gmtime_r(&local,&t);
#endif
    snprintf(out,size,"/clips/%s_%04d-%02d-%02d_%02d-%02d-%02d",role,t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec);
  }else snprintf(out,size,"/clips/%s_time-unknown_session-%llu_clip-%lu",role,(unsigned long long)session,(unsigned long)number);
}
}
