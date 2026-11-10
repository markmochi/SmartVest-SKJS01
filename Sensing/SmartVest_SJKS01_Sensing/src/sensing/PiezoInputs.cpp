#include "PiezoInputs.h"
#include "../link/EventLink.h"

void PiezoInputs::invalidate(esp_err_t error) {
  lastError = error; ++failures; validPair = false; fresh = false; goodPairs = 0;
  frontRaw = rearRaw = 0; clearPeaks();
  impact.breakContinuity();++impact.data.failures;
}
bool PiezoInputs::initialize() {
  lastInit = millis();
  adc_unit_t frontUnit = ADC_UNIT_1, rearUnit = ADC_UNIT_1;
  esp_err_t error = adc_oneshot_io_to_channel(SensorConfig::FRONT_PIEZO_PIN, &frontUnit, &frontChannel);
  if (error == ESP_OK) error = adc_oneshot_io_to_channel(SensorConfig::REAR_PIEZO_PIN, &rearUnit, &rearChannel);
  if (error != ESP_OK || frontUnit != ADC_UNIT_1 || rearUnit != ADC_UNIT_1 || frontChannel == rearChannel) {
    invalidate(error != ESP_OK ? error : ESP_ERR_INVALID_ARG); return false;
  }
  adc_oneshot_unit_init_cfg_t unit = {};
  unit.unit_id = ADC_UNIT_1; unit.ulp_mode = ADC_ULP_MODE_DISABLE;
  error = adc_oneshot_new_unit(&unit, &handle);
  if (error != ESP_OK) { handle = nullptr; invalidate(error); return false; }
  adc_oneshot_chan_cfg_t channel = {};
  channel.bitwidth = ADC_BITWIDTH_12; channel.atten = ADC_ATTEN_DB_12;
  error = adc_oneshot_config_channel(handle, frontChannel, &channel);
  if (error == ESP_OK) error = adc_oneshot_config_channel(handle, rearChannel, &channel);
  if (error != ESP_OK) {
    adc_oneshot_del_unit(handle); handle = nullptr; invalidate(error); return false;
  }
  initialized = true; lastPoll = millis() - SensorConfig::PIEZO_SAMPLE_MS;
  return true;
}
void PiezoInputs::begin() {
  if (started || !SensorConfig::PIEZO_MAPPING_CONFIRMED || !SensorConfig::PIEZO_CIRCUIT_VERIFIED) return;
  started = true;loadSettings();impact.resetBaseline();initialize();
}
bool PiezoInputs::healthy() const {
  return SensorConfig::PIEZO_MAPPING_CONFIRMED && SensorConfig::PIEZO_CIRCUIT_VERIFIED &&
         initialized && validPair && goodPairs >= SensorConfig::PIEZO_GOOD_PAIRS &&
         uint32_t(millis() - atMs) < SensorConfig::PIEZO_STALE_MS;
}
void PiezoInputs::tick() {
  fresh = false;
  if (!started) return;
  const uint32_t now = millis();
  if (!initialized) {
    if (uint32_t(now - lastInit) >= SensorConfig::PIEZO_INIT_RETRY_MS) initialize();
    return;
  }
  if (uint32_t(now - lastPoll) < SensorConfig::PIEZO_SAMPLE_MS) return;
  if (uint32_t(now - lastPoll) > SensorConfig::PIEZO_SAMPLE_MS * 2) ++overruns;
  lastPoll = now;
  if (validPair && uint32_t(now - atMs) >= SensorConfig::PIEZO_STALE_MS) {
    validPair = false; goodPairs = 0; clearPeaks();
    impact.breakContinuity();
  }
  int front = 0, rear = 0;
  esp_err_t error = adc_oneshot_read(handle, frontChannel, &front);
  if (error == ESP_OK) error = adc_oneshot_read(handle, rearChannel, &rear);
  if (error != ESP_OK) { invalidate(error); return; }
  if (front < 0 || front > 4095 || rear < 0 || rear > 4095) {
    invalidate(ESP_ERR_INVALID_RESPONSE); return;
  }
  frontRaw = uint16_t(front); rearRaw = uint16_t(rear);
  if (frontRaw > frontPeak) frontPeak = frontRaw;
  if (rearRaw > rearPeak) rearPeak = rearRaw;
  if (frontRaw == 4095 || rearRaw == 4095) ++clippedPairs;
  if (goodPairs < SensorConfig::PIEZO_GOOD_PAIRS) ++goodPairs;
  atMs = millis(); validPair = true; fresh = true; ++samples; lastError = ESP_OK;
  impact.feed(atMs,micros(),frontRaw,rearRaw,SensorConfig::PIEZO_IMPACT_DETECTION_ENABLED,!settingsFault);
}

