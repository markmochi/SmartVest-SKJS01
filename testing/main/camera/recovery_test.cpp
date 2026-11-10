#include "../../../Main_and_Cameras/SmartVest_SJKS01_Camera/src/NetworkSession.h"
#include "../../../Main_and_Cameras/SmartVest_SJKS01_Main/src/web/CameraPresence.h"
#include <cassert>
#include <cstring>
#include <iostream>
using CameraHttp::Request;
using CameraHttp::Session;
int main(){
 constexpr uint32_t timeout=15000;
 CameraPresence front,rear;
 for(uint32_t now=0;now<300000;now+=1000){front.expire(now,timeout);rear.expire(now,timeout);assert(!front.online(now,timeout)&&!rear.online(now,timeout));}
 assert(!strcmp(front.state(300000,timeout),"waiting"));
 front.heartbeat(300000,timeout);assert(front.online(300000,timeout));assert(!rear.online(300000,timeout));
 rear.heartbeat(420000,timeout);front.expire(420000,timeout);
 assert(rear.online(420000,timeout)&&!front.online(420000,timeout));
 assert(!strcmp(front.state(420000,timeout),"offline"));
 front.heartbeat(425000,timeout);assert(front.online(425000,timeout)&&front.connections==2);
 front.heartbeat(430000,timeout);assert(front.connections==2);
 front.expire(445000,timeout);assert(!front.online(445000,timeout));
 // Once expired, a full uint32 epoch cannot resurrect an old recording report.
 assert(!front.online(430000,timeout));
 front.heartbeat(0xfffffff0U,timeout);assert(front.online(10,timeout));
 front.expire(14984,timeout);assert(!front.online(14984,timeout));
 CameraPresence rebootedMain;assert(!rebootedMain.present);
 rebootedMain.heartbeat(1,timeout);assert(rebootedMain.online(1,timeout));
 std::cout<<"PASS main: independent late cameras, timeout, recovery, main reboot, clock wrap\n";

 for(uint32_t phase:{0U,350U}){
  Session s(5000,1000,1000,phase);
  assert(!s.observe(0,false));assert(s.next(300000,true)==Request::None);
  assert(s.observe(300000,true));
  if(phase)assert(s.next(300349,true)==Request::None);
  assert(s.next(300000+phase,true)==Request::Heartbeat); // queued ACK cannot hide startup
  s.completed(Request::Heartbeat,300000+phase,200,true,0);
  assert(!s.observe(300001+phase,true));
  assert(s.next(300074+phase,true)==Request::None);
  assert(s.next(300075+phase,true)==Request::Ack);
  s.completed(Request::Ack,300075+phase,200,false,0);
  assert(s.next(301075+phase,true)==Request::Ack);
  s.completed(Request::Ack,301075+phase,200,false,0);
  assert(s.next(302075+phase,true)==Request::Poll); // bounded ACK burst
  s.completed(Request::Poll,302075+phase,-1,false,0);
  // Neither an HTTP failure nor a repeated live-state observation resets pacing.
  assert(!s.observe(302076+phase,true));assert(s.next(302076+phase,true)==Request::None);
  s.observe(302077+phase,false);assert(s.next(302078+phase,true)==Request::None);
  assert(s.observe(302100+phase,true));
  assert(s.next(302100+phase+phase,true)==Request::Heartbeat);
  s.completed(Request::Heartbeat,302100+phase+phase,401,false,0);
  assert(s.next(302101+phase+phase,true)==Request::None);
  // GOT_IP notice covers a down/up pair between polling iterations.
  assert(s.observe(302200,true,true));assert(s.next(302200+phase,true)==Request::Heartbeat);
 }
 Session wrapped(5000,1000,1000,350);wrapped.observe(0xfffffff0U,true);
 assert(wrapped.next(333,false)==Request::None);assert(wrapped.next(334,false)==Request::Heartbeat);
 Session mismatched(5000,1000,1000,0);mismatched.observe(0,true);
 mismatched.completed(Request::Heartbeat,0,200,false,0);
 assert(mismatched.next(9999,false)==Request::None);
 assert(mismatched.next(10000,false)==Request::Poll);
 Session changed(5000,1000,1000,0);changed.observe(0,true);
 assert(changed.next(0,false)==Request::Heartbeat);changed.completed(Request::Heartbeat,0,200,true,0);
 changed.statusChanged(200);assert(changed.next(200,false)==Request::Poll);
 changed.completed(Request::Poll,200,200,false,0);assert(changed.next(274,false)==Request::None);
 assert(changed.next(275,false)==Request::Heartbeat);
 std::cout<<"PASS camera HTTP: reconnect resets cooldown, first heartbeat, ACK fairness, role phase, wrong-role backoff, wrap\n";

 // Failed HTTP with Wi-Fi still connected: keep retrying, never require an IP
 // transition. Main can start/restart at any time and receive a future heartbeat.
 Session http(5000,1000,1000,0);http.observe(0,true);
 uint32_t beats=0,polls=0;CameraPresence main;
 for(uint32_t now=0;now<180000;now+=50){
  assert(!http.observe(now,true));main.expire(now,timeout);
  const auto op=http.next(now,false);if(op==Request::None)continue;
  if(op==Request::Heartbeat)++beats;else if(op==Request::Poll)++polls;
  const bool serverUp=now>=120000;
  http.completed(op,now,serverUp?200:-1,serverUp,0);
  if(serverUp&&op==Request::Heartbeat)main.heartbeat(now,timeout);
 }
 assert(beats>10&&polls>10&&main.online(180000,timeout));
 // Wi-Fi retry policy has no terminal unavailable state after prolonged failure.
 CameraLink::Retry radio;radio.start(0);uint32_t attempts=0;
 for(uint32_t now=0;now<600000;now+=50){
  auto action=radio.tick(now,false);if(action==CameraLink::Action::Begin){++attempts;radio.failed(now);}
 }
 assert(attempts>15);radio.tick(600000,true);assert(radio.online());
 radio.tick(600050,false);assert(radio.tick(602050,false)==CameraLink::Action::Begin);
 std::cout<<"PASS outage: main HTTP returns after 2 minutes, Wi-Fi returns after 10 minutes, subsequent reconnect\n";
}
