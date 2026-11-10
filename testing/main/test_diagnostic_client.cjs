const assert=require('node:assert/strict'),path=require('node:path'),{pathToFileURL}=require('node:url');
(async()=>{
 const root=path.resolve(__dirname,'../../Main_and_Cameras/ui'),url=file=>pathToFileURL(path.join(root,file)).href;
 const {diagnosticPath,createDiagnosticReader,collectDiagnostics,redactTrace}=await import(url('shared/diagnostics.js'));
 let epoch=77,total=60,revision=9,changeDuring=false,requests=[];
 async function fetchPage(target){
  requests.push(target);const q=new URL('http://vest/'+target).searchParams,section=q.get('section'),limit=section?16:48,t=section?16:total;
  const before=Number(q.get('before'))||t+1,after=Number(q.get('after'))||0;
  const old=t-limit+1,serials=Array.from({length:limit},(_,i)=>t-i).filter(i=>i<before&&i>after),chosen=serials.slice(0,8);
  const reset=q.has('epoch')&&Number(q.get('epoch'))!==epoch;
  const page={epoch,total:t,retained:limit,oldest:old,more:serials.length>8,nextBefore:serials.length>8?chosen.at(-1):0,reset,lost:after>0&&after<old-1};
  const rows=chosen.map(serial=>({serial,text:'AT+CMGS="+639170000001"',direction:'TX',atMs:serial,role:'front',type:'clip_saved'}));
  const camera=target.startsWith('cameras/');
  const data={page,cameras:[],logs:camera?rows:[],entries:!camera&&!section?rows:[],lastFailure:camera?null:{revision,entryCount:16,entries:section?rows:[],command:'AT+CMGS="+639170000001"'}};
  if(changeDuring){epoch++;changeDuring=false;}return data;
 }
 let data=await collectDiagnostics(fetchPage,'cameras/monitor');assert.equal(data.logs.length,48);assert(data.history.complete);assert.equal(new Set(data.logs.map(e=>e.serial)).size,48);assert.equal(requests.length,6);
 requests=[];data=await collectDiagnostics(fetchPage,'gsm/trace');assert.equal(data.entries.length,48);assert.equal(data.lastFailure.entries.length,16);assert(data.history.complete&&data.failureHistory.complete);assert.equal(requests.length,8);
 assert(!JSON.stringify(redactTrace(data)).includes('+639170000001'));assert(JSON.stringify(redactTrace(data)).includes('[recipient hidden]'));
 const reader=createDiagnosticReader('gsm/trace');let merged;
 do{merged=reader.accept(await fetchPage(reader.path()));}while(reader.pending);
 assert.equal(merged.entries.length,48);total+=2;merged=reader.accept(await fetchPage(reader.path()));assert.equal(merged.entries.length,48);assert.equal(merged.entries[0].serial,62);assert(!reader.pending);
 total+=100;merged=reader.accept(await fetchPage(reader.path()));assert(merged.history.lost);
 const reboot={...await fetchPage('gsm/trace'),page:{...merged.page,epoch:88,total:2,retained:2,oldest:1,more:false,reset:true},entries:[{serial:2},{serial:1}]};
 merged=reader.accept(reboot);assert.deepEqual(merged.entries.map(e=>e.serial),[2,1]);
 changeDuring=true;await assert.rejects(()=>collectDiagnostics(fetchPage,'cameras/monitor'),/changed during download/);
 const old={logs:[{type:'clip_saved'}]};assert.deepEqual(await collectDiagnostics(async()=>old,'cameras/monitor'),old);
 assert(diagnosticPath('gsm/trace',{before:9,epoch:8}).includes('before=9'));
 console.log('PASS bounded clients: complete 48+16 histories, overlap deduplication, insertion/eviction, reboot isolation, interrupted download, legacy data and recipient masking');

 const {createRideRecorder}=await import(url('developer/ride-test.js'));
 let time=0,seq=0;const calls=[],saved=[];
 const store={async commit(_meta,rows){saved.push(...structuredClone(rows));},async rows(){return saved;}};
 const recorder=createRideRecorder({store,duration:45000,clock:()=>time,wall:()=>1700000000000+time,fetch:async target=>{
  calls.push({target,time});time+=1000;
  if(target.startsWith('cameras/monitor')){const e=Error('Response memory unavailable');e.status=503;throw e;}
  if(target.startsWith('sensing/imu'))return {imu:{boot:1,online:true,healthy:true,sample:{sequence:++seq},detector:{candidates:0,piezoImpactDetectionEnabled:null},diagnostics:{}},piezo:{boot:1,supported:true,sequence:seq}};
  if(target.startsWith('gsm/trace'))return {entries:[{text:'AT+CMGS="+639170000001"'}],lastFailure:{command:'AT+CMGS="+639170000001"',entries:[{text:'Recipient +639170000001'}]}};
  return {firmwareVersion:'1.0',uptimeSeconds:time/1000,cameras:[]};
 }});
 // Advance the injected clock when a recorder delay completes as well as on
 // responses. A frozen clock cannot reach a deadline introduced by backoff.
 const realTimeout=global.setTimeout;
 global.setTimeout=(fn,ms,...args)=>realTimeout(()=>{time+=ms;fn(...args);},0);
 await recorder.start('normal ride');
 for(let i=0;i<200&&(recorder.state().active||recorder.state().stopping);i++)await new Promise(r=>realTimeout(r,5));
 global.setTimeout=realTimeout;
 assert.equal(recorder.state().meta.state,'completed');const rows=await recorder.exportRows();
 assert(calls[0].target==='sensing/imu');
 for(let i=1;i<calls.length;i++)if(!calls[i-1].target.startsWith('sensing/imu'))assert.equal(calls[i].target,'sensing/imu');
 const cameraCalls=calls.filter(c=>c.target.startsWith('cameras/monitor'));assert(cameraCalls.length>=2);for(let i=1;i<cameraCalls.length;i++)assert(cameraCalls[i].time-cameraCalls[i-1].time>=10000);
 assert(rows.some(r=>r.kind==='fast')&&rows.some(r=>r.kind==='status')&&rows.some(r=>r.kind==='trace'));
 const errors=rows.filter(r=>r.kind==='moment'&&r.data.type==='Readings interrupted');assert(errors.length>0);assert(errors.every(r=>r.data.httpStatus===503&&r.data.transportFailure===false));
 assert(!JSON.stringify(rows.filter(r=>r.kind==='trace')).includes('+639170000001'));
 assert(rows.filter(r=>r.kind==='fast').every(r=>r.data.imu.detector.piezoImpactDetectionEnabled===null));
 assert(rows.every(r=>r.elapsed<=45000));
 console.log('PASS actual ride recorder: motion priority, one optional request/cycle, 503 endpoint backoff, no invented transport failure, partial data retained, nullable policy, masked nested traces and cap');
})().catch(e=>{console.error(e);process.exitCode=1;});
