"""Run the real message builders through the modem's GSM-7 length validator."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);source=p/'sms.cpp'
 source.write_text(r'''
#include "src/core/Domain.h"
#include "src/gsm/SmsCodec.h"
#include <cassert>
#include <iostream>
uint32_t clockMs=0;
int main(){
 for(size_t size=1;size<=40;++size){
  const std::string name(size,'A');
  const auto intro=Domain::invitationMessage(name);
  assert(intro=="Hello from "+name+"'s SmartVest!\nThey added you as an emergency contact. Please save this number as SmartVest for future alerts.");
  assert(SmsCodec::validText(intro.c_str(),intro.size()));
  for(double lat:{-90.,0.,90.})for(double lon:{-180.,0.,180.})for(bool fresh:{false,true})for(bool test:{false,true}){
   const auto location=Domain::smsLocation(lat,lon);
   const auto msg=Domain::incidentMessage(name,location,fresh,test);
   assert(SmsCodec::validText(msg.c_str(),msg.size()));
   assert(msg.find(location)!=std::string::npos);
   assert(msg.find("http")==std::string::npos);
   assert(msg.find("Ask rider")==std::string::npos);
   assert(msg.find(fresh?"Location:":"Last known location:")!=std::string::npos);
   if(!fresh)assert(msg.find("Last known")!=std::string::npos);
   if(!test){assert(msg.find("SmartVest CRASH ALERT\nRider: "+name+"\n")==0);
    assert(msg.find("Call their phone now. No answer? Call 911.")!=std::string::npos);
    for(const char* word:{"possible","may have","false","test"})assert(msg.find(word)==std::string::npos);
   }else {assert(msg.find("This is a test. No action needed.")!=std::string::npos);assert(msg.find("Call 911")==std::string::npos);}
  }
  const auto noGps=Domain::crashMessage(name,"",false);
  assert(noGps.find("Location unavailable.")!=std::string::npos);
  assert(noGps.find("https://")==std::string::npos&&noGps.find("Call 911")!=std::string::npos);
  assert(SmsCodec::validText(noGps.c_str(),noGps.size()));
 }
 const auto longest=Domain::crashMessage(std::string(40,'A'),Domain::smsLocation(-90,-180),false);
 assert(longest.size()<=160);
 std::cout<<"PASS actual invitation/alert/test builders: 40 name lengths, coordinate extremes, fresh/stale/missing GPS, GSM-7 <=160, personal-phone/emergency action, link-free coordinates and explicit test labels\n";
}
''',encoding='utf8')
 out=p/'sms'
 subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(root/'testing/main/host'),'-I'+str(main),str(source),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
