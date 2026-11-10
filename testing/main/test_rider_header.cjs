// Local fixtures only. No radio, SMS, camera, or physical-device requests.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {createHarness}=require('./preview_server.cjs'),pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start(),browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true});
 const errors=[],csp=[],out=process.env.SJKS_MAIN_PREVIEW;
 h.state.status.riderBattery={percent:80,ageMs:1000,current:true,estimated:true,low:false};
 try{
  const page=await browser.newPage({viewport:{width:390,height:844}});
  page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error'&&/Content Security Policy|Refused to/.test(m.text()))csp.push(m.text());});
  await page.goto(base);assert(await page.locator('#batterySlot').isHidden());
  await page.fill('#authUser','admin');await page.fill('#authPass','admin123');await page.click('#loginSubmit');
  for(const url of ['/','/equipment','/contacts','/activity','/account']){
   await page.goto(base+url);const meter=page.locator('.topbar [role=meter][aria-valuenow="80"]');await meter.waitFor();
   assert.equal(await page.locator('#pageContent [data-component=battery]').count(),0);
   const box=await meter.boundingBox();assert(box.width<=40 && box.height<=16 && box.y<64);
   assert.equal(await page.locator('#batteryStatus').evaluate(e=>e.open),false);
   assert.equal((await page.locator('#batteryStatus summary').innerText()).trim(),'80%');
   await page.locator('#batteryStatus summary').focus();await page.keyboard.press('Enter');
   await page.locator('[data-battery-age]').waitFor();assert.match(await page.locator('#batteryStatus').innerText(),/Last updated 1 second ago/);
   await page.waitForTimeout(2200);assert(await page.locator('#batteryStatus').evaluate(e=>e.open));
   await page.keyboard.press('Escape');assert.equal(await page.locator('#batteryStatus').evaluate(e=>e.open),false);
   assert.equal(await page.evaluate(()=>document.activeElement.tagName),'SUMMARY');
   const size=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));assert(size.scroll<=size.width+1);
  }
  console.log('PASS compact top battery on all five rider routes; no battery card; keyboard open/Escape/focus and refresh preservation');
  await page.goto(base+'/');await page.locator('.ride-status').waitFor();
  for(const width of [320,390,768,1440]){
   await page.setViewportSize({width,height:1000});
   assert(await page.locator('#refreshPage').isVisible());
   assert(await page.locator('.rider-nav a[aria-current=page]').isVisible());
   const sizes=await page.evaluate(()=>({w:innerWidth,s:document.documentElement.scrollWidth}));assert(sizes.s<=sizes.w+1,JSON.stringify(sizes));
   if(out&&[390,1440].includes(width)){fs.mkdirSync(out,{recursive:true});await page.screenshot({path:path.join(out,`rider-overview-${width}.png`),fullPage:true});}
  }
  h.state.status.riderBattery={percent:10,ageMs:0,current:true,estimated:true,low:true};
  await page.locator('.ride-attention a[href="#batteryStatus"]').waitFor();await page.click('.ride-attention a[href="#batteryStatus"]');
  assert(await page.locator('#batteryStatus').evaluate(e=>e.open));assert.match(await page.locator('#batteryStatus').innerText(),/Charge your vest/);
  await page.locator('#pageTitle').click();assert.equal(await page.locator('#batteryStatus').evaluate(e=>e.open),false);
  h.state.status.sensing.armed=false;await page.locator('.ride-status').filter({hasText:'needs attention'}).waitFor();
  assert.match(await page.locator('.ride-attention').innerText(),/Crash monitoring is unavailable/);
  if(out){await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.join(out,'rider-attention-390.png'),fullPage:true});}
  h.state.offline=true;await page.clock.install();await page.clock.fastForward(20000);
  await page.locator('#connectionBanner').waitFor();assert(!/Monitoring and alerts ready/.test(await page.locator('.ride-status').innerText()));
  assert.equal(await page.locator('.topbar [role=meter]').getAttribute('aria-valuenow'),'10');
  assert.match(await page.locator('#batteryStatus summary').innerText(),/Last reading/);
  await page.clock.resume();h.state.offline=false;h.state.status.sensing.armed=true;
  await page.reload();await page.locator('#batteryStatus').waitFor();
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0);
  await page.click('#logout');await page.locator('#authWrap').waitFor();assert(await page.locator('#batterySlot').isHidden());assert.equal(await page.locator('#batteryStatus').count(),0);
  assert.deepEqual(errors,[]);assert.deepEqual(csp,[]);
  console.log('PASS responsive 320/390/768/1440 layouts, actionable monitoring and battery warnings, honest lost-connection state and retained battery; no automatic writes or CSP errors');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1);});
