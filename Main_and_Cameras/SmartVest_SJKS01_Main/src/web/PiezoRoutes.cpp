#include "DashboardServer.h"
#include <cstring>
void DashboardServer::commandPiezo(){
  if(!authorize(true))return;
  auto body=makeJson();if(!readJsonBody(body))return;
  namespace L=ImuLabProtocol;
  if(!body["action"].is<const char*>()||!body["revision"].is<uint32_t>()){sendError(400,"Choose an impact action using the current readings.");return;}
  const char* action=body["action"];L::Op op;
  if(!std::strcmp(action,"baseline"))op=L::Op::PiezoBaseline;
  else if(!std::strcmp(action,"thresholds"))op=L::Op::PiezoThresholds;
  else if(!std::strcmp(action,"normal"))op=L::Op::PiezoNormal;
  else if(!std::strcmp(action,"taps"))op=L::Op::PiezoTaps;
  else if(!std::strcmp(action,"stop"))op=L::Op::PiezoStop;
  else{sendError(400,"That impact action isn't available.");return;}
  if(body.as<JsonObjectConst>().size()!=(op==L::Op::PiezoThresholds?4u:2u)){sendError(400,"Send only the fields for this impact action.");return;}
  uint16_t front=0,rear=0;
  if(op==L::Op::PiezoThresholds){
    if(!body["front"].is<unsigned>()||!body["rear"].is<unsigned>()||body["front"].as<unsigned>()<PiezoProtocol::MIN_THRESHOLD||body["front"].as<unsigned>()>PiezoProtocol::MAX_THRESHOLD||body["rear"].as<unsigned>()<PiezoProtocol::MIN_THRESHOLD||body["rear"].as<unsigned>()>PiezoProtocol::MAX_THRESHOLD){sendError(400,"Use a whole-number threshold from 50 to 800 for each bank.");return;}
    front=body["front"].as<uint16_t>();rear=body["rear"].as<uint16_t>();
  }
  if((op==L::Op::PiezoNormal||op==L::Op::PiezoTaps)&&!Config::ENABLE_BENCH_TESTS){sendError(409,"Administrator trials are disabled in this build.");return;}
  const auto ready=sensorLink?sensorLink->piezoCommandReady(op):L::Result::Offline;
  if(ready!=L::Result::Accepted){sendError(409,ready==L::Result::Busy?"Finish the current sensing action first.":"Fresh impact readings are unavailable. Check both updated boards.");return;}
  const uint32_t token=sensorLink->requestLab(op,front,rear,body["revision"].as<uint32_t>());
  if(!token){sendError(409,"Another sensing action is pending.");return;}
  auto result=makeJson();result["token"]=token;result["state"]="queued";sendJson(result,202);
}
