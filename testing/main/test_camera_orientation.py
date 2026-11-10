"""Exercise the production camera initialization and orientation failure paths."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
app = (root/'Main_and_Cameras/SmartVest_SJKS01_Camera/src/CameraApp.cpp').read_text(encoding='utf8')
config = (root/'Main_and_Cameras/SmartVest_SJKS01_Camera/CameraConfig.h').read_text(encoding='utf8')
init = app[app.index('bool initCamera() {'):app.index('\n}\n}\nvoid CameraApp::begin()')+2]
assert 'constexpr bool VERTICAL_FLIP = true;' in config
assert 'camera=initCamera();' in app[app.index('void CameraApp::tick()'):]
code = r'''
#include <cassert>
#include <iostream>
constexpr int LEDC_CHANNEL_0=0,LEDC_TIMER_0=0,PIXFORMAT_JPEG=1,FRAMESIZE_VGA=2,
              CAMERA_FB_IN_PSRAM=3,CAMERA_GRAB_LATEST=4,ESP_OK=0;
constexpr bool VERTICAL_FLIP=true;
constexpr char ROLE[]="front";
using esp_err_t=int;
struct camera_config_t {
 int ledc_channel,ledc_timer,pin_d0,pin_d1,pin_d2,pin_d3,pin_d4,pin_d5,pin_d6,pin_d7;
 int pin_xclk,pin_pclk,pin_vsync,pin_href,pin_sccb_sda,pin_sccb_scl,pin_pwdn,pin_reset;
 int xclk_freq_hz,pixel_format,frame_size,jpeg_quality,fb_count,fb_location,grab_mode;
};
struct sensor_t {struct {unsigned PID;} id;int (*set_vflip)(sensor_t*,int);};
bool psram=true,present=true,active=false;
int initResult=0,flipResult=0,initCalls=0,flipCalls=0,deinitCalls=0;
int applied=-1;
sensor_t sensor;
bool psramFound(){return psram;}
struct {template<class... T> void printf(const char*,T...){} void println(const char*){}} Serial;
int esp_camera_init(camera_config_t* c){
 ++initCalls;assert(c->frame_size==FRAMESIZE_VGA&&c->pixel_format==PIXFORMAT_JPEG);
 assert(c->fb_count==2&&c->grab_mode==CAMERA_GRAB_LATEST);
 active=initResult==ESP_OK;applied=-1;return initResult;
}
sensor_t* esp_camera_sensor_get(){assert(active);return present?&sensor:nullptr;}
int esp_camera_deinit(){assert(active);active=false;++deinitCalls;return 0;}
int flip(sensor_t* s,int value){assert(active&&s==&sensor);++flipCalls;applied=value;return flipResult;}
void reset(){
 psram=present=true;active=false;initResult=flipResult=initCalls=flipCalls=deinitCalls=0;
 applied=-1;sensor.id.PID=0x3660;sensor.set_vflip=flip;
}
''' + init + r'''
int main(){
 reset();assert(initCamera()&&applied==1&&flipCalls==1&&!deinitCalls);
 // A recovery resets sensor state, then must reapply the correction.
 esp_camera_deinit();assert(initCamera()&&applied==1&&flipCalls==2&&initCalls==2);
 reset();psram=false;assert(!initCamera()&&!initCalls&&!flipCalls);
 reset();initResult=-1;assert(!initCamera()&&initCalls==1&&!flipCalls&&!deinitCalls);
 reset();present=false;assert(!initCamera()&&!flipCalls&&deinitCalls==1&&!active);
 reset();sensor.id.PID=0x2640;assert(!initCamera()&&!flipCalls&&deinitCalls==1&&!active);
 reset();sensor.set_vflip=nullptr;assert(!initCamera()&&!flipCalls&&deinitCalls==1&&!active);
 reset();flipResult=-1;assert(!initCamera()&&flipCalls==1&&deinitCalls==1&&!active);
 flipResult=0;assert(initCamera()&&flipCalls==2&&applied==1&&active);
 std::cout<<"PASS actual camera initialization: vertical correction, recovery reapplication, missing PSRAM, driver failure, sensor mismatch, unavailable control and failed setting retry\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'orientation.cpp'
    source.write_text(code,encoding='utf8')
    binary=Path(tmp)/'orientation'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
