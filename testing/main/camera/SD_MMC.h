#pragma once
#include "FS.h"
#include <cstdio>
#include <filesystem>
struct TestSD {
 std::string root;
 bool failRename=false;unsigned renameCalls=0,failRenameAt=0,removeCalls=0,failRemoveAt=0;uint64_t freeBytes=256ULL*1024*1024;
 uint64_t totalBytes(){return 512ULL*1024*1024;}
 uint64_t usedBytes(){return totalBytes()-freeBytes;}
 std::string path(const String& p)const{return root.empty()?p.v:root+p.v;}
 bool exists(const String& p){return std::filesystem::exists(path(p));}
 File open(const String& p,const char* mode=FILE_READ){return File(path(p),mode==FILE_WRITE);}
 bool rename(const String&a,const String&b){++renameCalls;return !failRename&&renameCalls!=failRenameAt && std::rename(path(a).c_str(),path(b).c_str())==0;}
 bool remove(const String&a){++removeCalls;if(removeCalls==failRemoveAt)return false;const auto n=std::filesystem::exists(path(a))?std::filesystem::file_size(path(a)):0;const bool ok=std::remove(path(a).c_str())==0;if(ok)freeBytes+=n;return ok;}
};
inline TestSD SD_MMC;
