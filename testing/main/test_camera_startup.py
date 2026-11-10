"""Exercise actual camera startup allocation and retry gates without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]/'Main_and_Cameras'
app = (root / 'SmartVest_SJKS01_Camera/src/CameraApp.cpp').read_text(encoding='utf8')
services = app[app.index('bool startServices() {'):app.index('bool initCamera() {')]
tick = app[app.index('void CameraApp::tick() {') + len('void CameraApp::tick() {'):]
gate = tick[:tick.index('  Command c;')]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <iostream>
struct Queue {};
using QueueHandle_t=Queue*;
QueueHandle_t commands=nullptr,acks=nullptr,wifiNotices=nullptr;
struct Command {uint32_t boot,sequence,age,requestMs;uint64_t received;bool known,test,valid;};
struct Ack {uint32_t boot,sequence;uint8_t stage;};
struct WifiNotice {uint32_t at;uint8_t kind,reason;};
bool servicesReady=false,startupAllowed=false;
uint32_t startupRetryAt=0,cameraRetryAt=0,now=0;
unsigned allocation=0,failAllocation=0,deletions=0,taskAttempts=0,taskStarts=0,recorderEntries=0,publishes=0;
bool taskAvailable=true,cameraAvailable=true,camera=false;
unsigned cameraInitializations=0,cameraReleases=0;
std::set<Queue*> live;
std::string lastFault;
constexpr int pdPASS=1;
uint32_t millis(){return now;}
void delay(unsigned){}
void publish(){++publishes;}
void emitLog(const char*,const char*,const char*){}
struct {void println(const char*){}} Serial;
QueueHandle_t xQueueCreate(unsigned count,size_t bytes){
 ++allocation;
 assert((count==4&&bytes==sizeof(Command))||(count==16&&(bytes==sizeof(Ack)||bytes==sizeof(WifiNotice))));
 if(allocation==failAllocation)return nullptr;
 auto* queue=new Queue;assert(live.insert(queue).second);return queue;
}
void vQueueDelete(QueueHandle_t queue){assert(live.erase(queue)==1);++deletions;delete queue;}
void networkTask(void*){}
bool initCamera(){++cameraInitializations;return cameraAvailable;}
void esp_camera_deinit(){assert(camera);++cameraReleases;}
int xTaskCreatePinnedToCore(void(*entry)(void*),const char* name,unsigned stack,void* parameter,unsigned priority,void* handle,unsigned core){
 assert(entry==networkTask&&!strcmp(name,"camera_link")&&stack==8192&&!parameter&&priority==1&&!handle&&core==0);
 assert(commands&&acks&&wifiNotices&&live.size()==3&&cameraInitializations);++taskAttempts;
 if(!taskAvailable)return 0;
 ++taskStarts;return pdPASS;
}
''' + services + '\nvoid startupTick(){\n' + gate + r'''
 ++recorderEntries;
}
void clear(){
 for(auto* queue:{&commands,&acks,&wifiNotices})if(*queue){vQueueDelete(*queue);*queue=nullptr;}
 assert(live.empty());servicesReady=startupAllowed=false;startupRetryAt=cameraRetryAt=now=0;
 allocation=failAllocation=deletions=taskAttempts=taskStarts=recorderEntries=publishes=0;
 taskAvailable=cameraAvailable=true;camera=false;cameraInitializations=cameraReleases=0;lastFault.clear();
}
int main(){
 // Every partial queue failure is cleaned up before recorder admission.
 for(unsigned failed=1;failed<=3;++failed){
  clear();startupAllowed=true;failAllocation=failed;
  startupTick();assert(!servicesReady&&!commands&&!acks&&!wifiNotices&&live.empty());
  assert(allocation==3&&deletions==2&&taskAttempts==0&&cameraInitializations==0&&recorderEntries==0&&startupRetryAt==1000);
  now=999;startupTick();assert(allocation==3&&recorderEntries==0);
  failAllocation=0;now=1000;startupTick();
  assert(servicesReady&&live.size()==3&&taskStarts==1&&recorderEntries==1&&cameraRetryAt==1000&&lastFault.empty());
  startupTick();assert(allocation==6&&taskStarts==1&&recorderEntries==2);
 }
 // Task creation failure also releases every queue, then recovers on retry.
 clear();startupAllowed=true;taskAvailable=false;startupTick();
 assert(!servicesReady&&live.empty()&&deletions==3&&taskAttempts==1&&!taskStarts&&!recorderEntries&&!camera&&cameraReleases==1);
 now=999;startupTick();assert(taskAttempts==1);
 taskAvailable=true;now=1000;startupTick();assert(servicesReady&&taskStarts==1&&recorderEntries==1&&camera&&cameraInitializations==2);
 // Camera initialization failure still permits networking for status/recovery.
 clear();startupAllowed=true;cameraAvailable=false;startupTick();assert(servicesReady&&taskStarts==1&&!camera&&recorderEntries==1);
 // Invalid role/key configuration never starts services. Retry deadlines wrap.
 clear();startupTick();assert(!allocation&&!taskAttempts&&!recorderEntries);
 startupAllowed=true;now=0xffffff00U;failAllocation=1;assert(!startServices());
 assert(startupRetryAt==744);now=743;startupTick();assert(allocation==3);
 failAllocation=0;now=744;startupTick();assert(servicesReady&&taskStarts==1&&recorderEntries==1);
 clear();
 std::cout<<"PASS actual camera startup: each queue failure, partial cleanup, task failure, bounded retry, recovery, invalid configuration and clock wrap; recorder admission requires all services\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'startup.cpp'
    source.write_text(code, encoding='utf8')
    binary = Path(tmp) / 'startup'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
