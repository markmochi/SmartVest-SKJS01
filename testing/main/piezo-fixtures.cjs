// Local RAM fixtures; no vest, GPIO, SMS or camera connection.
function fixture(boot=7654){
 const trial=()=>({id:0,epoch:1,state:0,elapsedMs:0,gaps:0,clipped:0,front:{maxRise:0,weakestTap:0,taps:0,captureFloor:0,baseline:0},rear:{maxRise:0,weakestTap:0,taps:0,captureFloor:0,baseline:0}});
 const bank=baseline=>({threshold:100,supportPeak:0,supportAgeMs:null,supportHeadroom:true,ready:true,clipped:false,drift:false,raw:baseline+10,baseline,rise:10,hits:0,lastPeak:0,lastAgeMs:null,imuMatched:false});
 return {supportThreshold:800,supported:true,online:true,fresh:true,ageMs:10,sampleAgeMs:1,boot,sequence:1,revision:1,baselineEpoch:1,enabled:true,saved:false,settingsFault:false,samples:3000,readFailures:0,maxGapUs:1300,front:bank(0),rear:bank(0),normal:trial(),taps:trial(),command:{token:0,state:0,result:0}};
}
function update(state){
 const lab=state.imuLab;if(!lab.piezo)lab.piezo=fixture(lab.imu.boot);const p=lab.piezo;p.sequence++;p.samples+=200;
 lab.imu.detector.piezoImpactDetectionEnabled=lab.imu.online&&p.online&&p.boot===lab.imu.boot&&p.ageMs<1000?Boolean(p.enabled):null;
 lab.imu.detector.piezoCorroborationRequired=false;
 const running=state.piezoRunning;
 if(running){
  const trial=p[running.kind],elapsed=Math.min(15000,Math.floor((Date.now()-running.at)/(state.piezoTrialScale||1)));
  trial.elapsedMs=elapsed;
  if(elapsed>=15000){
   trial.state=2;trial.front.maxRise=running.kind==='normal'?30:900;trial.rear.maxRise=running.kind==='normal'?40:1000;
   if(running.kind==='taps')for(const [i,key]of ['front','rear'].entries()){trial[key].weakestTap=i?1000:900;trial[key].taps=4;if(trial[key].weakestTap>=p[key].threshold){p[key].hits+=4;p[key].lastPeak=trial[key].weakestTap;p[key].lastAgeMs=50;}state.piezoHitAt??={};state.piezoHitAt[key]=Date.now()-50;}
   state.piezoRunning=null;
  }
 }
 for(const key of ['front','rear'])if(state.piezoHitAt?.[key]!==undefined)p[key].lastAgeMs=Date.now()-state.piezoHitAt[key];
 return p;
}
function handle(state,body,json){
 const p=update(state),actions={baseline:11,thresholds:12,normal:13,taps:14,stop:15};
 if(!actions[body.action]||!Number.isInteger(body.revision)||Object.keys(body).length!==(body.action==='thresholds'?4:2))return json({error:'Invalid impact action.'},400);
 if(body.action!=='stop'&&body.revision!==p.revision)return json({error:'Settings changed. Refresh and try again.'},409);
 if(!p.online||!p.fresh)return json({error:'Fresh impact readings are unavailable.'},409);
 if(state.piezoRunning&&body.action!=='stop')return json({error:'Finish the current sensing action first.'},409);
 if(state.piezoFailure)return json({error:state.piezoFailure},409);
 if(body.action==='thresholds'){
  if(['front','rear'].some(key=>!Number.isInteger(body[key])||body[key]<50||body[key]>800))return json({error:'Use whole numbers from 50 to 800.'},400);
  for(const key of ['front','rear'])p[key].threshold=body[key];p.revision++;p.saved=true;
 }else if(body.action==='baseline'){
  p.baselineEpoch++;for(const key of ['front','rear'])p[key].ready=false;
  p.normal={...fixture().normal,epoch:p.baselineEpoch};p.taps={...fixture().taps,epoch:p.baselineEpoch};
  setTimeout(()=>{for(const key of ['front','rear'])p[key].ready=true;},200);
 }else if(body.action==='stop'){
  if(state.piezoRunning)p[state.piezoRunning.kind].state=3;state.piezoRunning=null;
 }else{
  if(body.action==='taps'&&p.normal.state!==2)return json({error:'Record movement before taps.'},409);
  if(body.action==='normal')p.taps={...fixture().taps,epoch:p.baselineEpoch};
  const t=p[body.action];t.id++;t.epoch=p.baselineEpoch;t.elapsedMs=0;t.state=1;
  state.piezoRunning={kind:body.action,at:Date.now()};
 }
 const command={token:(state.imuLab.imu.command.token||0)+1,operation:actions[body.action],state:2,result:0};
 state.imuLab.imu.command=command;p.command={...command};p.sequence++;
 return json({token:command.token,state:'queued'},202);
}
module.exports={fixture,update,handle};
