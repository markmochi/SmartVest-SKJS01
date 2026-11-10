#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/AviWriter.h"
#include <cassert>
#include <iterator>
int main(int argc,char**argv){
 assert(argc==3);
 std::ifstream f(argv[1],std::ios::binary);
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
 AviWriter w;assert(w.begin(argv[2],640,480));
 for(int i=0;i<24;++i)assert(w.frame(bytes.data(),bytes.size()));
 assert(w.count()==24);assert(w.close(3000));
 AviWriter published;assert(published.begin(String(argv[2])+"_system/active",640,480));
 for(int i=0;i<24;++i)assert(published.frame(bytes.data(),bytes.size()));
 assert(published.close(3000,String(argv[2])+"_LOCKED.avi"));
 AviWriter interrupted;assert(interrupted.begin(String(argv[2])+"_interrupted",640,480));
 assert(interrupted.frame(bytes.data(),bytes.size()));interrupted.abort();
 assert(!interrupted.close(1000));
 assert(interrupted.begin(String(argv[2])+"_resumed",640,480));
 assert(interrupted.frame(bytes.data(),bytes.size()));assert(interrupted.close(125));
 AviWriter empty;assert(empty.begin(String(argv[2])+"_empty",640,480));
 assert(!empty.close(1000));
 AviWriter invalid;assert(invalid.begin(String(argv[2])+"_bad",640,480));
 const uint8_t bad[4]={1,2,3,4};assert(!invalid.frame(bad,4));assert(!invalid.close(1000));
}
