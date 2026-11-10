"""Execute production Main boot/task order and network gate with controlled startup delays."""
from pathlib import Path
import subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[2];MAIN=ROOT/'Main_and_Cameras/SmartVest_SJKS01_Main'
sys.path.insert(0,str(ROOT/'testing/system'))
from http_core import method
application=(MAIN/'src/core/Application.cpp').read_text('utf8')
server=(MAIN/'src/web/DashboardServer.cpp').read_text('utf8')
boot=method(application,'void Application::begin()')
ui=method(application,'void uiTask(void*)')
assert 'dashboard.startNetwork()' not in boot
assert 'if (!Network.begin()) return false;' in server
assert 'if (Config::NETWORK_USES_AP && (Config::HOTSPOT_LOCAL_DEVICE_MODE || Config::ENABLE_CAPTIVE_PORTAL))' in server
assert 'if (network.hotspotDnsActive()) {' not in server
prefix=server[server.index('void DashboardServer::tick() {')+len('void DashboardServer::tick() {'):server.index('  cameraResponseRole=-1;download.tick(millis());')]
code=r'''
#include <cassert>
#include <cstdint>
#include <iostream>
uint32_t clockMs=0;uint32_t millis(){return clockMs;}
namespace Config {constexpr bool ENABLE_SENSOR_LINK=true,ENABLE_BATTERY_LED=true,ENABLE_SIM800L=true;constexpr uint32_t WIFI_RETRY_INITIAL_MS=5000;}
constexpr int ESP_LOG_NONE=0,pdPASS=1;
using SemaphoreHandle_t=void*;
bool mutexOK=true,storageOK=true,tcpOK=true,listenOK=true,taskOK=true,networkOK=true;
bool ledDone=false,storageDone=false,listener=false,taskExecuting=false,apVisible=false,stackReady=false;
unsigned firstProbes=0,servedProbes=0;
void esp_log_level_set(const char*,int){}
SemaphoreHandle_t xSemaphoreCreateMutex(){return mutexOK?reinterpret_cast<void*>(1):nullptr;}
namespace Console {void begin(int){}void line(const char*){}}
struct SmsJob{};
struct SerialReporter {void begin(){}}reporter;
struct StateStore {bool begin(){clockMs+=6000;storageDone=true;return storageOK;}}store;
struct NEO6MService {void begin(){}int consolePort(){return 0;}}gps;
struct EmergencyCoordinator {EmergencyCoordinator(StateStore&,NEO6MService&){}void begin(){}bool submissionGuard(const SmsJob&){return true;}}emergency{store,gps};
struct SensorLink {int source=0;SensorLink(EmergencyCoordinator&){}void begin(){}int& batterySource(){return source;}}sensor{emergency};
struct SIM800LService {SIM800LService(StateStore&){}void setSubmissionGuard(bool(*)(void*,const SmsJob&),void*){}void begin(){}}gsm{store};
struct BatteryService {void attachBattery(int*){}}power;
struct BatteryLed {void begin(){clockMs+=3000;ledDone=true;}bool colorReady(){return true;}}batteryLed;
SemaphoreHandle_t stateMutex=nullptr;bool hardwareReady=false,webReady=false;
struct Application {static void begin();};
struct DashboardServer {
 bool networkStarted=false;uint32_t networkRetryAt=0;unsigned starts=0,services=0;
 void attachRuntime(SemaphoreHandle_t,SensorLink&,EmergencyCoordinator&){assert(storageDone&&ledDone);}
 bool begin(){stackReady=tcpOK;if(!stackReady)return false;listener=listenOK;return listener;}
 bool startNetwork(){
  assert(taskExecuting&&listener&&webReady&&hardwareReady&&storageDone&&ledDone&&stackReady);
  ++starts;if(!networkOK)return false;networkStarted=true;apVisible=true;++firstProbes;return true;
 }
 void tick(){
'''+prefix+r'''
  ++services;if(firstProbes>servedProbes){assert(listener);++servedProbes;}
 }
}dashboard;
void(*taskEntry)(void*)=nullptr;
int xTaskCreatePinnedToCore(void(*entry)(void*),const char*,unsigned stack,void*,unsigned priority,void*,unsigned core){
 assert(listener&&storageDone&&ledDone&&!apVisible&&stack==16384&&priority==0&&core==0);
 if(!taskOK)return 0;taskEntry=entry;return pdPASS;
}
struct SliceFinished{};
uint32_t pdMS_TO_TICKS(uint32_t value){return value;}
void vTaskDelay(uint32_t value){clockMs+=value;throw SliceFinished{};}
'''+ui+r'''
bool guard(void* context,const SmsJob& job){return static_cast<EmergencyCoordinator*>(context)->submissionGuard(job);}
'''+boot+r'''
void slice(){assert(taskEntry);taskExecuting=true;try{taskEntry(nullptr);}catch(const SliceFinished&){}taskExecuting=false;}
void clear(){
 clockMs=0;mutexOK=storageOK=tcpOK=listenOK=taskOK=networkOK=true;
 ledDone=storageDone=listener=taskExecuting=apVisible=stackReady=false;
 firstProbes=servedProbes=0;taskEntry=nullptr;stateMutex=nullptr;hardwareReady=webReady=false;dashboard=DashboardServer{};
}
int main(){
 for(int fault=0;fault<5;++fault){
  clear();if(fault==1)mutexOK=false;if(fault==2)tcpOK=false;if(fault==3)listenOK=false;if(fault==4)taskOK=false;
  Application::begin();assert(!apVisible&&dashboard.starts==0);
  if(fault){assert(!taskEntry&&!webReady);continue;}
  assert(clockMs==9000&&webReady&&hardwareReady&&listener&&taskEntry);
  slice();assert(apVisible&&servedProbes==1&&firstProbes==1&&dashboard.starts==1);
  for(unsigned n=0;n<100;++n)slice();assert(dashboard.starts==1&&servedProbes==1);
 }
 clear();storageOK=false;Application::begin();assert(webReady&&taskEntry&&!apVisible);slice();assert(apVisible&&servedProbes==1);
 clear();Application::begin();networkOK=false;slice();assert(!apVisible&&dashboard.starts==1&&!dashboard.services);
 clockMs=dashboard.networkRetryAt-1;slice();assert(dashboard.starts==1);
 networkOK=true;slice();assert(dashboard.starts==2&&apVisible&&servedProbes==1);
 std::cout<<"PASS actual Main boot/task ordering: no joinable AP during nine seconds of startup, listener/task readiness, first probe served, mutex/TCP/listener/task failures, storage diagnostics and bounded network retry\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code,'utf8')
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
