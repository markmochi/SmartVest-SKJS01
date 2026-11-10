// Actual API client source with synthetic responses; no hardware is contacted.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const url=text=>'data:text/javascript;base64,'+Buffer.from(text).toString('base64');
(async()=>{
 const folder=path.resolve(__dirname,'../../Main_and_Cameras/ui/shared');
 const errors=url(fs.readFileSync(path.join(folder,'errors.js'),'utf8'));
 const source=fs.readFileSync(path.join(folder,'api.js'),'utf8').replace("'./errors.js'",JSON.stringify(errors));
 const {api,setSession}=await import(url(source));
 const {friendlyError}=await import(errors);
 assert.match(friendlyError(409,'messages/cancel','This text has already been handed to the mobile network'),/cannot be cancelled/);
 assert.match(friendlyError(503,'messages/cancel','internal device error'),/cancellation could not be saved/);
 global.window=new EventTarget();setSession({csrf:'synthetic-token',role:'developer'});
 let active=0,max=0,calls=0,finished=false;
 global.fetch=async(_path,options)=>{
  ++calls;++active;max=Math.max(max,active);
  if(calls===1)return {ok:true,status:200,json:()=>new Promise((_resolve,reject)=>{
   options.signal.addEventListener('abort',()=>{--active;finished=true;reject(new DOMException('Body timed out','AbortError'));},{once:true});
  })};
  assert(finished,'the next request cannot bypass the pending body');
  return {ok:true,status:200,json:async()=>{--active;return {recovered:true};}};
 };
 const timed=api('status','GET',undefined,{timeoutMs:500});
 const next=api('status');
 const outcome=await Promise.allSettled([timed,next]);
 assert.equal(outcome[0].status,'rejected');assert.equal(outcome[0].reason.offline,true);
 assert.match(outcome[0].reason.message,/did not respond/);
 assert.deepEqual(outcome[1],{status:'fulfilled',value:{recovered:true}});
 assert.equal(calls,2);assert.equal(max,1);assert.equal(active,0);
 console.log('PASS stalled response body times out as a transport failure and releases the serialized API queue');
 global.fetch=async()=>({ok:true,status:200,json:async()=>{throw new TypeError('Body stream interrupted');}});
 await assert.rejects(api('status'),error=>error.offline===true&&/did not respond/.test(error.message));
 global.fetch=async()=>({ok:true,status:200,json:async()=>{throw new SyntaxError('Invalid JSON');}});
 await assert.rejects(api('status'),error=>!error.offline&&/could not provide an update/.test(error.message));
 global.fetch=async()=>({ok:true,status:200,json:async()=>({ok:true})});
 assert.deepEqual(await api('status'),{ok:true});
 console.log('PASS interrupted response bodies retain transport classification; invalid JSON remains a response error; later requests recover');
 // A stalled preview must not hold page data or a deliberate action hostage.
 let releaseWrite,enteredPose=false,backgroundAborted=false,order=[],concurrent=0,peak=0;
 global.fetch=async(target,options)=>{
  order.push(target);concurrent++;peak=Math.max(peak,concurrent);
  if(target.endsWith('rider/pose')){enteredPose=true;return {ok:true,status:200,json:()=>new Promise((_resolve,reject)=>options.signal.addEventListener('abort',()=>{backgroundAborted=true;concurrent--;reject(new DOMException('preview yielded','AbortError'));},{once:true}))};}
  if(options.method==='POST')return {ok:true,status:200,json:()=>new Promise(resolve=>{releaseWrite=()=>{concurrent--;resolve({saved:true});};})};
  return {ok:true,status:200,json:async()=>{concurrent--;return {ready:true};}};
 };
 const preview=api('rider/pose','GET',undefined,{priority:'background'}).then(()=>{throw new Error('preview should yield');},error=>assert.equal(error.name,'AbortError'));
 while(!enteredPose)await new Promise(resolve=>setTimeout(resolve,0));
 const foreground=api('status?view=overview');
 await Promise.all([preview,foreground]);assert(backgroundAborted);assert.equal(peak,1);
 const write=api('settings','POST',{revision:7});
 while(!releaseWrite)await new Promise(resolve=>setTimeout(resolve,0));
 const controller=new AbortController();
 const cancelled=api('unused','GET',undefined,{signal:controller.signal}).then(()=>{throw new Error('queued read should cancel');},error=>assert.equal(error.name,'AbortError'));
 controller.abort();await cancelled;assert(!order.some(target=>target.endsWith('/unused')));
 const later=api('status');assert.equal(concurrent,1);releaseWrite();
 assert.deepEqual(await write,{saved:true});await later;assert.equal(peak,1);
 assert.equal(order.filter(target=>target.endsWith('/settings')).length,1);
 enteredPose=false;backgroundAborted=false;releaseWrite=null;
 const pageRead=api('rider/pose').then(()=>{throw new Error('read should yield to a write');},error=>assert.equal(error.name,'AbortError'));
 while(!enteredPose)await new Promise(resolve=>setTimeout(resolve,0));
 const urgent=api('settings','POST',{revision:8});
 while(!releaseWrite)await new Promise(resolve=>setTimeout(resolve,0));
 assert(backgroundAborted);releaseWrite();await Promise.all([pageRead,urgent]);
 assert.equal(order.filter(target=>target.endsWith('/settings')).length,2);assert.equal(peak,1);
 console.log('PASS foreground data preempts only a background GET; queued cancellation is immediate; active writes finish exactly once; one request remains in flight');
})().catch(error=>{console.error(error);process.exitCode=1;});
