#pragma once
#include "../host/Arduino.h"
#include <fstream>
#include <memory>
#include <filesystem>
#include <limits>
inline size_t hostWriteBudget=std::numeric_limits<size_t>::max();
constexpr char FILE_WRITE[]="w",FILE_READ[]="r";
class File {
 std::shared_ptr<std::fstream> f;
 bool writable=false,directory=false;std::string filePath;mutable std::string shortName;
 std::shared_ptr<std::vector<std::string>> children;
 std::shared_ptr<size_t> child;
 public:
 File()=default;
 File(const std::string& p,bool w):f(std::make_shared<std::fstream>()),writable(w),filePath(p){
   if(std::filesystem::is_directory(p)){directory=true;children=std::make_shared<std::vector<std::string>>();child=std::make_shared<size_t>(0);for(const auto& entry:std::filesystem::directory_iterator(p))children->push_back(entry.path().string());return;}
   f->open(p,std::ios::binary|(w?(std::ios::out|std::ios::in|std::ios::trunc):std::ios::in));
 }
 explicit operator bool()const{return directory||(f && f->is_open());}
 bool isDirectory()const{return directory;}
 const char* name()const{shortName=std::filesystem::path(filePath).filename().string();return shortName.c_str();}
 File openNextFile(){while(directory&&*child<children->size()){File entry((*children)[(*child)++],false);if(entry)return entry;}return File();}
 size_t write(uint8_t c){return write(&c,1);}
 int read(){return f->get();}
 size_t write(const uint8_t* p,size_t n){if(!f||!f->is_open())return 0;n=std::min(n,hostWriteBudget);hostWriteBudget-=n;f->write((const char*)p,n);return *f?n:0;}
 size_t position(){return writable?size_t(f->tellp()):size_t(f->tellg());}
 bool seek(size_t n){f->clear();if(writable)f->seekp(n);else f->seekg(n);return bool(*f);}
 size_t size(){if(!f||!f->is_open())return 0;auto p=f->tellg();f->clear();f->seekg(0,std::ios::end);auto n=f->tellg();f->seekg(p);return size_t(n);}
 size_t available(){return size()-position();}
 size_t read(uint8_t* p,size_t n){f->read((char*)p,n);return f->gcount();}
 size_t readBytes(char* p,size_t n){return read(reinterpret_cast<uint8_t*>(p),n);}
 void flush(){if(f)f->flush();}
 void close(){directory=false;if(f)f->close();}
};
