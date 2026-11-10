"""Execute the camera retry block with failed/successful initialization fixtures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
source=(root/'SmartVest_SJKS01_Camera/src/CameraApp.cpp').read_text(encoding='utf8')
start=source.index('  if(!camera&&!paused&&!downloadTransfer.active()&&int32_t(millis()-cameraRetryAt)>=0)')
block=source[start:source.index('  serviceStorage();',start)]
pre=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <iostream>
uint32_t now=0,cameraRetryAt=0,cameraBackoff=1000;
uint32_t millis(){return now;}
bool camera=false,paused=false,sd=false,running=false,nextInit=false,canOpen=true;
struct {bool busy=false;bool active(){return busy;}}downloadTransfer;
std::string lastFault;unsigned attempts=0,deinitializations=0,opens=0,errors=0;
void esp_camera_deinit(){++deinitializations;}
bool initCamera(){++attempts;return nextInit;}
bool openClip(){++opens;return running=canOpen;}
void failStorage(const char*){++errors;sd=running=false;}
void publish(){}template<class... T>void emitLog(T...){}
void tick(){
'''
checks=r'''
}
int main(){
 tick();assert(attempts==1&&cameraRetryAt==1000&&cameraBackoff==2000);
 for(uint32_t gap:{1000U,2000U,4000U,8000U,16000U,30000U,30000U}){
  const auto count=attempts;now=cameraRetryAt-1;tick();assert(attempts==count);
  now=cameraRetryAt;tick();assert(attempts==count+1);assert(cameraRetryAt-now==std::min(30000U,gap*2));
 }
 downloadTransfer.busy=true;now=cameraRetryAt;const auto held=attempts;tick();assert(attempts==held);downloadTransfer.busy=false;
 paused=true;now=cameraRetryAt;const auto count=attempts;tick();assert(attempts==count);
 paused=false;sd=true;nextInit=true;tick();assert(camera&&running&&opens==1&&cameraBackoff==1000);
 tick();assert(opens==1&&attempts==count+1);
 camera=false;canOpen=false;tick();assert(camera&&!sd&&!running&&errors==1);
 camera=false;nextInit=false;now=0xfffffff0U;cameraRetryAt=now;tick();const auto retry=cameraRetryAt;
 now=retry-1;const auto before=attempts;tick();assert(attempts==before);now=retry;tick();assert(attempts==before+1);
 std::cout<<"PASS actual camera retry: bounded exponential backoff, initialization recovery, recording restart, explicit pause, write failure and clock wrap\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);f=p/'retry.cpp';f.write_text(pre+block+checks,encoding='utf8');exe=p/'retry.exe'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(f),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
capture=source[source.index('camera_fb_t* f=esp_camera_fb_get()'):]
assert 'paused=true' not in capture
assert 'esp_camera_deinit()' in capture and 'Camera frames stalled' in capture
