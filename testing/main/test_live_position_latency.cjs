// Browser timing fixture; no physical sensor, network or calibration writes.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login}=require('./dashboard-test-helpers.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
(async()=>{const h=createHarness();prepare(h);const base=await h.start(),browser=await launch();
let p;try{
 p=await browser.newPage({viewport:{width:1440,height:1000}}),errors=[];
 p.on('pageerror',e=>errors.push(e.message));await p.goto(base+'/admin/sensing');await login(p,'admin');
 await p.waitForFunction(()=>document.querySelector('[data-rp-live]')?.textContent==='Live position');
 let freeze=false,cached=null,active=0,peak=0,calls=0;
 await p.route('**/api/v1/sensing/imu',async route=>{
  ++calls;peak=Math.max(peak,++active);
  try{await new Promise(resolve=>setTimeout(resolve,350));const response=await route.fetch();const data=await response.json();
   if(freeze){cached=cached||data;await route.fulfill({json:cached});}else{cached=null;await route.fulfill({json:data});}
  }finally{--active;}
 });
 const writes=h.state.requests.filter(r=>r.method==='POST').length;
 await p.evaluate(()=>{
  const root=document.querySelector('[data-rp-studio]');window.poseCanvas=document.querySelector('[data-rp-canvas]');window.posePauses=0;
  window.diagnosticsNode=document.querySelector('#imuDiagnostics dl');window.plotResizes=0;
  window.plotObserver=new MutationObserver(()=>++window.plotResizes);
  window.plotObserver.observe(document.querySelector('#accelPlot'),{attributes:true,attributeFilter:['width','height']});
  window.poseObserver=new MutationObserver(()=>{if(root.dataset.rpState==='waiting')++window.posePauses;});
  window.poseObserver.observe(root,{attributes:true,attributeFilter:['data-rp-state']});
 });
 await p.waitForTimeout(6000);
 assert.equal(await p.evaluate(()=>window.posePauses),0,'ordinary GET latency must not repeatedly blank the view');
 assert(await p.evaluate(()=>window.poseCanvas===document.querySelector('[data-rp-canvas]')));
 assert(await p.evaluate(()=>window.diagnosticsNode===document.querySelector('#imuDiagnostics dl')),'diagnostics must patch existing nodes');
 assert.equal(await p.evaluate(()=>window.plotResizes),0,'unchanged chart dimensions must not be repeatedly reset');
 assert.equal(await p.locator('[data-rp-live]').innerText(),'Live position');
 assert(peak===1&&calls>=8&&calls<=20,'polling is bounded and never overlaps');
 await p.screenshot({path:testOutput('sensing-delayed-live.png'),fullPage:true});
 freeze=true;await p.waitForFunction(()=>document.querySelector('[data-rp-live]').textContent==='View paused');
 assert.equal(await p.locator('[data-rp-side]').innerText(),'—');
 assert(await p.locator('[data-rp-gyro]').isDisabled());
 freeze=false;await p.waitForFunction(()=>document.querySelector('[data-rp-live]').textContent==='Live position');
 assert.equal(h.state.requests.filter(r=>r.method==='POST').length,writes);
 await p.evaluate(()=>{window.poseObserver.disconnect();window.plotObserver.disconnect();});assert.deepEqual(errors,[]);
 const message=await p.evaluate(async()=>{const prefix=document.querySelector('script[type="module"]').src.replace(/boot\.js$/,'');const {friendlyError}=await import(prefix+'errors.js');return friendlyError(503,'login','Response memory unavailable');});
 assert.match(message,/Sign-in is temporarily unavailable/);assert(!/saving changes/.test(message));
 console.log('PASS delayed sensing view: stable canvas/live status through 350 ms GET latency, bounded nonoverlapping cadence, genuine duplicate-stream pause, disabled stale calibration, recovery, no writes, accurate sign-in error');
}finally{await p?.unrouteAll({behavior:"wait"});await browser.close();await h.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
