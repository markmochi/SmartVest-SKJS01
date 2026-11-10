const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login,loaded}=require('./dashboard-test-helpers.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch();
 try{
  // Failed downloads are retried under fresh graph URLs. A delayed older
  // module may finish later but must not initialize or mount a second view.
  for(const target of ['shared/runtime.js','shared/boot.js','rider/app.js']){
   const context=await browser.newContext(),p=await context.newPage();let release,held=false,count=0;
   await p.clock.install();
   await p.route('**/'+target,async route=>{if(++count===1){held=true;await new Promise(resolve=>release=resolve);}try{await route.continue();}catch{}});
   await p.goto(base,{waitUntil:'domcontentloaded'});
   if(target==='rider/app.js'){
    await p.locator('#authWrap').waitFor({state:'visible'});await p.fill('#authUser','admin');await p.fill('#authPass','admin123');await p.click('#loginSubmit');
   }
   await p.waitForFunction(()=>window.smartvestBoot);
   for(let n=0;n<150&&!held;n++)await new Promise(resolve=>setTimeout(resolve,10));assert(held);
   await p.clock.fastForward(36000);await p.clock.resume();
   if(target==='rider/app.js')await loaded(p);else await p.locator('#authWrap').waitFor({state:'visible'});
   assert(count>=2,'the held resource must be retried automatically');
   const runtime=await p.evaluate(()=>{window.heldRuntime=window.smartvestRuntime;return Boolean(window.heldRuntime);});assert(runtime);
   const sessions=h.state.requests.filter(r=>r.path==='/api/v1/session').length;
   release();await p.waitForTimeout(400);
   assert(await p.evaluate(()=>window.heldRuntime===window.smartvestRuntime));
   assert.equal(h.state.requests.filter(r=>r.path==='/api/v1/session').length,sessions,'a late entry cannot initialize twice');
   assert.equal(await p.locator('[data-rp-canvas]').count(),target==='rider/app.js'?1:0);
   assert.equal(await p.locator('#reloadDashboard').count(),0);
   assert.equal(await p.evaluate(()=>getComputedStyle(document.body).margin),'0px');
   console.log('PASS automatic recovery and ignored late completion: '+target);
   await context.close();
  }
  for(const [role,route,endpoint] of [['rider','/','/rider/status'],['admin','/admin/cameras','/cameras/monitor']]){
   const context=await browser.newContext(),p=await context.newPage();
   await p.addInitScript(endpoint=>{
    window.qaHidden=false;Object.defineProperty(document,'hidden',{configurable:true,get:()=>window.qaHidden});
    const original=window.fetch;window.qaRead={hold:false,started:0,aborted:0};
    window.fetch=async(...args)=>{
     const response=await original(...args);
     if(!window.qaRead.hold||!String(args[0]).includes(endpoint))return response;
     window.qaRead.started++;
     return {ok:response.ok,status:response.status,json:()=>new Promise((resolve,reject)=>{
      const abort=()=>{window.qaRead.aborted++;reject(new DOMException('Read cancelled','AbortError'));};
      if(args[1].signal.aborted)abort();else args[1].signal.addEventListener('abort',abort,{once:true});
     })};
    };
   },endpoint);
   await p.goto(base+route);await login(p,role);
   await p.evaluate(()=>window.qaRead.hold=true);await p.click('#refreshPage');
   await p.waitForFunction(()=>window.qaRead.started>0);
   await p.evaluate(()=>{window.qaHidden=true;document.dispatchEvent(new Event('visibilitychange'));});
   await p.waitForFunction(()=>window.qaRead.aborted>0);
   await p.evaluate(()=>{window.qaRead.hold=false;window.qaHidden=false;document.dispatchEvent(new Event('visibilitychange'));});
   await p.waitForFunction(()=>!document.getElementById('refreshPage').disabled);
   assert.equal(await p.locator('#connection').innerText(),'Connected to Vest');
   assert(await p.locator('#connectionBanner').isHidden());
   assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0);
   console.log('PASS hiding '+role+' '+route+' aborts its stalled read and resumes the shared request queue without a false disconnection or repeated write');
   await context.close();
  }
  {
   h.state.status.benchTestsEnabled=true;
   const context=await browser.newContext(),p=await context.newPage();
   await p.addInitScript(()=>{
    window.qaHidden=false;Object.defineProperty(document,'hidden',{configurable:true,get:()=>window.qaHidden});
    window.qaWrite={started:false,aborted:0,done:false,result:null};const original=window.fetch;
    window.fetch=async(...args)=>{
     const response=await original(...args);
     if(args[1]?.method!=='POST'||!String(args[0]).endsWith('/power/led-test'))return response;
     const data=await response.json();
     return {ok:response.ok,status:response.status,json:()=>new Promise((resolve,reject)=>{
      window.qaWrite.started=true;window.qaWrite.release=()=>resolve(data);
      args[1].signal.addEventListener('abort',()=>{window.qaWrite.aborted++;reject(new DOMException('Write cancelled','AbortError'));},{once:true});
     })};
    };
   });
   await p.goto(base+'/admin/tools');await login(p,'admin');
   await p.evaluate(async()=>{
    const root=document.querySelector('script[type="module"]').src.replace(/shared\/boot.js$/,'');const {api}=await import(root+'shared/api.js');
    api('power/led-test','POST',{mode:'off'}).then(data=>window.qaWrite.result={ok:true,data},e=>window.qaWrite.result={ok:false,error:e.message}).finally(()=>window.qaWrite.done=true);
   });
   await p.waitForFunction(()=>window.qaWrite.started);
   await p.evaluate(()=>{window.qaHidden=true;document.dispatchEvent(new Event('visibilitychange'));});
   await p.waitForTimeout(100);assert.equal(await p.evaluate(()=>window.qaWrite.aborted),0);
   await p.evaluate(()=>{window.qaHidden=false;document.dispatchEvent(new Event('visibilitychange'));window.qaWrite.release();});
   await p.waitForFunction(()=>window.qaWrite.done);
   assert.equal(await p.evaluate(()=>window.qaWrite.result.ok),true);
   assert.equal(h.state.requests.filter(r=>r.method==='POST'&&r.path.endsWith('/power/led-test')).length,1,'a pending write is never replayed');
   console.log('PASS visibility cleanup preserves one pending simulated LED write until its response completes, without aborting or replaying it');
   await context.close();
  }
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
