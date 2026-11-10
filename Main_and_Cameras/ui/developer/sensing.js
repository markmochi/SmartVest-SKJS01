import {piezoMarkup,mountPiezo,updatePiezo,expirePiezo,piezoBusy} from './piezo.js';
import {riderPositionMarkup,mountRiderPosition} from '../shared/rider-position.js';
import {api,session} from '../shared/api.js';
import {$,num,age,kv,panel,notice,toast,confirmAction,renderContent} from '../shared/dom.js';
import {startPolling} from '../shared/poller.js';
import {profileFor,imuAvailability,boardCommunication} from './imu-data.js';
const errorNames=['None','SPI bus start failed','BMI323 identity mismatch','BMI323 configuration mismatch','Rejected sensor frame','No fresh sensor data'];
const phases=['Waiting to retry','Acquiring fresh BMI323 samples'];
const resultNames=['Accepted','Another position capture is active','IMU is offline','Complete the earlier calibration step first','Invalid request','Sensing board restarted; refresh','Unsupported command','Unsupported request','Calibration storage failed'];
const hex=n=>'0x'+Number(n||0).toString(16).toUpperCase().padStart(2,'0');
const wait=ms=>new Promise(r=>setTimeout(r,ms));
let latest=null,receivedAt=0,writing=false,history=[],historyBoot=0,lastSequence=0,plotTime=0,lastSensorUs=0;
let riderView=null,sensingPoll=null,mountContext=null;
function saveFile(name,body,type){const url=URL.createObjectURL(new Blob([body],{type})),a=document.createElement('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),10000);}
function mounted(){return Boolean($('imuLiveStatus'));}
function setText(id,text){const el=$(id);if(el)el.textContent=text;}
function block(id,blocked){const b=$(id);if(!b)return;b.dataset.blocked=String(blocked);b.disabled=blocked||!session.connected;}
function controls(){
 if(!mounted())return;
 const i=latest?.imu||{},pending=i.command?.state===1,busy=writing||pending||piezoBusy()||latest?.piezo?.normal?.state===1||latest?.piezo?.taps?.state===1||i.calibration?.result===1,online=i.online&&Date.now()-receivedAt<2000;
 block('resetCandidate',busy||!online);block('retryImu',busy||!online);block('clearBody',busy||!online);block('snapshotImu',!online);
}
function updateCommunication(stale=false){
 const state=boardCommunication(latest?.imu,latest?.link,stale);
 setText('boardLinkStatus',state.title);setText('boardLinkDescription',state.description);
 const card=$('boardCommunication');if(card)card.dataset.state=state.tone;
 const status=$('boardLinkStatus');if(status)status.className='status '+state.tone;
}
let plotPalette,disposeCharts;
function chartTheme(){
 const styles=getComputedStyle(document.documentElement),value=name=>styles.getPropertyValue(name).trim();
 plotPalette={colors:['x','y','z','magnitude'].map(name=>value('--plot-'+name)),line:value('--line'),text:value('--muted')};
 if(mounted()){drawPlot($('accelPlot'),'accel','g');drawPlot($('gyroPlot'),'gyro','°/s');}
}
function mountChartTheme(){
 disposeCharts?.();plotPalette=null;
 const stop=()=>{window.removeEventListener('themechange',chartTheme);window.removeEventListener('vestsignout',stop);window.removeEventListener('pagehide',stop);if(disposeCharts===stop)disposeCharts=null;};
 window.addEventListener('themechange',chartTheme);window.addEventListener('vestsignout',stop);window.addEventListener('pagehide',stop);disposeCharts=stop;
}
function drawPlot(canvas,series,units){
 if(!canvas||canvas.closest('[hidden]'))return;
 if(!plotPalette)chartTheme();
 const w=canvas.clientWidth||400,h=185,scale=devicePixelRatio||1,ctx=canvas.getContext('2d');if(canvas.width!==Math.round(w*scale)||canvas.height!==Math.round(h*scale)){canvas.width=Math.round(w*scale);canvas.height=Math.round(h*scale);}ctx.setTransform(scale,0,0,scale,0,0);ctx.clearRect(0,0,w,h);
 const colors=plotPalette.colors,left=42,right=w-12,top=12,bottom=155;
 let max=series==='accel'?1.5:10;for(const point of history)for(const v of point[series])max=Math.max(max,Math.abs(v)*1.1);
 const y=v=>bottom-(v+max)/(2*max)*(bottom-top);ctx.font='10px system-ui';ctx.strokeStyle=plotPalette.line;ctx.fillStyle=plotPalette.text;ctx.textAlign='right';
 for(const value of [-max,0,max]){ctx.beginPath();ctx.moveTo(left,y(value));ctx.lineTo(right,y(value));ctx.stroke();ctx.fillText(value.toFixed(max<10?1:0),left-5,y(value)+3);}
 ctx.textAlign='left';ctx.fillText(units,left,178);ctx.textAlign='right';ctx.fillText('30 s preview · ~2 updates/s',right,178);
 if(history.length<2)return;const end=history.at(-1).time,start=end-30;
 for(let j=0;j<4;j++){ctx.strokeStyle=colors[j];ctx.lineWidth=j===3?1.8:1.3;ctx.beginPath();let pen=false,prev=0;for(const point of history){if(point.time<start)continue;const x=left+(point.time-start)/30*(right-left);if(!pen||point.time-prev>1)ctx.moveTo(x,y(point[series][j]));else ctx.lineTo(x,y(point[series][j]));pen=true;prev=point.time;}ctx.stroke();}
}
function update(data,meta={}){
 if(data.rider){const rider={...data.rider,ageMs:(data.rider.ageMs??10000)+(meta.elapsedMs||0)};riderView?.update(rider);}
 latest=data;receivedAt=Date.now();updateCommunication();const i=data.imu||{},l=data.link||{},sample=i.sample||{},c=i.calibration||{},d=i.diagnostics||{};
 let profile=null;try{profile=profileFor(i);}catch{}const valid=i.online&&i.healthy&&profile;
 // Heartbeat readiness is not equivalent to a verified V2 stream.
 const availability=imuAvailability(i,l,Boolean(profile));
 setText('imuLiveStatus',i.online&&!i.healthy&&profile?errorNames[d.error]||availability.status:availability.status);
 $('imuLiveStatus').className='notice '+(valid?'good':'warn');
 setText('imuDiagnosis',i.online&&profile&&!i.healthy
  ?(errorNames[d.error]||'Sensor acquisition failed')+' · '+(phases[d.phase]||'Unknown stage')
  :availability.reason);
 const a=valid&&sample.accelRaw?sample.accelRaw.map(v=>v/profile.accelLsbPerG):null,g=valid&&sample.gyroRaw?sample.gyroRaw.map((v,j)=>v/profile.gyroLsbPerDps-((c.flags&1)?c.gyroBiasDps[j]:0)):null;
 setText('accelMagnitude',a?Math.hypot(...a).toFixed(3)+' g':'Unavailable');setText('gyroMagnitude',g?Math.hypot(...g).toFixed(2)+' °/s':'Unavailable');
 setText('imuRate',valid?num(d.measuredRateHz,1,' Hz'):'Unavailable');setText('imuTemperature',valid&&sample.temperatureValid?num(sample.temperatureRaw/profile.temperatureLsbPerC+profile.temperatureOffsetC,1,' °C'):'Unavailable');
 setText('axisValues',a?'Accel X/Y/Z: '+a.map(v=>v.toFixed(3)).join(' / ')+' g':'Accel X/Y/Z unavailable');
 setText('gyroValues',g?((c.flags&1)?'Bias-corrected gyro':'Uncalibrated gyro')+' X/Y/Z: '+g.map(v=>v.toFixed(2)).join(' / ')+' °/s':'Gyro X/Y/Z unavailable');
 const detector=i.detector||{};const detectionReady=Boolean(detector.automaticDetectionEnabled&&detector.mainAutomaticDetectionEnabled&&valid&&detector.automaticReady);$('automaticDetectionStatus').className='notice '+(detectionReady?'good':'warn');setText('automaticDetectionStatus',detector.automaticDetectionEnabled&&detector.mainAutomaticDetectionEnabled?(valid&&detector.automaticReady?'Detection ready - Qualifying motion triggers camera protection and alerts to configured contacts.':'Detection waiting - Complete calibration; check fresh IMU, piezo input health and the Main connection.'):'Detection unavailable - Automatic alerts are disabled.');setText('fallDiagnostic',detector.valid?['Unavailable','Waiting for quiet reference','Waiting','Evaluating motion','Fall candidate','Recovering'][detector.state]||'Unknown state':'Evaluation unavailable');setText('fallEvidence','Candidates: '+num(detector.candidates||0)+' · Rotation hold: '+num(detector.rotationHoldMs||0,0)+' ms · Tilt hold: '+num(detector.tiltHoldMs||0,0)+' ms · Posture change: '+num(detector.postureChangeDeg||0,1)+'°');block('resetCandidate',!i.online||writing);
 setText('imuTabState',c.result===1?'Saving position':'');
 setText('piezoTabState',data.piezo?.normal?.state===1||data.piezo?.taps?.state===1?'Recording':'');
 renderContent(kv([['Main / sensing firmware',(data.mainFirmware||'Unknown')+' / '+(i.online?i.firmware:'Not observed')],['BMI323 chip ID',i.online?hex(i.whoAmI):'Not observed'],['Sensing SPI wiring','SCK18 · MISO19 · MOSI23 · CS21 · 3.3 V · GND'],['SPI bus / nominal sampling','Mode 0 · 100 kHz / 200 Hz'],['Sensor rejected / duplicate / not ready',num(d.sensorRejectedFrames)+' / '+num(d.duplicatePolls)+' / '+num(d.notReadyPolls)],['Latest acquisition interval',valid?num(d.sampleIntervalUs,0,' µs'):'Unavailable'],['Register / observed / expected',hex(d.register)+' / '+hex(d.observed)+' / '+hex(d.expected)],['IMU failures / timing gaps',num(d.failures)+' / '+num(d.gapEvents)],['UART rejected frames',num(d.rejectedFrames)],['Main UART received bytes / valid heartbeats',num(d.uartBytesReceived)+' / '+num(d.uartValidHeartbeats)],['V2 lab frames / accepted live frames / hello sent',num(d.uartLabFramesReceived)+' / '+num(d.uartLiveAccepted)+' / '+num(d.uartHelloSent)],['V2 live data age',age(d.uartLabLastAcceptedAgeMs)],['Heartbeat reports sensor ready',l.online?(l.imuReady?'Yes':'No'):'Unknown (board offline)'],['Sensor boot / sample sequence',num(i.boot)+' / '+(valid?num(sample.sequence):'Unavailable')],['Last heartbeat',age(l.heartbeatAgeMs)],['Front / rear piezo',sample.flags&16?num(sample.piezoFrontRaw)+' / '+num(sample.piezoRearRaw):'Readings unavailable']]),'imuDiagnostics');
 if(i.boot!==historyBoot){history=[];historyBoot=i.boot;lastSequence=0;lastSensorUs=0;plotTime=0;}
 if(valid&&a&&g&&sample.sequence!==lastSequence){if(lastSequence)plotTime+=((sample.timeUs-lastSensorUs)>>>0)/1e6;lastSensorUs=sample.timeUs;lastSequence=sample.sequence;history.push({time:plotTime,accel:[...a,Math.hypot(...a)],gyro:[...g,Math.hypot(...g)]});history=history.filter(p=>plotTime-p.time<=30).slice(-180);}
 drawPlot($('accelPlot'),'accel','g');drawPlot($('gyroPlot'),'gyro','°/s');updatePiezo(data,meta);controls();
}
async function send(action,extra={}){
 const context=mountContext;if(writing||!context?.isCurrent())return;writing=true;controls();setText('imuActionMessage','Sending request…');
 try{const reply=await api('sensing/command','POST',{action,...extra});if(!context.isCurrent())return;const until=Date.now()+4500;
  while(Date.now()<until&&context.isCurrent()){const status=await api('sensing/imu','GET',undefined,{signal:context.signal});if(!context.isCurrent())return;update(status);const c=status.imu?.command;if(c?.token===reply.token&&c.state!==1){if(c.state===4)throw new Error('Sensing acknowledgement timed out. Check the current status before retrying.');if(c.state!==2)throw new Error(resultNames[c.result]||'Sensing command rejected.');setText('imuActionMessage','Accepted by the Sensing board.');return;}await wait(150);}
  throw new Error('Sensing acknowledgement unavailable. Refresh before retrying.');
 }catch(error){if(context.isCurrent()&&error.name!=='AbortError'){setText('imuActionMessage',error.message);toast(error.message,true);}}finally{if(context.isCurrent()){writing=false;controls();}}
}
function mountSensingTabs(){
 const names=['imu','piezo'],buttons=names.map(name=>$('sensingTab'+name));
 function select(name,focus=false){
  for(const [index,key] of names.entries()){
   const selected=key===name;buttons[index].setAttribute('aria-selected',String(selected));buttons[index].tabIndex=selected?0:-1;$('sensing'+key).hidden=!selected;
  }
  if(focus)buttons[names.indexOf(name)].focus({preventScroll:true});
  if(name==='imu'){drawPlot($('accelPlot'),'accel','g');drawPlot($('gyroPlot'),'gyro','°/s');}
 }
 for(const [index,button] of buttons.entries()){
  button.onclick=()=>{select(names[index]);window.history.replaceState(null,'','#'+names[index]);$('sensingStart').scrollIntoView({block:'start',behavior:'instant'});};
  button.onkeydown=event=>{let next;if(event.key==='ArrowRight'||event.key==='ArrowLeft')next=1-index;else if(event.key==='Home')next=0;else if(event.key==='End')next=1;else return;event.preventDefault();buttons[next].click();buttons[next].focus({preventScroll:true});};
 }
 select(location.hash==='#piezo'?'piezo':'imu');
}
export function mountSensing(context={isCurrent:()=>true}){
 mountContext=context;latest=null;receivedAt=0;writing=false;
 sensingPoll=null;riderView?.dispose();riderView=null;
 mountChartTheme();
 $('pageContent').innerHTML=`<span id="sensingStart" aria-hidden="true"></span><section id="boardCommunication" class="board-communication" data-state="neutral" aria-labelledby="boardLinkHeading"><div class="board-link-top"><div><p class="label">Board connection</p><h2 id="boardLinkHeading">Main ↔ Sensing</h2></div><span id="boardLinkStatus" class="status neutral" role="status" aria-live="polite">Checking connection</span></div><p id="boardLinkDescription">Waiting for a current update from your vest.</p></section><div class="sensing-tabs" role="tablist" aria-label="Sensing modules"><button id="sensingTabimu" role="tab" aria-selected="true" aria-controls="sensingimu" type="button">IMU &amp; rider<span id="imuTabState" class="sensing-tab-state"></span></button><button id="sensingTabpiezo" role="tab" aria-selected="false" aria-controls="sensingpiezo" tabindex="-1" type="button">Piezo impacts<span id="piezoTabState" class="sensing-tab-state"></span></button></div><section id="sensingimu" class="sensing-view" role="tabpanel" aria-labelledby="sensingTabimu" tabindex="0">`+riderPositionMarkup({admin:true})+`<div id="automaticDetectionStatus" class="notice warn" role="status">Checking detection readiness...</div>`+
 panel('Fall detection',`<p id="fallDiagnostic" role="status">Waiting for valid calibration and samples</p><p id="fallEvidence" class="hint"></p><p>Balanced: rotation ≥120°/s for 100 ms, posture change ≥30°, and tilt ≥60° for 800 ms within 4.5 s. All conditions must qualify before an incident is created and alerts are queued.</p><button id="resetCandidate" class="secondary" data-write>Reset fall candidate</button>`)+
 `<div id="imuLiveStatus" class="notice warn" role="status">Waiting for sensing board…</div><p id="imuDiagnosis" class="hint section-gap"></p>
 <div class="metric-strip section-gap">${[['Acceleration magnitude','accelMagnitude'],['Rotation magnitude','gyroMagnitude'],['Measured acquisition rate','imuRate'],['Die temperature','imuTemperature']].map(([label,id])=>`<div class="metric"><span class="label">${label}</span><span id="${id}" class="value">Unavailable</span></div>`).join('')}</div>
 <div class="split section-gap">${panel('Acceleration history',`<canvas id="accelPlot" class="imu-canvas" aria-label="Live acceleration preview"></canvas><p id="axisValues" class="hint"></p><p class="imu-legend"><span>X</span><span>Y</span><span>Z</span><span>Magnitude</span></p>`)}${panel('Rotation history',`<canvas id="gyroPlot" class="imu-canvas" aria-label="Live gyroscope preview"></canvas><p id="gyroValues" class="hint"></p><p class="imu-legend"><span>X</span><span>Y</span><span>Z</span><span>Magnitude</span></p>`)}</div>
 <p class="hint section-gap">Use <a href="/admin/ride-test">Ride Test</a> to record and export live observations.</p>
 <p id="imuActionMessage" class="form-message" role="status"></p><details class="section-gap"><summary>Sensor connection &amp; detailed readings</summary><div class="actions section-gap"><button id="retryImu" class="secondary" data-write>Retry sensor connection</button><button id="clearBody" class="secondary" data-write>Reset saved riding positions</button><button id="snapshotImu" class="secondary">Save diagnostics</button></div><div id="imuDiagnostics" class="section-gap"></div></details></section><section id="sensingpiezo" class="sensing-view" role="tabpanel" aria-labelledby="sensingTabpiezo" tabindex="0" hidden>`+piezoMarkup()+'</section>';
 for(const [id,name] of [['retryImu','retry'],['resetCandidate','reset_candidate']])$(id).onclick=()=>send(name);
 $('clearBody').onclick=async()=>{if(await confirmAction('Reset saved riding positions?','You will need to complete all three position steps again before crash monitoring is ready.','Reset positions'))send('clear_body');};
 $('snapshotImu').onclick=()=>{if(latest?.imu?.online&&Date.now()-receivedAt<2000)saveFile('SmartVest_IMU_snapshot_'+Date.now()+'.json',JSON.stringify({format:'smartvest-imu-snapshot-v2',capturedByBrowserAt:new Date().toISOString(),status:latest,note:'Diagnostic snapshot and one decimated reading when healthy. Check online/healthy before using sample values.'},null,2),'application/json');};
 mountSensingTabs();mountPiezo(controls);riderView=mountRiderPosition({external:true,refresh:()=>sensingPoll?.refresh({afterCurrent:true})});
 sensingPoll=startPolling('sensing/imu',update,{interval:350,fixedRate:true,onAge(){
  expirePiezo();
  if(!receivedAt||Date.now()-receivedAt<=2000)return;
  updateCommunication(true);
  setText('imuLiveStatus','Live data stale');$('imuLiveStatus').className='notice warn';
  setText('fallDiagnostic','Evaluation unavailable · stale telemetry');setText('automaticDetectionStatus','Detection readiness unknown - live data stale');$('automaticDetectionStatus').className='notice warn';
  for(const id of ['accelMagnitude','gyroMagnitude','imuRate','imuTemperature'])setText(id,'Unavailable');
  setText('axisValues','Live acceleration unavailable');setText('gyroValues','Live gyroscope unavailable');controls();
 }});controls();
}

export function piezoStatus(link={}) {
 const valid=Boolean(link.online&&link.piezoConfigured);
 return {valid,state:!link.online?'Sensing board offline':valid?'Live front/rear readings':'Disabled or not ready',
  front:valid?num(link.reportedPiezoFront):'Unavailable',rear:valid?num(link.reportedPiezoRear):'Unavailable'};
}
