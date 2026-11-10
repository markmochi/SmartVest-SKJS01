#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <unordered_set>
using PGM_P=const char*;
#define F(value) value
#define FPSTR(value) value
#define log_e(...) ((void)0)
#define log_v(...) ((void)0)
#define log_w(...) ((void)0)
inline uint32_t hostNow=0;
inline uint32_t millis(){return hostNow;}
inline void delay(uint32_t n){hostNow+=n;}
inline void yield(){}
class String;
inline std::unordered_set<const String*> hostStrings;
class String {
 public:
  std::string value;
  inline static bool failReserve=false;
  inline static unsigned failConcatAt=0,concatCalls=0;
  inline static size_t largestReservation=0,maxReservation=std::numeric_limits<size_t>::max();
  String(){hostStrings.insert(this);}
  String(const char* text):String(){value.assign(text?text:"");}
  String(const char* text,size_t count):String(){value.assign(text,count);}
  String(const std::string& text):String(){value.assign(text);}
  String(const String& other):String(){value.assign(other.value);}
  String(String&& other):String(){*this=std::move(other);}
  ~String(){hostStrings.erase(this);}
  String& operator=(const String& other){if(this!=&other)value.assign(other.value);return *this;}
  String& operator=(String&& other){
    if(this==&other)return *this;
    // Match native WString: smaller assignments reuse the destination buffer.
    if(value.capacity()>=other.length())value.assign(other.value);
    else value=std::move(other.value);
    std::string empty;other.value.swap(empty);return *this;
  }
  String& operator=(const char* text){if(text)value.assign(text);else{std::string empty;value.swap(empty);}return *this;}
  template<class T,std::enable_if_t<std::is_integral<T>::value,int> =0>
  String(T number):String(){value=std::to_string(number);}
  bool reserve(size_t count){largestReservation=std::max(largestReservation,count);if(failReserve||count>maxReservation)return false;value.reserve(count);return true;}
  size_t length()const{return value.size();}
  const char* c_str()const{return value.c_str();}
  char charAt(size_t n)const{return n<value.size()?value[n]:0;}
  char operator[](size_t n)const{return value[n];}
  bool concat(const char* text,size_t n){++concatCalls;if(failConcatAt&&concatCalls==failConcatAt)return false;if(failReserve&&value.size()+n>value.capacity())return false;value.append(text,n);return true;}
  bool concat(const char* text){return concat(text,std::strlen(text));}
  bool concat(const String& text){return concat(text.c_str(),text.length());}
  bool concat(char c){return concat(&c,1);}
  bool concat(unsigned char c){return concat(String(unsigned(c)));}
  String& operator+=(const String& text){concat(text);return *this;}
  String& operator+=(const char* text){concat(text);return *this;}
  String& operator+=(char c){concat(c);return *this;}
  void remove(size_t at){if(at<value.size())value.erase(at);}
  void remove(size_t at,size_t n){if(at<value.size())value.erase(at,n);}
  bool equalsIgnoreCase(const String& other)const{if(value.size()!=other.value.size())return false;for(size_t i=0;i<value.size();++i)if(std::tolower(static_cast<unsigned char>(value[i]))!=std::tolower(static_cast<unsigned char>(other.value[i])))return false;return true;}
  int indexOf(char c,int from=0)const{auto n=value.find(c,size_t(from));return n==std::string::npos?-1:int(n);}
  int indexOf(const char* text,int from=0)const{auto n=value.find(text,size_t(from));return n==std::string::npos?-1:int(n);}
  String substring(size_t start,size_t end=std::string::npos)const{return start>=value.size()?String():String(value.substr(start,end==std::string::npos?end:end-start));}
  void trim(){auto start=value.find_first_not_of(" \t\r\n");if(start==std::string::npos){value.clear();return;}value=value.substr(start,value.find_last_not_of(" \t\r\n")-start+1);}
  bool endsWith(const char* suffix)const{size_t n=std::strlen(suffix);return value.size()>=n&&value.compare(value.size()-n,n,suffix)==0;}
  friend bool operator==(const String& a,const String& b){return a.value==b.value;}
  friend bool operator!=(const String& a,const String& b){return !(a==b);}
  friend String operator+(const String& a,const String& b){return a.value+b.value;}
};
inline size_t hostStringHeapCapacity(){size_t bytes=0;for(auto* text:hostStrings)if(text->value.capacity()>std::string().capacity())bytes+=text->value.capacity()+1;return bytes;}
