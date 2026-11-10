const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start(),browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox']});
 try{
  const context=await browser.newContext({viewport:{width:390,height:844}}),rider=await context.newPage(),errors=[];
  const p=h.state.status.power;Object.assign(p,{enabled:true,viaSensing:true,sensorModel:'INA226',responding:true,sampleValid:true,state:'measured',sourceV:12.01,busV:12,currentMA:100,powerMW:1200,batteryPercent:60,batteryCalibrated:false,batteryEstimated:true,monitorsBatteryPack:true,sampleAgeMs:200,nanoBaud:19200,sensingRxPin:32,nanoI2cError:0});
  async function login(page,user,pass,url){page.on('pageerror',e=>errors.push(e.message));await page.goto(base+url);await page.fill('#authUser',user);await page.fill('#authPass',pass);await page.click('#loginSubmit');await page.locator('#workspace').waitFor({state:'visible'});}
  await login(rider,'admin','admin123','/equipment');
  const card=rider.locator('[data-component="battery"]');await card.locator('[role="meter"][aria-valuenow="60"]').waitFor();await card.locator('summary').click();assert(!/12\.01 V|INA226|UART|voltage/i.test(await card.innerText()));assert.match(await card.innerText(),/Battery level/);await card.locator('[data-battery-age]').waitFor();
  Object.assign(p,{sampleValid:false,state:'stale',sourceV:null,batteryPercent:null});await card.filter({hasText:'Last reading'}).waitFor();assert.equal(await card.locator('[role="meter"]').getAttribute('aria-valuenow'),'60');assert.match(await card.innerText(),/Last updated .+ ago/);
  Object.assign(p,{sampleValid:true,state:'measured',sourceV:11.4,batteryPercent:20});await card.locator('[role="meter"][aria-valuenow="20"]').waitFor();assert.match(await card.innerText(),/Low battery/);
  Object.assign(p,{batteryPercent:30});await card.locator('[role="meter"][aria-valuenow="30"]').waitFor();assert.match(await card.innerText(),/Low battery/);
  Object.assign(p,{batteryPercent:40});await card.locator('[role="meter"][aria-valuenow="40"]').waitFor();assert(!/Low battery/.test(await card.innerText()));
  // Main retains a measured snapshot even when a new browser opens offline.
  h.state.status.riderBattery={percent:20,voltageV:11.4,ageMs:120000,current:false,estimated:true,low:true};
  await rider.reload();await card.locator('[role="meter"][aria-valuenow="20"]').waitFor();await card.locator('summary').click();assert.match(await card.innerText(),/Last updated 2 minutes ago/);
  // Age keeps advancing even while HTTP itself stops responding.
  await rider.route('**/api/v1/rider/status*',route=>route.abort());
  await rider.clock.install();await rider.clock.fastForward(60000);
  await rider.waitForFunction(()=>document.querySelector('[data-battery-age]').textContent.includes('3 minutes ago'));
  assert.equal(await card.locator('[role="meter"]').getAttribute('aria-valuenow'),'20');
  await rider.unroute('**/api/v1/rider/status*');await rider.clock.resume();
  h.state.status.riderBattery={percent:80,ageMs:1000,current:true,estimated:true,low:false};
  await rider.reload();await card.locator('[role="meter"][aria-valuenow="80"]').waitFor();await card.locator('summary').click();assert.match(await card.innerText(),/Last updated 1 second ago/);
  const units=await rider.evaluate(async()=>{const root=document.querySelector('script[type="module"]').src.replace(/shared\/boot.js$/,'');const m=await import(root+'rider/app.js');return [1000,30000,60000,120000,3600000,86400000].map(m.batteryAge);});
  assert.deepEqual(units,['1 second ago','30 seconds ago','1 minute ago','2 minutes ago','1 hour ago','1 day ago']);
  const admin=await browser.newPage();await login(admin,'developer','developer-test-password','/admin/power');await admin.locator('.diagnostic-summary').filter({hasText:'Measuring'}).waitFor();await admin.locator('details summary').click();
  const body=await admin.locator('body').innerText();assert.match(body,/INA226.*Nano.*Sensing.*Main/);assert.match(body,/Nano A4/);assert.match(body,/GPIO32/);assert(!/Main SDA|Main SCL/.test(body));
  Object.assign(p,{nanoConnected:true,sampleValid:false,responding:false,state:'offline',nanoI2cError:2,batteryPercent:null,sourceV:null,busV:null,currentMA:null,powerMW:null});
  await admin.locator('.diagnostic-summary').filter({hasText:'Nano connected / monitor unavailable'}).waitFor();
  await admin.locator('.notice').filter({hasText:'Nano is communicating. INA226 did not reply.'}).waitFor();
  Object.assign(p,{responding:true,nanoI2cError:8});await admin.locator('.notice').filter({hasText:'INA226 settings mismatch'}).waitFor();
  Object.assign(p,{nanoI2cError:10});await admin.locator('.notice').filter({hasText:'Reading outside the expected battery range'}).waitFor();
  Object.assign(p,{nanoConnected:false,sampleValid:false,responding:false,state:'offline',batteryPercent:null,sourceV:null});await admin.locator('.diagnostic-summary').filter({hasText:'No battery telemetry'}).waitFor();
  delete h.state.status.riderBattery;
  Object.assign(p,{nanoConnected:true,sampleValid:true,responding:true,state:'measured',batteryPercent:100,sourceV:11.65,busV:11.625,nanoSequence:100,lowBatteryVoltage:false,nanoI2cError:0});
  await rider.reload();await card.locator('[role="meter"][aria-valuenow="100"]').waitFor();
  const voltage=admin.locator('dt').filter({hasText:/^Source voltage \(smoothed\)$/});
  const shown=[];
  for(let i=0;i<6;i++){
   p.nanoSequence++;p.sourceV=i%2?11.8:11.65;p.busV=p.sourceV-.025;
   await admin.click('#refreshPage');await admin.waitForFunction(()=>!document.getElementById('refreshPage').disabled);
   shown.push(await voltage.evaluate(e=>e.nextElementSibling.textContent));
   assert.equal(await admin.locator('dt').filter({hasText:/^Source voltage \(raw\)$/}).evaluate(e=>e.nextElementSibling.textContent),p.sourceV.toFixed(2)+' V');
   assert.match(await admin.locator('#pageContent').innerText(),/100%/);
  }
  assert.equal(new Set(shown).size,1,'raw voltage fluctuations must not toggle the main voltage display');
  await admin.screenshot({path:testOutput('full-charge-power.png'),fullPage:true});
  console.log('PASS actual rider/developer full-charge displays: 100% with 11.65 to 11.80 V fluctuations, stable smoothed voltage and exact raw details');
  assert.deepEqual(errors,[]);assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0);
  console.log('PASS rider retained battery level with advancing seconds/minutes/hours/days ago, page reload, HTTP loss and recovery, simple wording and administrator Nano/Sensing wiring/offline diagnostics; no automatic writes');
 }finally{await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exit(1);});
