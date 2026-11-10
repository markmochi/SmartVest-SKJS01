#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "src/sensing/FallDetector.h"
static uint64_t t;
void quiet(FallDetector& d,uint64_t start=0){t=start;for(int i=0;i<61;i++,t+=5000)d.feed(t,1,0,0,0,0);assert(d.state==FallDetector::Waiting);}
void motion(FallDetector& d,unsigned duration,float rate,float side,unsigned spacing=5000){const auto end=t+duration;for(;t<=end;t+=spacing)d.feed(t,1,rate,side,0,.1f);}
int main(int argc,char**argv){
 for(unsigned spacing:{3000u,5000u,6000u,7500u}){FallDetector d;quiet(d);motion(d,810000,120,60,spacing);assert(d.count==1&&d.latched);motion(d,1000000,130,70,spacing);assert(d.count==1);}
 {FallDetector d;quiet(d);t-=5000;for(unsigned j=0;j<200;j++){t+=j%2?6000:3000;d.feed(t,1,120,60,0,.1);}assert(d.count==1);}
 for(int missing=0;missing<3;missing++){FallDetector d;quiet(d);motion(d,110000,missing==0?119.99f:120,missing==1?20:0);motion(d,1000000,0,missing==1?20:missing==2?59.99f:60);assert(d.count==0);}
 {FallDetector d;quiet(d);motion(d,95000,120,60);motion(d,1000000,0,60);assert(d.count==0);}
 {FallDetector d;quiet(d);motion(d,795000,120,60);assert(d.count==0);d.feed(t,1,120,60,0,.1);assert(d.count==1);}
 {FallDetector d;quiet(d);motion(d,110000,120,0);motion(d,3800000,0,0);motion(d,810000,0,60);assert(d.count==0);}
 for(auto reason:{FallDetector::Gap,FallDetector::Rejected,FallDetector::Stale,FallDetector::Calibration,FallDetector::SensorLost,FallDetector::Clipped}){FallDetector d;quiet(d);motion(d,400000,120,60);d.invalidate(reason);motion(d,1000000,120,60);assert(d.count==0);}
 {FallDetector d;quiet(d);motion(d,400000,120,60);t+=7501;motion(d,1000000,120,60);assert(d.count==0);}
 {FallDetector d;motion(d,1000000,140,70);assert(d.count==0);}
 {FallDetector d;quiet(d,uint64_t(UINT32_MAX)-50000);motion(d,810000,120,60);assert(d.count==1);}
 {FallDetector d;quiet(d);for(int j=0;j<200;j++,t+=5000)d.feed(t,j%2?6:.1f,600,0,0,.1);assert(d.count==0);}
 {FallDetector d;quiet(d);motion(d,810000,120,60);motion(d,2200000,0,0);motion(d,1600000,0,0);motion(d,810000,120,60);assert(d.count==2);d.reset();assert(!d.latched&&d.count==2);}
 for(unsigned tiltAt:{3700000u,3705000u}){FallDetector d;quiet(d);auto start=t;for(unsigned dt=0;dt<=4505000;dt+=5000){d.feed(start+dt,1,dt<=100000?120:0,dt>=tiltAt?60:0,0,.1);}assert(d.count==(tiltAt==3700000?1u:0u));}
 {FallDetector d;quiet(d);motion(d,250000,0,60);motion(d,900000,120,80);assert(d.count==0);}
 std::cout<<"PASS actual detector: positive 3/5/6/7.5 ms and alternating3/6, threshold subsets, holds, window expiry, invalidation, unknown startup, time rollover, flags-only negative, recovery/reset\n";
 for(int k=1;k<argc;k++){std::ifstream f(argv[k]);assert(f);FallDetector d;std::string line;unsigned rows=0,gaps=0;uint64_t priorSeq=0,priorTime=0;while(std::getline(f,line)){std::istringstream in(line);uint64_t now,seq;float g,rate,side,forward,activity;unsigned delta,clip;in>>now>>seq>>g>>rate>>side>>forward>>activity>>delta>>clip;assert(in);if(rows&&(seq!=priorSeq+1||now<=priorTime||now-priorTime>7500||delta>7500)){d.invalidate(FallDetector::Gap);++gaps;}if(clip)d.invalidate(FallDetector::Clipped);else d.feed(now,g,rate,side,forward,activity);priorSeq=seq;priorTime=now;++rows;}assert(rows>60000);std::cout<<"Ride "<<argv[k]<<": rows="<<rows<<" candidates="<<d.count<<" gaps="<<gaps<<" (negative-only feature replay)\n";assert(d.count==0);}
}