void PiezoInputs::loadSettings(){
  settingsOpen=settings.begin("sv_piezo",false);settingsFault=!settingsOpen;
  if(!settingsOpen)return;
  const size_t n=settings.getBytesLength("thresholds");if(!n)return;
  uint8_t b[16]={};
  if(n!=16||settings.getBytes("thresholds",b,16)!=16||b[0]!='P'||b[1]!='Z'||b[2]<1||b[2]>3||b[3]||b[12]||b[13]||CrashProtocol::get16(b+14)!=CrashProtocol::crc16(b,14)) {settingsFault=true;return;}
  const auto f=CrashProtocol::get16(b+4),r=CrashProtocol::get16(b+6);const auto revision=CrashProtocol::get32(b+8);
  const bool legacy=b[2]<3;
  if(f<50||r<50||f>4094||r>4094||!revision||(b[2]==1&&(f>3000||r>3000))||(!legacy&&(!PiezoProtocol::validThreshold(f)||!PiezoProtocol::validThreshold(r)))){settingsFault=true;return;}
  if(legacy){
    // Upgrade validated earlier profiles once to the tested 100-point hit level.
    // Keep the existing revision monotonic; corrupt/failed records are never erased.
    impact.data.revision=revision;
    if(!saveSettings(PiezoProtocol::DEFAULT_THRESHOLD,PiezoProtocol::DEFAULT_THRESHOLD))settingsFault=true;
    return;
  }
  impact.configure(f,r);impact.data.revision=revision;saved=true;
}
bool PiezoInputs::saveSettings(uint16_t front,uint16_t rear){
  if(!settingsOpen||impact.data.revision==UINT32_MAX||!PiezoProtocol::validThreshold(front)||!PiezoProtocol::validThreshold(rear))return false;
  uint8_t b[16]={},verify[16]={};std::memcpy(b,"PZ\3\0",4);
  CrashProtocol::put16(b+4,front);CrashProtocol::put16(b+6,rear);CrashProtocol::put32(b+8,impact.data.revision+1);
  CrashProtocol::put16(b+14,CrashProtocol::crc16(b,14));
  if(settings.putBytes("thresholds",b,16)!=16||settings.getBytesLength("thresholds")!=16||settings.getBytes("thresholds",verify,16)!=16||std::memcmp(b,verify,16)){settingsFault=true;return false;}
  impact.configure(front,rear);++impact.data.revision;saved=true;settingsFault=false;return true;
}
void PiezoInputs::handleCommand(const ImuLabProtocol::Frame& f,EventLink& link,bool busy){
  namespace L=ImuLabProtocol;
  if(f.type!=L::Type::Command||f.length!=40||!f.token||!link.labSessionCurrent(f.session))return;
  if(f.token==lastToken&&f.session==lastSession){if(std::memcmp(f.data,lastCommand.data,40)==0)responsePending=true;return;}
  const auto op=L::Op(f.data[0]);L::Result result=L::Result::Unknown;
  const uint16_t front=L::get16(f.data+2),rear=L::get16(f.data+4);
  bool clean=!f.data[1]&&!f.data[6]&&!f.data[7];for(unsigned i=12;i<40;++i)clean&=f.data[i]==0;
  if(op<L::Op::PiezoBaseline||op>L::Op::PiezoStop)return;
  if(!clean || (op!=L::Op::PiezoThresholds&&(front||rear)))result=L::Result::Range;
  else if(op!=L::Op::PiezoStop && L::get32(f.data+8)!=impact.data.revision)result=L::Result::TrialChanged;
  else if(op==L::Op::PiezoStop){impact.stop(millis(),true);result=L::Result::Accepted;}
  else if(busy||impact.capturing())result=L::Result::Busy;
  else if(!healthy())result=L::Result::Offline;
  else if(op==L::Op::PiezoBaseline){impact.resetBaseline();result=L::Result::Accepted;}
  else if(op==L::Op::PiezoThresholds){
    if(!PiezoProtocol::validThreshold(front)||!PiezoProtocol::validThreshold(rear)||(impact.calibrated()&&(uint32_t(front)+impact.data.bank[0].baseline>=4095||uint32_t(rear)+impact.data.bank[1].baseline>=4095)))result=L::Result::Range;
    else result=saveSettings(front,rear)?L::Result::Accepted:L::Result::Storage;
  }else if(settingsFault||!impact.ready())result=L::Result::Prerequisite;
  else result=impact.start(op==L::Op::PiezoNormal?1:2,millis())?L::Result::Accepted:L::Result::Prerequisite;
  lastToken=f.token;lastSession=f.session;lastCommand=f;
  response=L::Frame();response.type=L::Type::Response;response.boot=link.bootId();response.session=f.session;response.token=f.token;
  response.length=6;response.data[0]=uint8_t(op);response.data[1]=uint8_t(result);responsePending=true;
}
void PiezoInputs::report(EventLink& link,uint32_t candidate,uint32_t decisionAt,bool busy){
  if(busy&&impact.capturing())impact.stop(millis(),true);
  impact.candidate(candidate,decisionAt);
  if(responsePending&&!link.labSessionCurrent(response.session))responsePending=false;
  if(responsePending&&link.sendLab(response))responsePending=false;
  const uint32_t now=millis();if(!link.labActive()||uint32_t(now-reportAt)<200)return;
  namespace P=PiezoProtocol;
  auto& s=impact.data;s.flags=(SensorConfig::PIEZO_IMPACT_DETECTION_ENABLED?P::Enabled:0)|(saved?P::Saved:0)|(settingsFault?P::SettingsFault:0)|(healthy()?P::Healthy:0);
  s.sampleAgeMs=uint16_t(std::min<uint32_t>(65535u,uint32_t(now-atMs)));s.uptime=now;s.sequence=reportSequence;
  ImuLabProtocol::Frame frame;frame.boot=link.bootId();P::encode(s,frame);
  if(link.sendLab(frame)){reportAt=now;++reportSequence;if(!reportSequence)++reportSequence;}
}
