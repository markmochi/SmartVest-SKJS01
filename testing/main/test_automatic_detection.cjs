const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const harness=createHarness(),base=await harness.start();
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true});
 const page=await browser.newPage({viewport:{width:1440,height:1000}}),errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 try{
  await page.goto(base+'/admin/sensing');await page.fill('#authUser','developer');await page.fill('#authPass','developer-test-password');await page.click('#loginSubmit');
  await page.locator('#imuLiveStatus').filter({hasText:'IMU online'}).waitFor();
  Object.assign(harness.state.imuLab.imu.detector,{automaticDetectionEnabled:true,mainAutomaticDetectionEnabled:true,automaticReady:false});
  await page.locator('#automaticDetectionStatus').filter({hasText:'Detection waiting'}).waitFor();
  harness.state.imuLab.imu.detector.automaticReady=true;
  await page.locator('#automaticDetectionStatus').filter({hasText:'Detection ready'}).waitFor();
  assert.equal(harness.state.requests.filter(r=>r.path.endsWith('/sensing/command')).length,0);
  harness.state.imuLab.imu.healthy=false;
  await page.locator('#automaticDetectionStatus').filter({hasText:'Detection waiting'}).waitFor();
  console.log('PASS automatic detection UI: readiness, unhealthy stream cannot show ready, no automatic UI commands');
  const row={boot:123,sequence:1,test:true,sensorUptimeMs:900,triggerTimeKnown:true,front:'protected_reported',rear:'received',message:'SmartVest <script>bad()</script>',
   latency:{available:true,estimatedDetectionToMainMs:0,front:{firstOfferedMs:50,receivedAckMs:70,protectedAckMs:300000},rear:{firstOfferedMs:80,receivedAckMs:90,protectedAckMs:null}},
   recipients:[{phone:'+639123456789',state:'accepted',latency:{queuedMs:10,submissionIntentMs:20,firstResultMs:40}}]};
  await page.route('**/api/v1/incidents',r=>r.fulfill({contentType:'application/json',body:JSON.stringify({loaded:true,writable:true,automaticCrashDetectionEnabled:true,incidents:[row]})}));
  await page.goto(base+'/admin/incidents');await page.locator('.incident-card').waitFor();
  assert(await page.getByText('Date and time unavailable',{exact:true}).isVisible());
  assert(await page.getByText('Recording protected',{exact:true}).isVisible());
  assert(await page.getByText('Protecting recording',{exact:true}).isVisible());
  assert.equal(await page.getByText('Communication timing',{exact:true}).count(),0);
  await page.getByText('Saved alert message',{exact:true}).click();
  assert(await page.getByText('SmartVest <script>bad()</script>',{exact:true}).isVisible());
  assert.equal(await page.locator('#pageContent script').count(),0);
  if(process.env.SJKS_BENCH_PREVIEW)await page.screenshot({path:process.env.SJKS_BENCH_PREVIEW,fullPage:true});
  await page.setViewportSize({width:390,height:844});
  const dims=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));assert(dims.scroll<=dims.width+1);
  assert.deepEqual(errors,[]);
  console.log('PASS incident UI: honest legacy date, independent camera outcomes, plain language, escaped saved message, mobile width');
 }finally{await browser.close();await harness.close();}
})().catch(e=>{console.error(e);process.exit(1)});
