const assert=require('node:assert/strict');
const path=require('node:path');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const harness=createHarness(),base=await harness.start();
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true});
 const context=await browser.newContext({viewport:{width:1440,height:1000}}),page=await context.newPage(),errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 const forbidden=/\b(?:bench|test|tests|provisional)\b/i;
 const ready=async()=>{await page.locator('#workspace').waitFor({state:'visible'});await page.waitForFunction(()=>!document.querySelector('#pageContent .loading'));};
 try{
  harness.state.status.automaticCrashDetectionEnabled=true;
  harness.state.incidents.push({boot:999,sequence:9,test:true,message:'SmartVest test\nArchived diagnostic',recipients:[],front:'received',rear:'pending'});
  await page.goto(base+'/admin');await page.fill('#authUser','developer');await page.fill('#authPass','developer-test-password');await page.click('#loginSubmit');await ready();
  await page.goto(base+'/admin/sensing');await page.locator('#imuLiveStatus').filter({hasText:'IMU online'}).waitFor();
  Object.assign(harness.state.imuLab.imu.detector,{automaticDetectionEnabled:true,mainAutomaticDetectionEnabled:true,automaticReady:true});
  for(const url of ['/admin','/admin/gsm','/admin/gps','/admin/sensing','/admin/power','/admin/cameras','/admin/network','/admin/system','/admin/contacts','/admin/activity','/admin/incidents','/admin/tools','/admin/account']){
   await page.goto(base+url);await ready();
   const text=await page.locator('body').innerText();const publicText=await page.locator('#main').evaluate(el=>{const copy=el.cloneNode(true);copy.querySelector('#led-test')?.remove();copy.querySelector('#sendCrashSms')?.closest('section')?.remove();copy.querySelector('#piezoPanel')?.remove();copy.querySelectorAll('a[href="/admin/tools#led-test"]').forEach(link=>link.remove());return copy.textContent;});assert(!forbidden.test(publicText.replaceAll('Pairing / SMS Test','').replaceAll('Crash SMS tests','').replaceAll('Crash SMS test','').replaceAll('SMS Test','').replaceAll('Ride Test','')),url+'\n'+text);
   assert.equal(await page.locator('#sendCheck,#systemTest,#cameraTest').count(),0,url);assert.equal(await page.locator('[data-led]').count(),url==='/admin/tools'?6:0,url);assert.equal(await page.locator('#sendCrashSms').count(),url==='/admin/tools'?1:0);
   if(url==='/admin/sensing')await page.locator('#automaticDetectionStatus').filter({hasText:'Detection ready'}).waitFor();
   if(url==='/admin/incidents')assert(!text.includes('Archived diagnostic'));
  }
  assert.equal(harness.state.incidents.length,2,'old records are retained in storage');
  assert.equal(harness.state.jobs.length,2,'history filtering does not delete jobs');
  assert.equal(harness.state.requests.filter(r=>r.method==='POST'&&r.path!=='/api/v1/login').length,0,'viewing pages must not send messages or reset state');
  console.log('PASS all 13 administrative pages: manual SMS test controls confined to Pairing / SMS Test; historical diagnostic records retained but hidden; no automatic writes');
  await page.click('#logout');await page.locator('#authWrap').waitFor({state:'visible'});
  await page.fill('#authUser','admin');await page.fill('#authPass','admin123');await page.click('#loginSubmit');await ready();
  for(const url of ['/','/equipment','/contacts','/activity','/account']){
   await page.goto(base+url);await ready();assert(!forbidden.test(await page.locator('body').innerText()),url);
  }
  if(process.env.SJKS_FINAL_PREVIEW){await page.goto(base+'/');await ready();await page.screenshot({path:path.join(process.env.SJKS_FINAL_PREVIEW,'rider-overview.png'),fullPage:true});}
  await page.setViewportSize({width:390,height:844});await page.goto(base+'/equipment');await ready();
  const dimensions=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));assert(dimensions.scroll<=dimensions.width+1);
  assert.deepEqual(errors,[]);
  console.log('PASS all five rider pages: standard monitoring/alert presentation, no test labels, mobile width and no browser errors');
 }finally{await context.close();await browser.close();await harness.close();}
})().catch(e=>{console.error(e);process.exit(1)});
