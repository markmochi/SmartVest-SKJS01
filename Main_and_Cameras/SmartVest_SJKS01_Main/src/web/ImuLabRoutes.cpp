#include "DashboardServer.h"
#include "../core/Version.h"
#include <cstring>
void DashboardServer::sendRiderPose() {
  if(!authorize(false,Access::Role::Rider))return;
  if(!sensorLink){sendError(503,"Your vest's position service is unavailable. Please try again.");return;}
  auto doc=makeJson();sensorLink->writeRiderPoseJson(doc.to<JsonObject>());sendJson(doc);
}
void DashboardServer::commandRiderCalibration() {
  if(!authorize(true,Access::Role::Rider))return;
  auto request=makeJson();if(!readJsonBody(request))return;
  if(!request["action"].is<const char*>() || request.as<JsonObjectConst>().size()!=1){
    sendError(400,"Choose one position setup step.");return;
  }
  const char* action=request["action"];
  namespace L=ImuLabProtocol;L::Op op;
  if(!std::strcmp(action,"upright"))op=L::Op::Upright;
  else if(!std::strcmp(action,"forward"))op=L::Op::Forward;
  else if(!std::strcmp(action,"neutral"))op=L::Op::Neutral;
  else if(!std::strcmp(action,"gyro"))op=L::Op::Gyro;
  else{sendError(400,"That position setup step isn't available.");return;}
  const L::Result ready=sensorLink?sensorLink->riderCalibrationReady(op):L::Result::Offline;
  if(ready!=L::Result::Accepted){
    if(ready==L::Result::Busy)sendError(409,"Your vest is finishing another action. Please wait and try again.");
    else if(ready==L::Result::Prerequisite)sendError(409,op==L::Op::Forward?"Save the upright position first.":"Save the upright and forward positions first.");
    else sendError(409,"Your vest isn't sending a fresh position. Check that it is powered and connected.");
    return;
  }
  const uint32_t token=sensorLink->requestLab(op);
  if(!token){sendError(409,"Your vest is finishing another action. Please wait and try again.");return;}
  auto reply=makeJson();reply["token"]=token;reply["state"]="queued";
  reply["message"]="Keep your position still while your vest saves it.";sendJson(reply,202);
}
void DashboardServer::sendImuLab() {
  if(!authorize())return;
  if(!sensorLink){sendError(503,"Sensing service unavailable.");return;}
  ResponseMemory::Allocator memory(responseHealth,ResponseMemory::Endpoint::Imu);JsonDocument doc(&memory);doc["mainFirmware"]=Firmware::VERSION;
  sensorLink->writeJson(doc["link"].to<JsonObject>());sensorLink->writeLabJson(doc["imu"].to<JsonObject>());sensorLink->writePiezoJson(doc["piezo"].to<JsonObject>());
  // Share the authoritative pose/calibration serializer with the Sensing view.
  // Its animation needs no second high-rate HTTP polling loop.
  sensorLink->writeRiderPoseJson(doc["rider"].to<JsonObject>());sendJson(doc);
}
void DashboardServer::commandImuLab() {
  if(!authorize(true))return;
  if(!sensorLink || !sensorLink->labOnline()){sendError(409,"No fresh IMU-lab telemetry. Flash both updated boards and check the sensing UART.");return;}
  auto request=makeJson();if(!readJsonBody(request))return;
  const String action=request["action"] | "";
  namespace L=ImuLabProtocol;L::Op op;
  if(action=="retry")op=L::Op::Retry;
  else if(action=="gyro")op=L::Op::Gyro;
  else if(action=="upright")op=L::Op::Upright;
  else if(action=="forward")op=L::Op::Forward;
  else if(action=="neutral")op=L::Op::Neutral;
  else if(action=="clear_body")op=L::Op::ClearBody;
  else if(action=="reset_candidate")op=L::Op::ResetCandidate;
  else{sendError(400,"Unknown IMU action.");return;}
  const uint32_t token=sensorLink->requestLab(op);
  if(!token){sendError(409,"A sensing command is still awaiting acknowledgement. Try again after it finishes.");return;}
  auto reply=makeJson();reply["token"]=token;reply["state"]="queued";reply["message"]="Awaiting sensing-board acknowledgement.";sendJson(reply,202);
}
