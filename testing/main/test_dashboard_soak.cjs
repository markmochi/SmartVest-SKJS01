const assert=require('node:assert/strict'),fs=require('node:fs');
const {createHarness}=require('./preview_server.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
const {prepare,launch,login,theme}=require('./dashboard-test-helpers.cjs');
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch(),clients=[],samples=[],errors=[];
 const init=()=>{
  window.qaHidden=false;window.qaTimeOffset=0;const now=Date.now.bind(Date);Date.now=()=>now()+window.qaTimeOffset;
  Object.defineProperty(document,'hidden',{configurable:true,get:()=>window.qaHidden});
  window.qaRequests={active:0,max:0,calls:0};const original=window.fetch;
  window.fetch=async(...args)=>{
   if(!String(args[0]).startsWith('/api/'))return original(...args);
   const count=window.qaRequests;++count.active;++count.calls;count.max=Math.max(count.max,count.active);
   try{const response=await original(...args);return {ok:response.ok,status:response.status,json:async()=>{try{return await response.json();}finally{--count.active;}}};}
   catch(error){--count.active;throw error;}
  };
  const add=EventTarget.prototype.addEventListener,remove=EventTarget.prototype.removeEventListener,owners=new Map();
  window.qaListeners=()=>[...owners.values()].reduce((n,types)=>n+[...types.values()].reduce((a,callbacks)=>a+callbacks.size,0),0);
  EventTarget.prototype.addEventListener=function(type,fn,options){
   if(this===window||this===document){let types=owners.get(this);if(!types)owners.set(this,types=new Map());let callbacks=types.get(type);if(!callbacks)types.set(type,callbacks=new Set());callbacks.add(fn);}
   return add.call(this,type,fn,options);
  };
  EventTarget.prototype.removeEventListener=function(type,fn,options){if(this===window||this===document)owners.get(this)?.get(type)?.delete(fn);return remove.call(this,type,fn,options);};
 };
 async function snapshot(p){return p.evaluate(()=>({nodes:document.querySelectorAll('*').length,canvases:document.querySelectorAll('[data-rp-canvas]').length,listeners:window.qaListeners(),...window.qaRequests}));}
 async function visible(p,value){await p.evaluate(hidden=>{window.qaHidden=hidden;document.dispatchEvent(new Event('visibilitychange'));},!value);}
 async function api(p,path){return p.evaluate(async path=>{
  const root=document.querySelector('script[type="module"]').src.replace(/shared\/boot.js$/,'');
  const {api}=await import(root+'shared/api.js');
  try{return {ok:true,data:await api(path)};}catch(e){return {ok:false,status:e.status,message:e.message};}
 },path);}
 try{
  for(const role of ['rider','admin']){
   const context=await browser.newContext(),p=await context.newPage();await p.addInitScript(init);p.on('pageerror',e=>errors.push(e.message));
   await p.goto(base+(role==='admin'?'/admin':'/'));await login(p,role);await p.waitForTimeout(200);
   clients.push({role,context,p,baseline:await snapshot(p)});
  }
  for(let cycle=1;cycle<=16;cycle++){
   h.state.sessionOffsetMs+=30*60*1000;
   for(const client of clients){
    const {p,role,baseline}=client;
    await p.evaluate(()=>window.qaTimeOffset+=30*60*1000);
    const expired=await api(p,'session');assert(!expired.ok&&(expired.status===401||/session changed/i.test(expired.message)));
    await p.locator('#authWrap').waitFor({state:'visible'});
    await p.waitForTimeout(30);await p.waitForFunction(()=>window.qaRequests.active===0);
    const paused=await p.evaluate(()=>window.qaRequests.calls);await p.waitForTimeout(50);assert.equal(await p.evaluate(()=>window.qaRequests.calls),paused,'expired view must stop reads');
    await login(p,role);await theme(p,cycle%2?'dim':'cream');
    if(role==='rider')await p.waitForFunction(()=>document.querySelector('[data-rp-live]')?.textContent==='Live position');
    await p.evaluate(()=>window.qaCanvas=document.querySelector('[data-rp-canvas]'));
    for(let n=0;n<3;n++){await visible(p,false);await visible(p,true);}
    await p.waitForFunction(()=>!document.getElementById('refreshPage').disabled);await p.click('#refreshPage');await p.waitForFunction(()=>!document.getElementById('refreshPage').disabled);
    assert(await p.evaluate(()=>window.qaCanvas===document.querySelector('[data-rp-canvas]')));
    const current=await snapshot(p);assert.equal(current.max,1,'one API request per tab');assert.equal(current.canvases,role==='rider'?1:0);
    assert(current.nodes<=baseline.nodes+25,`${role} nodes grew from ${baseline.nodes} to ${current.nodes}`);
    assert(current.listeners<=baseline.listeners,`${role} listeners grew from ${baseline.listeners} to ${current.listeners}`);
    samples.push({cycle,virtualMinutes:cycle*30,role,...current});
   }
  }
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0,'lifecycle and theme changes never write settings or send commands');
  assert.deepEqual(errors,[]);
  fs.writeFileSync(testOutput('lifecycle.json'),JSON.stringify({virtualHours:8,sessionExpiriesPerRole:16,visibilityCyclesPerRole:48,samples,errors,scope:'Accelerated browser and server wall-clock checkpoints. Network operations and timers run normally between checkpoints; this does not simulate every poll during eight physical hours or validate Wi-Fi hardware.'},null,2));
  console.log('PASS eight virtual hours, 16 expiries/relogins and 48 visibility cycles per role; single-request ownership, bounded DOM/listeners/canvas and no unintended writes');
 }finally{for(const c of clients)await c.context.close();await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
