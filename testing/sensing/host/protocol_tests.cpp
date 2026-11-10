#include <cassert>
#include <iostream>
#include <random>
#include "src/link/EventLink.h"
#include "legacy_crash_v1_types_1_to_4.h"
namespace L=ImuLabProtocol;
int main(){
 L::Sample golden;golden.sequence=10;golden.accel[2]=2048;golden.gyro[0]=16;golden.temperature=512;golden.flags=356;golden.sensorTime=128;golden.deltaUs=5000;uint8_t raw[L::SAMPLE_BYTES];L::encodeSample(golden,raw);assert(L::decodeSample(raw).sequence==10 && L::decodeSample(raw).accel[2]==2048);

 // Existing type bytes remain identical. The original receiver rejects the new
 // type-5 extension, so a rollback cannot turn it into a real crash.
 for(unsigned type=1;type<=4;++type){CrashProtocol::Frame a;a.type=CrashProtocol::Type(type);a.boot=1;uint8_t raw[32];CrashProtocol::encode(a,raw);LegacyCrashProtocol::Frame old;assert(LegacyCrashProtocol::decode(raw,old));}
 CrashProtocol::Frame automatic;automatic.type=CrashProtocol::Type::AutomaticCrash;automatic.flags=7;automatic.boot=1;automatic.sequence=1;
 uint8_t automaticBytes[32];CrashProtocol::encode(automatic,automaticBytes);LegacyCrashProtocol::Frame old;CrashProtocol::Frame decodedAutomatic;
 assert(CrashProtocol::decode(automaticBytes,decodedAutomatic) && !LegacyCrashProtocol::decode(automaticBytes,old));
 FakeNvs::reset();EventLink link;link.begin();L::Frame f;f.boot=link.bootId();f.session=7;f.type=L::Type::Live;f.length=40;
 assert(!link.sendLab(f));L::Frame hello;hello.type=L::Type::Hello;hello.boot=f.boot;hello.session=7;
 uint8_t b[L::MAX_FRAME];auto n=L::encode(hello,b);assert(n==22);for(unsigned j=0;j<n;j++)ports[2].rx.push_back(b[j]);link.tick(CrashProtocol::Frame());assert(link.sendLab(f));clockMs+=2500;assert(!link.sendLab(f));
 hello.session=9;n=L::encode(hello,b);for(unsigned j=0;j<n;j++)ports[2].rx.push_back(b[j]);link.tick(CrashProtocol::Frame());assert(!link.labSessionCurrent(7)&&link.labSessionCurrent(9));assert(!link.sendLab(f));f.session=9;assert(link.sendLab(f));

 automatic.boot=link.bootId();assert(!link.enqueue(automatic));
 hello.token=L::AUTOMATIC_CRASH_HELLO_TOKEN;n=L::encode(hello,b);for(unsigned j=0;j<n;j++)ports[2].rx.push_back(b[j]);link.tick(CrashProtocol::Frame());
 assert(link.automaticPeerCurrent());
 if(SensorConfig::ARM_CRASH_DETECTION){assert(link.enqueue(automatic));clockMs+=2500;assert(link.replayBlocked());}else assert(!link.enqueue(automatic));
 std::cout<<"PASS automatic crash type, original receiver rejection, readiness flags, capability lease and disabled-mode quarantine\n";
 // Unknown legacy envelope encloses a complete, CRC-valid incident; no V1 rescan.
 CrashProtocol::Frame event;event.type=CrashProtocol::Type::Crash;event.boot=1;event.sequence=1;event.flags=CrashProtocol::Armed|CrashProtocol::ImuReady|CrashProtocol::PiezoReady;
 f.length=100;CrashProtocol::encode(event,f.data+20);n=L::encode(f,b);b[2]=1;L::put16(b+n-2,CrashProtocol::crc16(b,n-2));
 L::Parser parser;L::Frame out;CrashProtocol::Frame v1;unsigned accepted=0;for(unsigned j=0;j<n;j++)accepted+=parser.feed(b[j],clockMs,v1,out);assert(!accepted);
 n=L::encode(f,b);for(unsigned j=0;j<n;j++)accepted+=parser.feed(b[j],clockMs,v1,out);assert(accepted==2);
 b[n-1]^=1;accepted=0;for(unsigned j=0;j<n;j++)accepted+=parser.feed(b[j],clockMs,v1,out);assert(!accepted);
 L::Live live;live.healthy=true;live.who=67;live.candidateCount=1;live.eventRotationUs=100000;live.eventTiltUs=800000;live.eventChange=30;L::encodeLive(live,f);n=L::encode(f,b);assert(n==224);L::Frame decoded;assert(L::decode(b,n,decoded));L::Live round;assert(L::decodeLive(decoded,round)&&round.eventTiltUs==800000&&round.candidateCount==1);
 L::putFloat(decoded.data+28,NAN);assert(!L::decodeLive(decoded,round));
 std::mt19937 rng(12);for(unsigned i=0;i<100000;i++)parser.feed(uint8_t(rng()),i/100,v1,out);
 std::cout<<"PASS V2: no pre-negotiation stream, lease expiry, unknown/V1 envelope isolation, embedded valid incident, CRC rejection, maximum live frame/candidate evidence, nonfinite rejection, parser noise\n";
}
