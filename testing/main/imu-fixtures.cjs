// Simulated IMU data for browser checks, never experimental crash evidence.
const profile={sensorModel:'BMI323',interface:'SPI',nominalRateHz:200,accelLsbPerG:2048,gyroLsbPerDps:16.384,temperatureLsbPerC:512,temperatureOffsetC:23,accConfig:0x70b9,gyroConfig:0x70c9,gapWarningUs:7500};
function fixture(){return {mainFirmware:'1.0',link:{online:true,heartbeatAgeMs:50},imu:{schemaVersion:2,profile,detector:{valid:false,state:1,candidates:0},online:true,healthy:true,firmware:'1.0',boot:7654,whoAmI:67,sample:{timeUs:5000000,sequence:1000,accelRaw:[100,0,2048],gyroRaw:[16,-8,3],temperatureRaw:100,piezoFrontRaw:null,piezoRearRaw:null,flags:0},calibration:{flags:0,operation:0,result:0,samples:0,gyroBiasDps:[16/16.384,-8/16.384,3/16.384]},pose:{valid:false,sideDeg:8,forwardDeg:12,gravityTrust:.9},diagnostics:{error:0,phase:1,register:0,observed:0,expected:0,probeMask:2,probeIds:[0,112],samples:1000,failures:0,gapEvents:0,sampleIntervalUs:5000,measuredRateHz:200,rejectedFrames:0},command:{token:0,operation:0,state:0,result:0}}};}
function handle(state,endpoint,body,json){
 if(!state.imuLab)state.imuLab=fixture();const lab=state.imuLab,i=lab.imu;
 if(endpoint==='sensing/piezo')return require('./piezo-fixtures.cjs').handle(state,body,json);
 if(endpoint==='sensing/imu'){require('./piezo-fixtures.cjs').update(state);i.sample.sequence+=40;i.sample.timeUs+=200000;return json(lab);}
 if(endpoint!=='sensing/command')return false;
 if(['start','stop','read'].includes(body.action))return json({error:'Unknown IMU action.'},400);
 const codes={retry:1,gyro:2,upright:3,forward:4,neutral:5,clear_body:6,reset_candidate:10},token=i.command.token+1,op=codes[body.action];
 i.command={token,operation:op,state:2,result:0};
 if(['gyro','upright','forward','neutral'].includes(body.action)){
  i.calibration.operation=op;i.calibration.result=1;i.calibration.samples=0;
  setTimeout(()=>{i.calibration.result=2;i.calibration.samples=240;i.calibration.revision=(i.calibration.revision||0)+1;i.calibration.flags=body.action==='upright'?9:body.action==='forward'?11:body.action==='neutral'?15:i.calibration.flags|1;i.pose.valid=Boolean(i.calibration.flags&2);i.sample.flags=i.pose.valid?100:96;i.sample.temperatureValid=true;},120);
 }else if(body.action==='clear_body'){i.calibration.flags&=1;i.pose.valid=false;i.sample.flags&=~4;}
 else if(body.action==='reset_candidate'){i.detector.state=1;}
 json({token,state:'queued'},202);return true;
}
module.exports={fixture,handle};
