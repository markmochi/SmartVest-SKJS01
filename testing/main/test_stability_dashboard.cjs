const assert=require('node:assert/strict'),path=require('node:path');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT||'playwright');
(async()=>{
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox']});
 const h=createHarness(),base=await h.start(),page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 Object.assign(h.state.status.power,{enabled:false,ledTestSupported:true,indicatorEnabled:true,indicatorState:'unknown'});h.state.status.benchTestsEnabled=true;
 try{
  await page.goto(base+'/admin/power');await page.fill('#authUser','developer');await page.fill('#authPass','developer-test-password');await page.click('#loginSubmit');
  await page.locator('.diagnostic-summary').waitFor();
  assert.equal(await page.locator('[data-led-test]').count(),0);
  assert.equal(h.state.requests.filter(r=>r.path.endsWith('/power/led-test')).length,0);
  console.log('PASS power page has no manual LED test controls or automatic LED writes');
  const outcome=await page.evaluate(async()=>{
   const root=document.querySelector('script[type="module"]').src.replace(/shared\/boot.js$/,'');
   const api=await import(root+'shared/api.js'),poller=await import(root+'shared/poller.js');poller.stopPolling();
   const old=window.fetch;let active=0,max=0;
   window.fetch=async(...args)=>{active++;max=Math.max(max,active);try{await new Promise(r=>setTimeout(r,40));return await old(...args);}finally{active--;}};
   try{await Promise.all([api.api('status'),api.api('status'),api.api('status')]);return max;}finally{window.fetch=old;}
  });
  assert.equal(outcome,1);console.log('PASS browser API queue: three concurrent callers produce only one in-flight request');
  const delayed=await page.evaluate(async()=>{
   const root=document.querySelector('script[type="module"]').src.replace(/shared\/boot.js$/,'');
   const api=await import(root+'shared/api.js'),poller=await import(root+'shared/poller.js');poller.stopPolling();
   const probe=document.createElement('button');probe.dataset.write='';document.body.append(probe);
   const polling=poller.startPolling('status',()=>{}, {interval:2000});await polling.refresh();
   const old=window.fetch;let failed=false;
   window.fetch=async(...args)=>{if(!failed&&String(args[0]).includes('/api/v1/status')){failed=true;throw new TypeError('simulated transient read failure');}return old(...args);};
   try{
    await polling.refresh();
    return {connected:api.session.connected,disabled:probe.disabled,label:document.querySelector('#connection').textContent,bannerHidden:document.querySelector('#connectionBanner').hidden,updating:!document.querySelector('#updateIndicator').hidden,stale:document.querySelector('#workspace').classList.contains('stale')};
   }finally{poller.stopPolling();window.fetch=old;probe.remove();}
  });
  assert.equal(delayed.connected,true);assert.equal(delayed.disabled,false);assert.equal(delayed.stale,false);assert.equal(delayed.label,'Connected to Vest');assert.equal(delayed.bannerHidden,true);assert.equal(delayed.updating,true);
  console.log('PASS transient dashboard read failure: warning only; unrelated controls remain available');
  await page.goto(base+'/admin/sensing#piezo');await page.locator('#piezoState').filter({hasText:'Listening for impacts'}).waitFor();
  Object.assign(h.state.imuLab.piezo,{online:true,fresh:true});h.state.imuLab.piezo.front.raw=345;h.state.imuLab.piezo.rear.raw=0;await page.locator('#piezoPanel details').evaluate(el=>el.open=true);
  await page.locator('#piezoFront').filter({hasText:'345'}).waitFor();assert.equal(await page.locator('#piezoRear').innerText(),'0');
  h.state.imuLab.imu.healthy=false;await page.locator('#piezoFront').filter({hasText:'345'}).waitFor();
  Object.assign(h.state.imuLab.piezo,{online:false,fresh:false});await page.locator('#piezoState').filter({hasText:'Impact readings unavailable'}).waitFor();assert.equal(await page.locator('#piezoFront').innerText(),'Unavailable');
  console.log('PASS browser piezo panel: valid group values, independent IMU readiness, stale/offline unavailable');
  assert.deepEqual(errors,[]);console.log('PASS no JavaScript errors. APIs are simulated, not hardware measurements.');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1);});
