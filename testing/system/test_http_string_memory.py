"""Check HTTP text cleanup with the actual installed ESP32 String methods."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
from http_core import method

root=Path(__file__).resolve().parents[2]
configured=os.environ.get('SJKS_ESP32_CORE')
assert configured,'Set SJKS_ESP32_CORE to the installed pinned ESP32 core 3.3.12 folder'
core=Path(configured)
native=(core/'cores/esp32/WString.cpp').read_text(encoding='utf8')
transport=(root/'Main_and_Cameras/SmartVest_SJKS01_Main/src/web/ReadyWebServer.h').read_text(encoding='utf8')
signatures=['String::String(const char *cstr)', 'String::String(const String &value)',
 'String::String(String &&rval)', 'String::~String()', 'inline void String::init(void)',
 'void String::invalidate(void)', 'bool String::reserve(unsigned int size)',
 'bool String::changeBuffer(unsigned int maxStrLen)', 'String &String::copy(const char *cstr, unsigned int length)',
 'void String::move(String &rhs)', 'String &String::operator=(const String &rhs)',
 'String &String::operator=(String &&rval)', 'String &String::operator=(const char *cstr)']
methods='\n\n'.join(method(native,s) for s in signatures)
if 'static void releaseText(' in transport:
    cleanup=method(transport,'static void releaseText(')
else:
    assert 's.output=String();' in method(transport,'void closeSlot(')
    cleanup='static void releaseText(String& value){value=String();}'

verifier=(root/'Main_and_Cameras/SmartVest_SJKS01_Main/src/security/LoginVerifier.h').read_text('utf8')
login_cleanup=method(verifier,'__attribute__((noinline)) void reset()')+'\n'+method(verifier,'__attribute__((noinline)) void clearPassword()')

with tempfile.TemporaryDirectory() as temporary:
    folder=Path(temporary)
    (folder/'WString.h').write_bytes((core/'cores/esp32/WString.h').read_bytes())
    (folder/'pgmspace.h').write_text('#pragma once\n#include <cstring>\n#define memcpy_P memcpy\n',encoding='utf8')
    code=r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <iostream>
#include "WString.h"
namespace Heap {
std::map<void*,size_t> allocations;size_t used=0,peak=0;
void* allocate(size_t n){void* p=std::malloc(n);if(p){allocations[p]=n;used+=n;peak=std::max(peak,used);}return p;}
void release(void* p){if(!p)return;used-=allocations.at(p);allocations.erase(p);std::free(p);}
void* resize(void* p,size_t n){size_t old=p?allocations.at(p):0;void* q=std::realloc(p,n);if(q){if(p)allocations.erase(p);allocations[q]=n;used=used-old+n;peak=std::max(peak,used);}return q;}
}
#define malloc Heap::allocate
#define free Heap::release
#define realloc Heap::resize
'''+methods+r'''
#undef malloc
#undef free
#undef realloc
'''+cleanup+r'''
struct LoginBuffers {
 String username,password;
 struct {String adminUsername,adminSalt,adminHash,riderUsername,salt,passwordHash;}credentials;
 uint32_t ticket=1;uint64_t utc=1;std::atomic<uint8_t> phase{2};
'''+login_cleanup+r'''
};
int main(){
 {String value;assert(value.reserve(20000));value=String();
  assert(value.length()==0&&Heap::used>=20000);
  std::cout<<"CONFIRMED native ESP32 String empty assignment retains "<<Heap::used<<" allocated bytes\n";
 }
 assert(Heap::used==0);
 for(unsigned cycle=0;cycle<1000;++cycle){
  String responses[4];for(auto& value:responses)assert(value.reserve(20000));
  assert(Heap::used>=80000);
  for(auto& value:responses)releaseText(value);
  if(Heap::used){std::cerr<<"FAIL completed HTTP slots retain "<<Heap::used<<" bytes despite empty text\n";return 1;}
  for(auto& value:responses){value="healthy response";releaseText(value);}
  assert(Heap::used==0);
 }
 {
  String body(std::string(16000,'x').c_str()),slot;
  const char* original=body.c_str();const size_t before=Heap::used;
  slot=std::move(body);
  assert(slot.c_str()==original&&slot.length()==16000&&body.length()==0&&Heap::used==before);
  releaseText(slot);assert(Heap::used==0);
  std::cout<<"PASS native 16000-byte response ownership transfer: identical buffer, no extra heap allocation\n";
 }
 {
  LoginBuffers job;
  for(unsigned n=0;n<1000;++n){
   job.username=std::string(32,'u').c_str();job.password=std::string(64,'p').c_str();
   for(String* field:{&job.credentials.adminUsername,&job.credentials.adminSalt,&job.credentials.adminHash,&job.credentials.riderUsername,&job.credentials.salt,&job.credentials.passwordHash})*field=std::string(64,'c').c_str();
   assert(Heap::used>0);job.reset();assert(Heap::used==0&&job.ticket==0&&job.utc==0&&job.phase.load()==0);
  }
  std::cout<<"PASS production login cleanup with native ESP32 String: 1000 credential/password cycles retain zero buffer bytes\\n";
 }
 assert(Heap::allocations.empty());
 std::cout<<"PASS production HTTP text release: four large slots, 1000 cycles, all allocated buffer bytes released\n";
}
'''
    (folder/'test.cpp').write_text(code,encoding='utf8')
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG','-I'+str(folder),str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)
    output=Path(os.environ.get('SJKS_TEST_OUTPUT',root/'testing/results'))
    output.mkdir(parents=True,exist_ok=True)
    (output/'native-string-source.json').write_text(json.dumps({'core':str(core),'WString.cpp':hashlib.sha256((core/'cores/esp32/WString.cpp').read_bytes()).hexdigest(),'selectedMethodsSha256':hashlib.sha256(methods.encode()).hexdigest()},indent=2),encoding='utf8')
