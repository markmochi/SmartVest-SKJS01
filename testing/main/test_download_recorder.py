"""Run the actual download/recording owner against signed clip metadata and files."""
from pathlib import Path
import ast,os,subprocess,tempfile
root=Path(__file__).resolve().parents[2];camera=root/'Main_and_Cameras/SmartVest_SJKS01_Camera/src'
s=(camera/'CameraApp.cpp').read_text();fixture=ast.parse(Path(__file__).with_name('test_camera_storage.py').read_text());pieces={}
for node in fixture.body:
 if isinstance(node,ast.Assign) and isinstance(node.value,ast.Constant) and isinstance(node.value.value,str):pieces[node.targets[0].id]=node.value.value
pre=pieces['pre'].replace('MAX_FRAME_GAP_MS=2000,','').replace('constexpr size_t NORMAL_CLIPS=30;','').replace('FREE_RESERVE_BYTES=64ULL*1024*1024,','');clip=s[s.index('struct Clip {'):s.index('QueueHandle_t commands=')];helpers=s[s.index('bool loadJson('):s.index('// FAT stores local wall time.')]
globals=pieces['globals']+r'''
#include "FileLibrary.h"
#include "DownloadTransfer.h"
struct Socket{bool begin(){return true;}bool accept(){return false;}int read(char*,size_t){return -1;}int write(const uint8_t*,size_t){return -1;}void close(){}};
DownloadTransfer<Socket> downloadTransfer;File downloadFile;bool downloadResume=false;
uint32_t downloadControlId=1,downloadControlUntil=11000;bool downloadControlStop=false;
bool sd=true,camera=true,paused=false,running=true,canClose=true,canOpen=true;
unsigned opens=0,closes=0,trims=0;int snapshotMux=0;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
bool closeClip(){++closes;if(!canClose)return false;running=false;return true;}
bool trim(){++trims;return true;}
bool openClip(){++opens;running=canOpen;return canOpen;}
'''
worker=(camera/'DownloadRecorder.h').read_text().replace('#pragma once','')
start=s.index("  if(Serial.available() && Serial.read()=='s')")
stop=s[start:s.index('  if(!camera&&!paused',start)].replace('Serial.','Console.')
worker += r'''
struct ConsoleStub{bool available(){return true;}int read(){return 's';}void println(const char*){assert(paused&&!downloadTransfer.active()&&!downloadFile);}}Console;
bool saveEvent(Event&){return true;}void reply(Event&){}
void operatorStop(){
'''+stop+'\n}\n'

tests=r'''
int main(int argc,char**argv){
 assert(argc==2);SD_MMC.root=argv[1];for(const char* p:{"/clips","/system/clips"})std::filesystem::create_directories(SD_MMC.root+p);
 Clip c;c.role=ROLE;c.stem=c.videoStem="/clips/front_saved";c.session=42;c.start=100;c.end=1600;c.first=100;c.last=1475;c.frames=12;c.good=true;c.bytes=1000;
 strcpy(c.payload,"1111111111111111111111111111111111111111111111111111111111111111");assert(clipSeal(c,c.seal));JsonDocument doc;clipJson(doc.to<JsonObject>(),c);assert(saveJson(ClipStorage::metadata(c.stem),doc));
 {File file=SD_MMC.open(c.stem+".avi",FILE_WRITE);uint8_t data[1000]{};assert(file.write(data,1000)==1000);}
 FileRequest request;request.id=1;request.beforeSession=42;request.beforeStart=100;strcpy(request.beforeName,c.stem.c_str());strcpy(request.token,"abcdef0123456789abcdef0123456789");char error[161];current.frames=3;
 assert(beginDownload(request,error,sizeof(error)));assert(downloadTransfer.active()&&!running&&closes==1&&downloadResume);
 clockMs=11001;assert(!serviceDownload());assert(!downloadTransfer.active()&&running&&opens==1&&!downloadResume);
 // A repeated/expired permission never pauses recording again.
 assert(!beginDownload(request,error,sizeof(error)));assert(closes==1&&running);
 clockMs=12000;downloadControlUntil=22000;downloadControlId=request.id=2;
 assert(beginDownload(request,error,sizeof(error)));downloadControlStop=true;assert(!serviceDownload()&&running&&opens==2);downloadControlStop=false;
 // Existing operator stop remains stopped; finishing a download cannot override it.
 downloadControlId=request.id=3;paused=true;running=false;assert(beginDownload(request,error,sizeof(error)));endDownload(false);assert(paused&&!running&&opens==2);paused=false;
 // New incident work interrupts transfer and returns the recorder before evidence work.
 downloadControlId=request.id=4;running=true;assert(beginDownload(request,error,sizeof(error)));events.push_back(Event{});assert(!serviceDownload()&&running&&opens==3);events.clear();
 downloadControlId=request.id=5;assert(beginDownload(request,error,sizeof(error)));sd=false;assert(!serviceDownload()&&!downloadTransfer.active()&&opens==3);sd=true;
 downloadControlId=request.id=6;running=true;canClose=false;assert(!beginDownload(request,error,sizeof(error))&&catalogFailure&&!downloadTransfer.active());canClose=true;
 // Resume failures leave transfer mode and delegate to ordinary storage recovery.
 catalogFailure=false;downloadControlId=request.id=7;running=true;assert(beginDownload(request,error,sizeof(error)));canOpen=false;endDownload(false);assert(catalogFailure&&!downloadTransfer.active()&&!downloadResume);canOpen=true;
 downloadControlId=request.id=8;running=true;paused=false;catalogFailure=false;assert(beginDownload(request,error,sizeof(error)));const auto beforeStop=opens;operatorStop();assert(paused&&!running&&!downloadTransfer.active()&&opens==beforeStop);
 {File file=SD_MMC.open(c.stem+".avi",FILE_READ);assert(file&&file.size()==1000);for(unsigned i=0;i<1000;++i)assert(file.read()==0);}
 std::cout<<"PASS production owner: finalize/pause, finish/timeout/cancel/incident/card loss, preserve operator stop, recovery after failure and original file preservation\n";
}
'''
with tempfile.TemporaryDirectory() as temporary:
 p=Path(temporary);source=p/'owner.cpp';source.write_text(pre+clip+globals+helpers+worker+tests);binary=p/'owner.exe'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(root/'testing/system/platform'),'-I'+str(root/'testing/main/camera'),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(camera),str(source),'-lcrypto','-o',str(binary)],check=True)
 subprocess.run([str(binary),str(p/'sd')],check=True)
