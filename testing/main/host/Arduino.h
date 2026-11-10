#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
using byte = uint8_t;
constexpr int SERIAL_8N1 = 0;
constexpr double TWO_PI = 6.28318530717958647692;
inline double radians(double d){return d*3.14159265358979323846/180;}
inline double degrees(double r){return r*180/3.14159265358979323846;}
inline double sq(double d){return d*d;}
extern uint32_t clockMs;
inline uint32_t millis() { return clockMs; }
class String {
 public:
 std::string v;
 String()=default; String(const char* s):v(s?s:""){} String(const std::string& s):v(s){}
 String(unsigned n):v(std::to_string(n)){} String(int n):v(std::to_string(n)){}
 const char* c_str() const { return v.c_str(); }
 size_t length()const{return v.size();}
 void toCharArray(char* p,size_t n)const{if(n){std::strncpy(p,v.c_str(),n-1);p[n-1]=0;}}
 bool reserve(size_t n){v.reserve(n);return true;}
 char operator[](size_t i)const{return v[i];}
 String& operator+=(char c){v+=c;return *this;}
 String& operator+=(const char* s){v+=s;return *this;}
 String& operator+=(const String& s){v+=s.v;return *this;}
 size_t write(uint8_t c){v+=char(c);return 1;}
 size_t write(const uint8_t* data,size_t count){v.append(reinterpret_cast<const char*>(data),count);return count;}
 int indexOf(char c,int from=0)const{auto p=v.find(c,from);return p==std::string::npos?-1:int(p);}
 int indexOf(const char* s,int from=0)const{auto p=v.find(s,from);return p==std::string::npos?-1:int(p);}
 int lastIndexOf(char c)const{auto p=v.rfind(c);return p==std::string::npos?-1:int(p);}
 String substring(size_t from,size_t to=std::string::npos)const{return from>=v.size()?String():String(v.substr(from,to==std::string::npos?to:to-from));}
 bool startsWith(const String& s)const{return v.rfind(s.v,0)==0;}
 bool startsWith(const char* s)const{return v.rfind(s,0)==0;}
 bool endsWith(const String& s)const{return v.size()>=s.v.size()&&v.compare(v.size()-s.v.size(),s.v.size(),s.v)==0;}
 void trim(){auto a=v.find_first_not_of(" \t\r\n");if(a==std::string::npos){v.clear();return;}v=v.substr(a,v.find_last_not_of(" \t\r\n")-a+1);}
 void toUpperCase(){std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return char(std::toupper(c));});}
 friend String operator+(const String&a,const String&b){return a.v+b.v;}
 friend String operator+(const char*a,const String&b){return std::string(a?a:"")+b.v;}
 friend bool operator==(const String&a,const String&b){return a.v==b.v;}
 friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
struct DebugSerial { std::vector<std::string> output; void println(const String& s){output.push_back(s.v);} };
inline DebugSerial Serial;
class IPAddress {
 public:
 uint8_t bytes[4];
 IPAddress(uint8_t a=0,uint8_t b=0,uint8_t c=0,uint8_t d=0):bytes{a,b,c,d}{}
 String toString()const{return std::to_string(bytes[0])+"."+std::to_string(bytes[1])+"."+std::to_string(bytes[2])+"."+std::to_string(bytes[3]);}
 bool operator==(const IPAddress&b)const{return toString()==b.toString();}
 bool operator!=(const IPAddress&b)const{return !(*this==b);}
};
