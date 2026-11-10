import {api,session} from '../shared/api.js';
import {$,esc,panel,toast} from '../shared/dom.js';
const HIT_THRESHOLD=100,SUPPORT_THRESHOLD=800;
let latest=null,at=0,transportMs=0,busy=false,dirty=false,boot=0,notify=()=>{},controller=null,mountId=0;
const text=(id,value)=>{const el=$(id);if(el&&el.textContent!==String(value))el.textContent=String(value);};
const mounted=()=>Boolean($('piezoPanel'));
const current=()=>latest?.fresh&&Date.now()-at+transportMs+Number(latest.ageMs||0)+Number(latest.sampleAgeMs||0)<1200;
export const piezoBusy=()=>busy;
export function piezoMarkup(){return panel('Impact sensors',`<div id="piezoPanel"><div class="impact-heading"><p id="piezoState" role="status">Waiting for impact readings</p><span id="piezoSaved" class="hint"></span></div><div class="impact-banks">${['Front','Rear'].map(name=>{const key=name.toLowerCase();return `<section class="impact-bank" id="${key}Impact"><h3>${name} bank <span class="hint">4 sensors</span></h3><p id="${key}Hit" class="impact-reading">—</p><div class="impact-track" role="meter" aria-label="${name} impact strength" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"><span id="${key}ImpactFill"></span></div><p id="${key}ImpactState" class="hint">Waiting</p><p id="${key}LastHit" class="hint"></p><div class="field"><label for="${key}ImpactThreshold">${name} hit threshold</label><input id="${key}ImpactThreshold" type="number" min="50" max="800" step="1" inputmode="numeric" data-write></div></section>`;}).join('')}</div><div class="actions section-gap"><button id="saveImpactThresholds" data-write>Save thresholds</button><button id="zeroImpact" class="secondary" data-write>Reset resting level</button></div><p id="piezoHelp" class="hint">Impacts register at 100 points above rest by default. A peak of at least 800 points on either bank can support an IMU crash decision. The IMU checks continuously and can qualify without an impact. An impact alone never sends an alert.</p><div class="impact-trial"><h3>Find a starting threshold</h3><p class="hint">1. Record everyday movement. 2. Record controlled taps on each sensor.</p><div class="actions section-gap"><button id="normalImpactTrial" class="secondary" data-write>Record movement · 15 s</button><button id="tapImpactTrial" class="secondary" data-write>Record taps · 15 s</button><button id="stopImpactTrial" class="secondary" data-write>Stop</button></div><progress id="impactProgress" max="15000" value="0" aria-label="Impact trial progress"></progress><p id="impactTrialState" role="status">No impact trial recorded.</p><div id="impactReport"></div><div class="actions section-gap"><button id="useImpactSuggestion" class="secondary" disabled>Use starting values</button><button id="saveImpactReport" class="secondary" disabled>Save report</button></div><p class="hint">These trials do not trigger alerts. IMU crash monitoring stays on.</p></div><p id="impactActionMessage" class="form-message" role="status"></p><details class="section-gap"><summary>Readings and test details</summary><div id="impactDetails" class="hint section-gap"></div><p class="hint">Raw front / rear: <span id="piezoFront">Unavailable</span> / <span id="piezoRear">Unavailable</span></p><p class="hint">Readings cover two groups, so the vest cannot identify which individual sensor was tapped. Reports stay until a restart or a new trial pair.</p></details></div>`);}
export function suggestion(p){
 const n=p?.normal,t=p?.taps;
 if(!n?.id||!t?.id||n.state!==2||t.state!==2)return {message:'Complete both trials to compare the readings.'};
 if(n.epoch!==p.baselineEpoch||t.epoch!==p.baselineEpoch||n.elapsedMs<15000||t.elapsedMs<15000||n.gaps||t.gaps||n.clipped||t.clipped)return {message:'Readings were interrupted or reached their limit. Repeat both trials.'};
 const values=[];
 for(const bank of ['front','rear']){
  const noise=n[bank],tap=t[bank];
  if(!noise||!tap||tap.taps<4)return {message:'Record at least four clear taps on each bank, then compare again.'};
  if(Math.abs(noise.baseline-tap.baseline)>100)return {message:'The resting level changed. Reset it and repeat both trials.'};
  const low=Math.max(HIT_THRESHOLD,noise.maxRise+Math.max(50,Math.ceil(noise.maxRise*.25)));
  const high=Math.min(SUPPORT_THRESHOLD,tap.weakestTap-Math.max(25,Math.ceil(tap.weakestTap*.1)),4094-tap.baseline);
  if(low>=high)return {message:'Movement and taps overlap, or the required hit threshold would exceed 800 points. Repeat controlled trials before changing the 100-point default.'};
  values.push(Math.max(low,Math.min(high,Math.round((low+high)/20)*10)));
 }
 return {values,message:'Clear separation in these trials. Try these starting values, then retest.'};
}
function controls(){
 if(!mounted())return;const active=latest?.normal?.state===1||latest?.taps?.state===1;
 const ready=current()&&latest.enabled&&latest.front?.ready&&latest.rear?.ready&&!latest.settingsFault;
 const blocked=busy||latest?.command?.state===1||latest?.imuBusy||!session.connected;
 for(const id of ['saveImpactThresholds','zeroImpact','normalImpactTrial','tapImpactTrial','stopImpactTrial']){
  const el=$(id);const stop=id==='stopImpactTrial';const trial=id==='normalImpactTrial'||id==='tapImpactTrial';
  const off=blocked||!latest?.online||(!stop&&!current())||(stop?!active:active)||(trial&&!ready)||(id==='tapImpactTrial'&&latest?.normal?.state!==2);
  el.dataset.blocked=String(off);el.disabled=off;
 }
 for(const key of ['front','rear']){const el=$(key+'ImpactThreshold'),off=blocked||active||!latest?.online;el.dataset.blocked=String(off);el.disabled=off;}
 $('useImpactSuggestion').disabled=blocked||active||!current()||!suggestion(latest).values;
 $('saveImpactReport').disabled=!latest?.normal?.id||busy;
}
export function expirePiezo(){
 if(!mounted())return;
 if(!current()){
  text('piezoState',latest?.supported?'Impact readings unavailable':'Update both boards for impact detection');
  for(const [index,key] of ['front','rear'].entries()){
   text(key+'Hit','—');text(key+'ImpactState','Waiting for fresh readings');if(latest?.[key]?.lastPeak)text(key+'LastHit','Earlier hit '+latest[key].lastPeak+' · live confirmation unavailable');text(index?'piezoRear':'piezoFront','Unavailable');
   $(key+'ImpactFill').style.width='0%';$(key+'ImpactFill').parentElement.setAttribute('aria-valuenow','0');$(key+'Impact').classList.remove('has-hit');
  }
 }
 controls();
}
export function updatePiezo(data,meta={}){
 if(!mounted())return;const p=data?.piezo||{};
 if(boot&&p.boot!==boot){dirty=false;controller?.abort();text('impactActionMessage','Sensing restarted. Check the current readings.');}
 boot=p.boot||0;latest={...p,imuHealthy:Boolean(data?.imu?.online&&data?.imu?.healthy),imuBusy:data?.imu?.command?.state===1||data?.imu?.calibration?.result===1||[1,2].includes(data?.imu?.trial?.state)};at=Date.now();transportMs=Number(meta.elapsedMs||0);
 const fresh=current();
 text('piezoSaved',p.settingsFault?'Threshold storage needs attention':p.saved?'Thresholds saved':'100-point hit threshold · not saved');
 text('piezoState',fresh?(p.settingsFault?'Save thresholds to restore impact detection':!p.enabled?'Impact detection is disabled':p.front?.clipped||p.rear?.clipped?'Check an input reading':p.front?.ready&&p.rear?.ready?'Listening for impacts':'Learning resting levels · keep sensors untouched'):'Waiting for fresh impact readings');
 for(const [index,key] of ['front','rear'].entries()){
  const b=p[key]||{};const recent=fresh&&b.lastPeak>0&&Number.isFinite(b.lastAgeMs)&&b.lastAgeMs+transportMs<2000;
  const shown=recent?Math.max(b.rise||0,b.lastPeak||0):b.rise;
  text(key+'Hit',fresh?shown+' points':'—');
  text(key+'ImpactState',!fresh?'Readings unavailable':b.clipped?'Input at limit · check the sensor':b.drift?'Resting level moved · reset it':!b.ready?(b.baseline+b.threshold>=4095?'Insufficient ADC range above rest · check the input circuit':'Hold still while the resting level is learned'):b.supportHeadroom===false?'Small impacts available · insufficient range for 800-point support':recent?'Impact detected · recent peak':'Listening · '+(b.hits||0)+' hits');
  text(key+'LastHit',b.lastPeak?'Latest hit '+b.lastPeak+' · '+(b.imuMatched?'Strong hit '+(b.supportPeak||SUPPORT_THRESHOLD)+' matched an IMU crash pattern':!latest.imuHealthy?'IMU unavailable':b.lastPeak<SUPPORT_THRESHOLD?'Below the 800-point support level':Number.isFinite(b.lastAgeMs)&&b.lastAgeMs+transportMs<4500?'Checking rider motion':'IMU crash rule not met'):'No impact recorded');
  $(key+'Impact').classList.toggle('has-hit',recent);
  const fill=fresh?Math.min(100,Math.round(Number(shown||0)/Math.max(1,Number(b.threshold||HIT_THRESHOLD)*2)*100)):0;
  $(key+'ImpactFill').style.width=fill+'%';$(key+'ImpactFill').parentElement.setAttribute('aria-valuenow',String(fill));
  if(!dirty&&document.activeElement!==$(key+'ImpactThreshold'))$(key+'ImpactThreshold').value=b.threshold||HIT_THRESHOLD;
  text(index?'piezoRear':'piezoFront',fresh?String(b.raw):'Unavailable');
 }
 const active=p.normal?.state===1?p.normal:p.taps?.state===1?p.taps:null;
 $('impactProgress').value=active?.elapsedMs||0;
 text('impactTrialState',active?(active===p.normal?'Movement trial':'Tap trial')+' · '+Math.ceil((15000-active.elapsedMs)/1000)+' s remaining':p.normal?.state===3||p.taps?.state===3?'Trial interrupted · repeat it':p.taps?.state===2?'Trials recorded':p.normal?.state===2?'Movement recorded · record taps next':'No impact trial recorded.');
 const advice=suggestion(p);let report='<p class="hint section-gap">'+esc(advice.message)+'</p>';
 if(p.normal?.id)report+='<div class="table-wrap"><table class="impact-results"><thead><tr><th>Bank</th><th>Movement peak</th><th>Weakest tap</th><th>Try</th></tr></thead><tbody>'+['front','rear'].map((key,i)=>'<tr><td>'+['Front','Rear'][i]+'</td><td>'+Number(p.normal[key]?.maxRise||0)+'</td><td>'+(p.taps?.[key]?.taps?Number(p.taps[key].weakestTap):'—')+'</td><td>'+(advice.values?.[i]??'—')+'</td></tr>').join('')+'</tbody></table></div>';
 if($('impactReport').innerHTML!==report)$('impactReport').innerHTML=report;
 text('impactDetails','Front: now '+(fresh?p.front?.raw:'unavailable')+', resting '+(fresh?p.front?.baseline:'unavailable')+'. Rear: now '+(fresh?p.rear?.raw:'unavailable')+', resting '+(fresh?p.rear?.baseline:'unavailable')+'. Strong impact support starts at 800 points above rest. Readings are not force.');
 expirePiezo();
}
async function send(action){
 if(busy||(!current()&&!(action==='stop'&&latest?.online)))return;
 const before=latest,body={action,revision:before.revision};
 if(action==='thresholds'){
  for(const key of ['front','rear']){const n=Number($(key+'ImpactThreshold').value);if(!Number.isInteger(n)||n<50||n>SUPPORT_THRESHOLD){toast('Use whole numbers from 50 to 800.',true);return;}body[key]=n;}
 }
 busy=true;notify();controls();controller?.abort();controller=new AbortController();const signal=controller.signal,generation=session.generation,owner=mountId;
 text('impactActionMessage','Sending…');
 try{
  const reply=await api('sensing/piezo','POST',body,{signal}),until=Date.now()+6000;
  while(mounted()&&!signal.aborted&&generation===session.generation&&Date.now()<until){
   const start=Date.now(),data=await api('sensing/imu','GET',undefined,{signal});updatePiezo(data,{elapsedMs:Date.now()-start});
   const p=data.piezo||{},c=p.command;
   if(p.boot!==before.boot)throw Error('The sensing board restarted. Check the current readings.');
   if(c?.token===reply.token&&c.state!==1){
    if(c.state!==2)throw Error(['Accepted','Finish the current sensing action first.','Fresh readings are unavailable.','Wait for resting levels, or record movement before taps.','Check threshold range and input headroom.','The sensing board restarted.','Update both boards for this action.','Settings changed. Refresh and try again.','Thresholds could not be verified in storage.'][c.result]||'The action could not be confirmed. Check the current readings.');
    const changed=action==='thresholds'?p.revision!==before.revision&&p.saved&&p.front?.threshold===body.front&&p.rear?.threshold===body.rear:action==='baseline'?p.baselineEpoch!==before.baselineEpoch:action==='normal'?p.normal?.id!==before.normal?.id:action==='taps'?p.taps?.id!==before.taps?.id:![p.normal?.state,p.taps?.state].includes(1);
    if(changed&&p.sequence!==before.sequence){dirty=false;text('impactActionMessage',action==='thresholds'?'Thresholds saved on the vest.':action==='baseline'?'Keep both banks untouched while the resting levels are learned.':action==='stop'?'Trial stopped.':'Recording on the vest.');return;}
   }
   await new Promise(resolve=>setTimeout(resolve,150));
  }
  if(!signal.aborted&&mounted())throw Error('Confirmation timed out. Check the current values before retrying.');
 }catch(error){if(!signal.aborted&&mounted()&&generation===session.generation){text('impactActionMessage',error.message);toast(error.message,true);}}
 finally{if(owner===mountId){busy=false;if(mounted()){notify();controls();}}}
}
export function mountPiezo(onBusy=()=>{}){
 controller?.abort();++mountId;latest=null;dirty=false;boot=0;busy=false;notify=onBusy;
 for(const key of ['front','rear'])$(key+'ImpactThreshold').addEventListener('input',()=>{dirty=true;});
 for(const [id,action] of [['saveImpactThresholds','thresholds'],['zeroImpact','baseline'],['normalImpactTrial','normal'],['tapImpactTrial','taps'],['stopImpactTrial','stop']])$(id).onclick=()=>send(action);
 $('useImpactSuggestion').onclick=()=>{const value=suggestion(latest).values;if(!value)return;['front','rear'].forEach((key,i)=>{$(key+'ImpactThreshold').value=value[i];});dirty=true;text('impactActionMessage','Starting values filled in. Save thresholds to apply them.');};
 $('saveImpactReport').onclick=()=>{if(!latest)return;const report={format:'smartvest-impact-trial-v1',capturedAt:new Date().toISOString(),unit:'ADC rise points; not force',hitDefault:HIT_THRESHOLD,supportBaseline:SUPPORT_THRESHOLD,status:latest,comparison:suggestion(latest),note:'Movement and controlled-tap comparison; the independent IMU crash rule is unchanged.'};const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download='SmartVest_impact_trials_'+latest.boot+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
 const end=()=>{controller?.abort();window.removeEventListener('pagehide',end);window.removeEventListener('vestsignout',end);};window.addEventListener('pagehide',end,{once:true});window.addEventListener('vestsignout',end,{once:true});controls();
}
