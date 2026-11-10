// Execute the production recorder with a deterministic clock and failed APIs.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'../../Main_and_Cameras/ui');
const diagnostics=fs.readFileSync(path.join(root,'shared/diagnostics.js'),'utf8').replace(/^export /gm,'');
const {createDiagnosticReader,redactTrace}=new Function(diagnostics+';return {createDiagnosticReader,redactTrace};')();
const source=fs.readFileSync(path.join(root,'developer/ride-test.js'),'utf8').replace(/^import .*;\r?\n/gm,'').replace(/^export /gm,'');
async function run(mode){
 let now=0,sequence=0,timers=[],calls=[],records=[];
 const factory=new Function('createDiagnosticReader','redactTrace','setTimeout','RIDE_MS',source+';return createRideRecorder;')(createDiagnosticReader,redactTrace,(fn,ms)=>{timers.push({at:now+ms,fn});return timers.length;},1200000);
 const fetch=async route=>{
  calls.push({at:now,route});const ep=route.split('?')[0];
  if(ep==='sensing/imu'&&(mode==='503'||mode==='offline'||mode==='mixed'||mode==='recover'&&now<10000)){
   const error=new Error('Injected failure');if(mode==='offline'||mode==='mixed')error.offline=true;else error.status=503;throw error;
  }
  if(mode==='offline'){const error=new Error('Injected failure');error.offline=true;throw error;}
  if(ep==='sensing/imu')return {imu:{online:true,healthy:true,boot:1,sample:{sequence:++sequence},detector:{candidates:0}},piezo:{supported:false}};
  if(ep==='gsm/trace')return {entries:[],page:{epoch:1,total:100,oldest:53,retained:48,more:mode==='stuck-trace',nextBefore:93}};
  return {};
 };
 const rec=factory({store:{commit:async(meta,rows)=>records.push(...rows),rows:async()=>records},fetch,clock:()=>now,wall:()=>1793419200000+now,duration:60000});
 await rec.start();
 for(let n=0;n<10000;n++){
  await new Promise(resolve=>setImmediate(resolve));if(!rec.state().active)break;
  timers.sort((a,b)=>a.at-b.at);assert(timers.length);const timer=timers.shift();now=timer.at;timer.fn();
 }
 await new Promise(resolve=>setImmediate(resolve));
 assert.equal(rec.state().meta.state,'completed');assert.equal(rec.state().meta.elapsed,60000);
 assert.equal(rec.state().meta.previewIntervalMs,250);
 const imu=calls.filter(x=>x.route==='sensing/imu');
 if(['503','offline','mixed'].includes(mode)){assert(imu.length<=9,mode+' must back off');assert.equal(rec.state().meta.quality.imuRows,0);}
 if(mode==='healthy'){assert.equal(imu.length,240);assert.equal(rec.state().meta.quality.requestErrors,0);}
 if(mode==='recover'){assert(imu.some(x=>x.at>=15000));assert(rec.state().meta.quality.imuRows>100);}
 if(mode==='stuck-trace'){assert(calls.filter(x=>x.route.startsWith('gsm/trace')).length<=15);assert(records.some(x=>x.data?.error?.includes('did not advance')));}
 console.log('PASS recorder '+mode+': '+imu.length+' IMU requests/minute, bounded errors, correct completion and saved cadence');
}
(async()=>{
 for(const mode of ['healthy','503','offline','mixed','recover','stuck-trace'])await run(mode);
 const reader=createDiagnosticReader('gsm/trace');
 reader.accept({entries:[],page:{epoch:1,total:100,oldest:53,retained:48,more:true,nextBefore:93}});
 assert.throws(()=>reader.accept({entries:[],page:{epoch:1,total:100,oldest:53,retained:48,more:true,nextBefore:93}}),/did not advance/);assert(!reader.pending);
 const changing=createDiagnosticReader('gsm/trace');let failed=false;
 for(let n=1;n<=8;n++)try{changing.accept({entries:[],page:{epoch:n,total:100,oldest:53,retained:48,more:true,nextBefore:93,reset:true}});}catch{failed=true;}
 assert(failed&&!changing.pending,'continual resets must also stop the catch-up');
 console.log('PASS non-advancing cursors and repeated history resets cannot create endless catch-up');
})().catch(error=>{console.error(error);process.exitCode=1;});
