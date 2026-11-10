import {createDiagnosticReader,redactTrace} from '../shared/diagnostics.js';
import {api,session} from '../shared/api.js';
import {$,esc,panel,toast} from '../shared/dom.js';
import {stopPolling,received,setConnection} from '../shared/poller.js';
import {openRideStore,RIDE_MS,phTime,rideTables} from './ride-data.js';
import {zipFiles,csvFile,excelFile,download} from './ride-export.js';
const id=()=>Date.now().toString(36)+'-'+Math.random().toString(36).slice(2,10);
const delay=ms=>new Promise(r=>setTimeout(r,ms));
export function createRideRecorder({store,fetch=api,notify=()=>{},clock=()=>performance.now(),wall=()=>Date.now(),duration=RIDE_MS}){
 let meta=null,buffer=[],saving=null,active=false,starting=false,stopping=false,token=0,request=null,anchor=0,base=0;
 let imuKey='',piezoKey='',candidate=null,lastBoot=null,lastMain=null,failures=0,lastWallDrift=0,hiddenAt=0;
 let traceReader=createDiagnosticReader('gsm/trace');const endpointFailures=new Map();
 const fastInterval=250; // Browser preview only; sensing/detection still acquire at 200 Hz.
 const elapsed=()=>base+Math.max(0,clock()-anchor),live=()=>active&&elapsed()<duration;
 const state=()=>{if(meta&&active)meta.elapsed=Math.min(duration,elapsed());return {meta,active,starting,stopping,pending:buffer.length,saving:Boolean(saving)};};
 function record(kind,data,extra={}){if(!meta)return;buffer.push({ride:meta.id,n:meta.nextN++,kind,at:wall(),elapsed:Math.min(duration,elapsed()),data,...extra});}
 function moment(type,detail={}){record('moment',{type,...detail});}
 async function flush(){
  if(saving)return saving;if(!meta)return;
  const rows=buffer.splice(0),snapshot={...meta,quality:{...meta.quality},elapsed:Math.min(duration,elapsed()),savedRows:meta.savedRows+rows.length};
  saving=store.commit(snapshot,rows).then(()=>{meta.savedRows+=rows.length;meta.savedAt=wall();notify(state());}).catch(error=>{
   buffer.unshift(...rows);active=false;token++;request?.abort();meta.state='storage error';meta.error=error.message;notify(state());
  }).finally(()=>{saving=null;});return saving;
 }
 async function stop(reason='stopped'){
  if(!meta||stopping||(!active&&!starting&&meta.state!=='recording'))return;stopping=true;active=false;starting=false;token++;request?.abort();
  moment(reason==='completed'?'20-minute limit reached':'Recording stopped',{reason});
  await saving;if(meta.state!=='storage error'){
   meta.state=reason;meta.end=wall();meta.elapsed=Math.min(duration,elapsed());await flush();
  }
  stopping=false;notify(state());
 }
 async function get(path){
  request=new AbortController();const start=clock(),endpoint=path.split('?')[0];
  try{
   const data=await fetch(path,'GET',undefined,{signal:request.signal,timeoutMs:3000});
   const failed=endpointFailures.get(endpoint)||0;if(failed)moment('Readings recovered',{endpoint,failedRequests:failed});
   endpointFailures.delete(endpoint);if(endpoint==='sensing/imu')failures=0;if(typeof document!=='undefined')received();
   return {data,latency:Math.round(clock()-start)};
  }catch(error){
   if(active&&error.name!=='AbortError'){
    endpointFailures.set(endpoint,(endpointFailures.get(endpoint)||0)+1);meta.quality.requestErrors++;
    moment('Readings interrupted',{endpoint,error:error.message,httpStatus:error.status??null,transportFailure:Boolean(error.offline)});
    if(endpoint==='sensing/imu')failures++;
    if(error.offline){if(typeof document!=='undefined'&&failures>=3)setConnection(false);}
    else if(error.status&&typeof document!=='undefined')setConnection(true);
   }
   return null;
  }
 }
 const retryDelay=(endpoint,normal)=>Math.min(30000,normal*2**Math.min(endpointFailures.get(endpoint)||0,3));
 async function cycle(owner){
  let fastAt=0,slowAt=0,cameraAt=0,traceAt=0,saveAt=clock();
  while(owner===token&&active){
   const start=clock();if(!live()){await stop('completed');break;}
   if(typeof document!=='undefined'&&document.hidden){await delay(500);continue;}
   if(clock()<fastAt){await delay(Math.min(250,fastAt-clock()));continue;}
   try{
    // Motion first; at most one optional request follows. A failed history
    // endpoint cannot monopolize sampling or turn an HTTP 503 into a Wi-Fi loss.
    const r=await get('sensing/imu');
    fastAt=clock()+(r?fastInterval:Math.min(15000,500*2**Math.min(failures,5)));
    if(!live()||owner!==token)break;
    if(r){
     const i=r.data.imu||{},p=r.data.piezo||{},ik=i.boot+':'+i.sample?.sequence,pk=p.boot+':'+p.sequence;
     const imuNew=Boolean(i.sample&&ik!==imuKey),piezoNew=Boolean(p.supported&&pk!==piezoKey);
     if(imuNew||piezoNew){record('fast',{imu:{online:i.online,healthy:i.healthy,boot:i.boot,firmware:i.firmware,profile:i.profile,sample:i.sample,pose:i.pose,detector:i.detector,diagnostics:i.diagnostics,calibration:i.calibration},piezo:p},{latency:r.latency,imuNew,piezoNew});if(imuNew)meta.quality.imuRows++;if(piezoNew)meta.quality.piezoRows++;}
     if(lastBoot!==null&&i.boot!==lastBoot){moment('Sensing restarted',{previousBoot:lastBoot,boot:i.boot});candidate=null;meta.quality.sensorRestarts++;}lastBoot=i.boot;
     const count=i.detector?.candidates;if(Number.isFinite(count)&&candidate!==null&&count>candidate)moment('New IMU crash candidate',{count,decision:i.detector.lastCandidate,detector:i.detector});if(Number.isFinite(count))candidate=count;
     imuKey=ik;piezoKey=pk;
    }
    const due=[['status',slowAt],['camera',cameraAt],['trace',traceAt]].filter(([,at])=>clock()>=at).sort((a,b)=>a[1]-b[1])[0]?.[0];
    if(due==='status'){
     const r=await get('status?view=ride');slowAt=clock()+retryDelay('status',2000);if(!live()||owner!==token)break;
     if(r){const s=r.data;record('status',{firmwareVersion:s.firmwareVersion,uptimeSeconds:s.uptimeSeconds,freeHeapBytes:s.freeHeapBytes,minimumFreeHeapBytes:s.minimumFreeHeapBytes,largestHeapBlockBytes:s.largestHeapBlockBytes,network:s.network,gsm:s.gsm,gps:s.gps,power:s.power,cameras:s.cameras,sensing:s.sensing},{latency:r.latency});if(lastMain!==null&&s.uptimeSeconds<lastMain)moment('Main restarted');lastMain=s.uptimeSeconds;meta.firmware=s.firmwareVersion;}
    }else if(due==='camera'){
     const r=await get('cameras/monitor?view=summary');cameraAt=clock()+retryDelay('cameras/monitor',5000);if(!live()||owner!==token)break;
     if(r)record('camera',{cameras:r.data.cameras},{latency:r.latency});
    }else if(due==='trace'){
     const r=await get(traceReader.path());traceAt=clock()+retryDelay('gsm/trace',10000);if(!live()||owner!==token)break;
     if(r){traceReader.accept(r.data);record('trace',redactTrace(r.data),{latency:r.latency});if(r.data.page?.lost||r.data.page?.reset)moment('Modem history changed',{lost:Boolean(r.data.page.lost),reset:Boolean(r.data.page.reset),retained:r.data.page.retained});if(traceReader.pending)traceAt=clock()+1000;}
    }
    const drift=wall()-meta.start-elapsed();if(Math.abs(drift-lastWallDrift)>2000){moment('Phone clock changed',{differenceMs:drift});meta.quality.clockChanges++;lastWallDrift=drift;}
   }catch(error){
    if(owner!==token||!active)break;
    meta.quality.requestErrors++;moment('Readings interrupted',{error:error.message,transportFailure:Boolean(error.offline)});
   }
   if(clock()-saveAt>=1000){await flush();saveAt=clock();}notify(state());
   await delay(Math.max(15,fastAt-clock()));
  }
  if(owner===token&&active&&!live())await stop('completed');
 }
 async function start(notes='',saved=null){
  if(active||starting||stopping)throw Error('Stop the current ride first.');
  const previous=meta,owner=++token;starting=true;
  anchor=clock();base=saved?Math.max(saved.elapsed||0,wall()-saved.start):0;
  meta=saved?{...saved,quality:{...saved.quality},owner:id(),state:'recording'}:{id:id(),owner:id(),start:wall(),state:'recording',nextN:1,savedRows:0,elapsed:0,notes,source:typeof location==='undefined'?'test':location.origin,simulated:typeof document!=='undefined'&&Boolean(document.querySelector('.brand-model')?.textContent.includes('DEMO')),quality:{imuRows:0,piezoRows:0,requestErrors:0,sensorRestarts:0,clockChanges:0,hiddenCount:0},format:'smartvest-ride-v1'};
  if(saved&&meta.previewIntervalMs!==fastInterval)meta.mixedPreview=true;
  meta.previewIntervalMs=fastInterval;
  buffer=[];imuKey=piezoKey='';candidate=lastBoot=lastMain=null;failures=0;lastWallDrift=0;endpointFailures.clear();traceReader=createDiagnosticReader('gsm/trace');
  notify(state());try{await store.commit(meta,[],true);}catch(error){starting=false;meta=previous;notify(state());throw error;}
  if(owner!==token)return;starting=false;active=true;moment(saved?'Recording resumed':'Ride started',{mode:'live telemetry',limitMs:duration,previewIntervalMs:fastInterval});notify(state());cycle(owner);
 }
 function visibility(hidden){if(!active)return;if(hidden){hiddenAt=elapsed();meta.quality.hiddenCount++;moment('Page hidden; sampling paused');flush();}else{moment('Page visible again',{unobservedMs:hiddenAt?elapsed()-hiddenAt:0});hiddenAt=0;if(!live())stop('completed');}}
 return {start,stop,state,flush,visibility,async exportRows(){await saving;return [...new Map([...buffer,...await store.rows(meta.id)].map(r=>[r.n,r])).values()];}};
}
export async function mountRideTest(context={isCurrent:()=>true}){
 if(!context.isCurrent())return;
 stopPolling();let store,recorder,current=null,saved=[],exporting=false,disposed=false;
 const live=()=>!disposed&&context.isCurrent()&&Boolean($('rideStart'))&&session.logged;
 $('pageContent').innerHTML=panel('Ride Test',`<p>Record a normal ride for up to <strong>20 minutes</strong>. Stop early whenever you need.</p><div class="form-grid section-gap"><div class="field"><label for="rideNotes">Ride notes <span class="optional">(optional)</span></label><input id="rideNotes" maxlength="180" placeholder="Route, weather or vest setup"></div><div class="field"><label for="savedRides">Saved on this phone</label><select id="savedRides"><option>Opening saved rides…</option></select></div></div><div class="actions section-gap"><button id="rideStart" disabled>Start ride</button><button id="rideStop" class="secondary" disabled>Stop ride</button><button id="rideResume" class="secondary" disabled>Continue saved ride</button></div><p id="rideMessage" class="form-message" role="status">Preparing phone storage…</p><progress id="rideProgress" class="imu-progress" max="1200000" value="0" aria-label="Ride duration"></progress><div class="metric-strip section-gap"><div class="metric"><span class="label">Elapsed / 20 min</span><strong id="rideElapsed" class="value">00:00</strong></div><div class="metric"><span class="label">Saved records</span><strong id="rideSaved" class="value">0</strong></div><div class="metric"><span class="label">IMU readings</span><strong id="rideImu" class="value">0</strong></div><div class="metric"><span class="label">Piezo readings</span><strong id="ridePiezo" class="value">0</strong></div></div><p class="hint section-gap">Keep this page open and your phone awake. Logging uses the existing live feed, up to about 4 IMU readings/s. GPS, SIM and battery: about every 2 s. Camera details: every 5 s. Missing readings are marked, not filled in.</p><p class="hint">Recording does not change crash monitoring or emergency alerts. Start and stop while safely stationary.</p><div class="actions section-gap"><button id="rideCsv" class="secondary" disabled>Download CSV files (.zip)</button><button id="rideExcel" class="secondary" disabled>Download Excel</button></div><p class="hint">CSV files are grouped by module in one download. Excel has separate sheets, fitted columns, filters and frozen headings. Times use Philippine time (UTC+08).</p><p class="hint">Recordings stay in this browser at this dashboard address. Use the same phone and address to recover them. Save your download before clearing browser data.</p>`);
 function render(s){if(!live())return;const m=s?.meta&&(s.active||s.starting||s.stopping||s.meta.id===current?.id)?s.meta:current;current=m;const seconds=Math.floor((m?.elapsed||0)/1000);
  $('rideElapsed').textContent=String(Math.floor(seconds/60)).padStart(2,'0')+':'+String(seconds%60).padStart(2,'0');$('rideProgress').value=m?.elapsed||0;
  $('rideSaved').textContent=m?.savedRows||0;$('rideImu').textContent=m?.quality?.imuRows||0;$('ridePiezo').textContent=m?.quality?.piezoRows||0;
  $('rideStart').disabled=!store||Boolean(s?.active||s?.starting||s?.stopping||exporting);$('rideStop').disabled=!s?.active||s?.stopping;
  $('rideResume').disabled=!m||m.state!=='recording'||Boolean(s?.active||s?.starting||s?.stopping||exporting);
  for(const key of ['rideCsv','rideExcel'])$(key).disabled=!m||Boolean(s?.active||s?.stopping||exporting)||!(m.savedRows||s?.pending);
  $('savedRides').disabled=Boolean(s?.active||s?.stopping||exporting);$('rideNotes').disabled=Boolean(s?.active);
  if(s?.starting)$('rideMessage').textContent='Starting ride…';
  else if(s?.active){$('rideMessage').textContent='Recording · saving on this phone'+(s.saving?'…':'');}
  else $('rideMessage').textContent=m?.error?'Recording stopped: '+m.error:m?'Ride '+m.state+'. '+(m.state==='recording'?'Continue it or download the saved portion.':'Ready to download.'):'Ready. Start when you are safely stopped.';
  window.sjksDirty=Boolean(s?.active);
 }
 async function list(){saved=await store.list();if(!live())return;$('savedRides').innerHTML='<option value="">Choose a saved ride</option>'+saved.map(m=>'<option value="'+esc(m.id)+'">'+esc(phTime(m.start).slice(0,19).replace('T',' ')+' · '+m.state)+'</option>').join('');if(current)$('savedRides').value=current.id;}
 try{store=await openRideStore();if(!context.isCurrent()){store.close();return;}recorder=createRideRecorder({store,notify:render});await list();if(!context.isCurrent()){store.close();return;}current=saved[0]||null;if(current)$('savedRides').value=current.id;render();}catch(error){if(!context.isCurrent())return;$('rideMessage').textContent='Phone storage is unavailable. Enable browser storage before starting.';return;}
 $('rideStart').onclick=async()=>{try{await recorder.start($('rideNotes').value.trim());await list();}catch(e){toast(e.message,true);}};
 $('rideResume').onclick=async()=>{try{await recorder.start('',current);}catch(e){toast(e.message,true);}};
 $('rideStop').onclick=async()=>{await recorder.stop();await list();render(recorder.state());};
 $('savedRides').onchange=()=>{current=saved.find(m=>m.id===$('savedRides').value)||null;render();};
 async function exportRide(excel){if(exporting||!current)return;exporting=true;render(recorder.state().meta?.id===current.id?recorder.state():undefined);$('rideMessage').textContent='Preparing your recording…';
  try{const rows=recorder.state().meta?.id===current.id?await recorder.exportRows():await store.rows(current.id),tables=rideTables(current,rows),name='SmartVest_Ride_'+phTime(current.start).slice(0,19).replace(/[:T]/g,'-')+'_'+current.state.replaceAll(' ','_');
   const blob=excel?await excelFile(tables):await zipFiles([...tables.map(t=>({name:t.name.replaceAll(' ','_')+'.csv',data:csvFile(t)})),{name:'Raw_records.json',data:JSON.stringify({metadata:current,records:rows})}]);
   download(blob,name+(excel?'.xlsx':'.zip'));toast('Recording downloaded.');
  }catch(e){toast(e.message,true);}finally{exporting=false;render();}}
 $('rideCsv').onclick=()=>exportRide(false);$('rideExcel').onclick=()=>exportRide(true);
 const visibility=()=>recorder.visibility(document.hidden);
 function leave(){
  if(disposed)return;disposed=true;clearInterval(timer);document.removeEventListener('visibilitychange',visibility);window.removeEventListener('pagehide',leave);window.removeEventListener('vestsignout',leave);context.signal?.removeEventListener('abort',leave);window.sjksDirty=false;
  Promise.resolve(recorder.stop('interrupted')).finally(()=>store.close()).catch(()=>{});
 }
 const timer=setInterval(()=>{if(!live()){leave();return;}const s=recorder.state();if(s.active&&s.meta.elapsed>=RIDE_MS)recorder.stop('completed');render(s);},500);
 document.addEventListener('visibilitychange',visibility);window.addEventListener('pagehide',leave,{once:true});window.addEventListener('vestsignout',leave,{once:true});context.signal?.addEventListener('abort',leave,{once:true});
}
