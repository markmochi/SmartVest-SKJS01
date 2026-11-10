"""Compile the shipped periodic health-report block; no serial/device access."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras';s=r/'SmartVest_SJKS01_Main'
source=(s/'src/diagnostics/SerialReporter.cpp').read_text(encoding='utf8')
block=source[source.index('  static uint32_t lastHealthReport=0;'):source.index('  const auto position =')]
with tempfile.TemporaryDirectory() as t:
 p=Path(t);code=p/'report.cpp'
 code.write_text('''#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>
#include "Config.h"
#include "src/core/Version.h"
#include "src/diagnostics/RuntimeTelemetry.h"
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
unsigned heap_caps_get_free_size(unsigned caps){assert(caps==3);return 64000;}
unsigned heap_caps_get_minimum_free_size(unsigned){return 4000;}
unsigned heap_caps_get_largest_free_block(unsigned){return 32000;}
namespace Console {std::vector<std::string> lines;
void format(const char* f,...){char b[192];va_list a;va_start(a,f);int n=vsnprintf(b,sizeof b,f,a);va_end(a);assert(n>=0&&n<190);lines.emplace_back(b);}}
void report(const RuntimeTelemetry& runtime,uint32_t now){
'''+block+'''}
int main(){
RuntimeTelemetry r;r.clients=2;r.clientJoins=6;r.clientLeaves=6;r.apStarts=2;r.apStops=1;r.hotspot=true;
r.capturedAt=999670;r.radio={};r.radio.configured=r.radio.visible=r.radio.readable=r.radio.dhcp=true;
r.radio.txPowerQuarterDbm=78;r.radio.channel=6;r.radio.protocol=3;r.radio.limit=6;r.httpRequests=263;
r.radio.serviceRecoveries=1;r.radio.authFailures=3;
r.httpSending=4;r.httpWriteAborts=3;r.httpFlashCompleted=100;r.maxFlashMs=12000;
r.httpRequestMemoryFailures=1;r.httpResponseMemoryFailures=2;r.httpDocumentFailures=3;r.httpOutputFailures=4;r.httpLengthFailures=5;r.httpQueuedBytes=4096;r.httpMaxQueuedBytes=20000;
report(r,1000004);assert(Console::lines.size()==7);
assert(Console::lines[0].find("clientsNow=2/6")!=std::string::npos);
assert(Console::lines[0].find("joinEvents=6 leaveEvents=6")!=std::string::npos);
assert(Console::lines[1].find("visible=1 config=1 dhcp=1")!=std::string::npos);
assert(Console::lines[1].find("proto=0x3 limit=6")!=std::string::npos);
assert(Console::lines[1].find("service=1 authFails=3")!=std::string::npos);
assert(Console::lines[3].find("byteFree=64000 byteMinimum=4000 byteLargest=32000")!=std::string::npos);
assert(Console::lines[4].find("snapshotAgeMs=334")!=std::string::npos);
assert(Console::lines[2].find("txLimitQdBm=78")!=std::string::npos);
assert(Console::lines[5].find("active=4 completed=100 aborted=3 maxFlashMs=12000")!=std::string::npos);
assert(Console::lines[6].find("requestFails=1 responseFails=2 docFails=3 outputFails=4 lengthFails=5 queued=4096 peakQueued=20000")!=std::string::npos);
// Every counter can reach its full width without truncating the diagnosis.
r.clientJoins=r.clientLeaves=r.apStarts=r.apStops=UINT32_MAX;r.clients=6;
r.radio.recoveryAttempts=r.radio.lastStartMs=r.radio.lastStopMs=UINT32_MAX;r.radio.disconnectReason=UINT16_MAX;
r.radio.serviceRecoveries=r.radio.authFailures=UINT32_MAX;
r.radio.error=INT32_MIN;r.maxHttpMs=r.httpRequests=r.httpExpired=r.httpRejected=UINT32_MAX;
r.httpWriteAborts=r.httpFlashCompleted=r.maxFlashMs=UINT32_MAX;r.httpSending=UINT8_MAX;
r.httpRequestMemoryFailures=r.httpResponseMemoryFailures=r.httpDocumentFailures=r.httpOutputFailures=r.httpLengthFailures=r.httpQueuedBytes=r.httpMaxQueuedBytes=UINT32_MAX;
report(r,UINT32_MAX);assert(Console::lines.size()==14);
for(auto& l:Console::lines){assert(l.find(Config::WIFI_PASSWORD)==std::string::npos);assert(l.find(Config::DEVELOPER_PASSWORD)==std::string::npos);}
std::cout<<"PASS actual serial health block: simultaneous clients vs lifetime events, radio/DHCP/protocol/byte-RAM/snapshot age; full-width counters fit console lines and no credentials emitted\\n";
}
''',encoding='utf8')
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(r/'../testing/main/host'),'-I'+str(s),str(code),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
