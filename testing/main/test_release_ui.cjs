const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start(),browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true});
 const errors=[],csp=[];
 async function login(user,password,url){const page=await browser.newPage({viewport:{width:390,height:844}});page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error'&&/Content Security Policy|Refused to/.test(m.text()))csp.push(m.text());});await page.goto(base+url);await page.fill('#authUser',user);await page.fill('#authPass',password);await page.click('#loginSubmit');await page.locator('#workspace').waitFor();return page;}
 try{
  const admin=await login('developer','developer-test-password','/admin/tools');
  await admin.locator('#sendCrashSms').waitFor();assert.equal(await admin.locator('#pageTitle').innerText(),'Pairing / SMS Test');
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0);
  const incidents=h.state.incidents.length;
  await admin.click('#sendCrashSms');await admin.click('#confirmNo');
  assert.equal(h.state.requests.filter(r=>r.path.endsWith('/messages/crash')).length,0);
  await admin.click('#sendCrashSms');await admin.click('#confirmYes');
  await admin.locator('#contactMessage').filter({hasText:'Crash SMS queued'}).waitFor();
  const requests=h.state.requests.filter(r=>r.path.endsWith('/messages/crash'));assert.equal(requests.length,1);
  assert.deepEqual(requests[0].body,{phone:'+639170000001'});
  assert.equal(h.state.incidents.length,incidents);
  assert.equal(h.state.requests.filter(r=>/\/crash\/test|\/cameras\/test/.test(r.path)).length,0);
  const job=h.state.jobs.at(-1);assert.equal(job.kind,'manual_crash');assert(!/test/i.test(job.text));
  await admin.locator('#queueProgress').filter({hasText:'Message #'+job.id}).waitFor();
  job.state='sending';job.detail='Submitting selected-contact SMS';
  await admin.locator('#queueProgress').filter({hasText:job.detail}).waitFor();
  job.state='accepted';job.detail='Submitted to network';await admin.locator('#queueProgress').filter({hasText:job.detail}).waitFor();
  const sensor=h.state.status.sensing;Object.assign(sensor,{enabled:true,online:true,armed:true,outboxWritable:true,pending:0});
  const cases=[[true,false,'Connected / Piezo not ready'],[false,true,'Connected / IMU not ready'],[false,false,'Connected / IMU & Piezo not ready'],[true,true,'Connected']];
  await admin.goto(base+'/admin');
  for(const [imu,piezo,label]of cases){Object.assign(sensor,{imuReady:imu,piezoConfigured:piezo});await admin.waitForFunction(label=>[...document.querySelectorAll('tr')].find(r=>r.textContent.includes('Sensing board'))?.querySelector('.status')?.textContent===label,label);assert.equal(await admin.locator('tr').filter({hasText:'Sensing board'}).locator('.status').innerText(),label);}
  sensor.armed=false;
  for(const [imu,piezo,label]of cases){Object.assign(sensor,{imuReady:imu,piezoConfigured:piezo});await admin.waitForFunction(label=>[...document.querySelectorAll('tr')].find(r=>r.textContent.includes('Sensing board'))?.querySelector('.status')?.textContent===label,label);}
  await admin.locator('tr').filter({hasText:'Sensing board'}).filter({hasText:'Detection not armed'}).waitFor();
  for(const [field,value,text] of [['outboxWritable',false,'Event storage not ready'],['outboxWritable',true,'Detection not armed'],['pending',4,'Event queue full']]){
   sensor[field]=value;const row=admin.locator('tr').filter({hasText:'Sensing board'});await row.filter({hasText:text}).waitFor();assert.equal(await row.locator('.status').innerText(),'Connected');
  }
  sensor.pending=0;
  sensor.online=false;await admin.locator('tr').filter({hasText:'Sensing board'}).filter({hasText:'No heartbeat'}).waitFor();
  const p=h.state.status.power;Object.assign(p,{enabled:true,viaSensing:true,sensorModel:'INA226',sampleValid:true,batteryPercent:80,sampleAgeMs:0});
  const rider=await login('admin','admin123','/equipment'),card=rider.locator('[data-component="battery"]');
  for(const percent of [0,10,20,80,100]){
   h.state.status.riderBattery={percent,current:true,estimated:true,low:percent<=20,ageMs:0};
   await rider.reload();
   await card.locator(`[role="meter"][aria-valuenow="${percent}"]`).waitFor();
   await rider.waitForTimeout(600);
   const geometry=await card.locator('.battery-gauge').evaluate(el=>({fill:el.querySelector('.battery-fill').getBBox().width,track:96,color:getComputedStyle(el.querySelector('.battery-fill')).fill,width:el.getBoundingClientRect().width,svg:el.querySelector('svg').getBoundingClientRect().height}));
   assert(Math.abs(geometry.fill/geometry.track-percent/100)<0.001,JSON.stringify(geometry));assert(geometry.width<=40&&geometry.svg>=12);
   if(percent===80){const [red,green,blue]=geometry.color.match(/\d+/g).map(Number);assert(green>red&&green>blue&&green-red>=20,'fresh battery fill remains visibly green');const out=process.env.SJKS_MAIN_PREVIEW;if(out){fs.mkdirSync(out,{recursive:true});await card.screenshot({path:path.join(out,'battery-80-mobile.png')});}}
  }
  h.state.status.riderBattery={percent:80,current:false,estimated:true,low:false,ageMs:120000};await rider.reload();await card.filter({hasText:'Last reading'}).waitFor();assert.equal(await card.locator('.battery-fill').getAttribute('width'),'76.80');
  assert.equal(await rider.locator('#sendCrashSms').count(),0);
  const denied=await rider.evaluate(async()=>{const session=await fetch('/api/v1/session').then(r=>r.json());return fetch('/api/v1/messages/crash',{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':session.csrf},body:JSON.stringify({phone:'+639170000001'})}).then(r=>r.status);});assert.equal(denied,403);
  const size=await rider.evaluate(()=>({w:innerWidth,s:document.documentElement.scrollWidth}));assert(size.s<=size.w+1);
  assert.deepEqual(errors,[]);assert.deepEqual(csp,[]);
  console.log('PASS selected-contact crash SMS confirmation/one request/live queue, no incident/camera side effects; Sensing readiness matrix; visible proportional SVG battery at 0/10/20/80/100 and retained 80%; rider denial, mobile width and CSP');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1)});
