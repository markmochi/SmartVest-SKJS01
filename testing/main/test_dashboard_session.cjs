// Browser lifecycle checks with simulated APIs and an accelerated server clock.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs'),{prepare}=require('./dashboard-test-helpers.cjs');
const {chromium}=require(process.env.SJKS_PLAYWRIGHT||'playwright');
(async()=>{
 const h=createHarness();prepare(h);
 const base=await h.start(),browser=await chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 const page=await browser.newPage(),errors=[];page.setDefaultTimeout(10000);page.on('pageerror',error=>errors.push(error.message));
 await page.addInitScript(()=>{
  window.qaHidden=false;Object.defineProperty(document,'hidden',{configurable:true,get:()=>window.qaHidden});
  window.qaApi={active:0,max:0,calls:0};const original=window.fetch;
  window.fetch=async(...args)=>{
   if(!String(args[0]).startsWith('/api/'))return original(...args);
   const tracker=window.qaApi;++tracker.calls;++tracker.active;tracker.max=Math.max(tracker.max,tracker.active);
   try{const response=await original(...args);return {ok:response.ok,status:response.status,json:async()=>{try{return await response.json();}finally{--tracker.active;}}};}
   catch(error){--tracker.active;throw error;}
  };
 });
 const apiCall=method=>page.evaluate(async method=>{
  const root=document.querySelector('script[type="module"]').src.replace(/shared\/boot.js$/,'');
  const {api}=await import(root+'shared/api.js');
  try{return {ok:true,data:await api(method)};}catch(error){return {ok:false,status:error.status,message:error.message};}
 },method);
 async function login(){await page.fill('#authUser','admin');await page.fill('#authPass','admin123');await page.click('#loginSubmit');await page.locator('#workspace').waitFor({state:'visible'});await page.locator('#authWrap').waitFor({state:'hidden'});await page.waitForFunction(()=>document.querySelector('[data-rp-live]')?.textContent==='Live position');}
 async function visibility(hidden){await page.evaluate(hidden=>{window.qaHidden=hidden;document.dispatchEvent(new Event('visibilitychange'));window.dispatchEvent(new Event(hidden?'offline':'online'));},hidden);}
 try{
  await page.goto(base);await login();
  await page.evaluate(()=>window.qaCanvas=document.querySelector('[data-rp-canvas]'));
  for(let n=0;n<10;++n){await visibility(true);await page.waitForTimeout(25);await visibility(false);await page.waitForTimeout(25);}
  await page.waitForFunction(()=>document.querySelector('[data-rp-live]')?.textContent==='Live position');
  assert(await page.evaluate(()=>window.qaCanvas===document.querySelector('[data-rp-canvas]')));
  await visibility(true);await page.waitForFunction(()=>window.qaApi.active===0);const paused=h.state.requests.length;await page.waitForTimeout(350);assert.equal(h.state.requests.length,paused,'hidden views stop requesting new observations');
  await visibility(false);await page.waitForFunction(()=>document.querySelector('[data-rp-live]')?.textContent==='Live position');
  const before=h.state.requests.length;await page.waitForTimeout(2200);assert(h.state.requests.length-before<=35,'visibility events must not create duplicate polling loops');
  h.state.offline=true;await page.evaluate(()=>window.dispatchEvent(new Event('online')));await page.waitForTimeout(250);
  h.state.offline=false;await page.evaluate(()=>window.dispatchEvent(new Event('online')));await page.waitForFunction(()=>document.querySelector('[data-rp-live]')?.textContent==='Live position');
  console.log('PASS repeated visibility/reconnect cycles preserve one API request at a time, stop hidden polling and resume live readings without duplicating loops');
  for(let cycle=0;cycle<3;++cycle){
   h.state.sessionOffsetMs+=29*60*1000;
   assert.equal((await apiCall('session')).ok,true,'active requests do not silently extend the fixed login lifetime');
   h.state.sessionOffsetMs+=2*60*1000;
   const expired=await apiCall('session');assert.equal(expired.ok,false);assert(expired.status===401||/session changed/i.test(expired.message),'an expiry response or cancelled stale-session request is expected');assert.equal(h.state.sessions.size,0);
   await page.locator('#authWrap').waitFor({state:'visible'});
   assert(await page.locator('#workspace').isHidden());assert.match(await page.locator('#loginMessage').innerText(),/session has ended/i);
   await page.waitForTimeout(200);const stopped=h.state.requests.length;await page.waitForTimeout(350);assert.equal(h.state.requests.length,stopped,'expired sessions dispose both status and pose polling');
   await login();assert.equal((await apiCall('session')).ok,true);
  }
  assert.equal(await page.locator('#riderPosition').count(),1);assert.equal(await page.evaluate(()=>window.qaApi.max),1);
  assert.equal(h.state.requests.filter(request=>request.method==='POST'&&!request.path.endsWith('/login')).length,0);
  assert.deepEqual(errors,[]);
  console.log('PASS three accelerated 30-minute session expiries: sign-in returns, old polling stops, relogin restores one live view, no automatic writes or JavaScript errors');
 }finally{await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
