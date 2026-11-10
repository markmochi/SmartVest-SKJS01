const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start();
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox']});
 try{
  const rider=await browser.newPage({viewport:{width:390,height:844}}),admin=await browser.newPage(),errors=[];
  const p=h.state.status.power;
  Object.assign(p,{enabled:true,viaSensing:true,sensorModel:'INA226',responding:true,sampleValid:true,sourceV:11.8,batteryPercent:80,batteryEstimated:true,sampleAgeMs:0,charging:true,chargingEstimated:false,netCurrentMA:500});
  async function login(page,url,user,pass){page.on('pageerror',e=>errors.push(e.message));await page.goto(base+url);await page.fill('#authUser',user);await page.fill('#authPass',pass);await page.click('#loginSubmit');await page.locator('#workspace').waitFor({state:'visible'});}
  await login(rider,'/','admin','admin123');await login(admin,'/admin/power','developer','developer-test-password');
  const meter=rider.locator('[data-component="battery"]');
  await meter.locator('.battery-caption').filter({hasText:/^Charging$/}).waitFor();
  assert.equal(await meter.locator('[role=meter]').getAttribute('aria-valuenow'),'80');
  await meter.locator('summary').click();assert.equal(await meter.locator('.battery-bolt').count(),1);
  assert(!/possible|INA226|current|shunt|voltage|estimated|not charging/i.test(await meter.innerText()));
  const chargeRow=admin.locator('dt').filter({hasText:/^Charging status$/}).locator('xpath=following-sibling::dd[1]');
  await chargeRow.filter({hasText:/^Charging$/}).waitFor();
  await admin.locator('details summary').click();assert.match(await admin.locator('#pageContent').innerText(),/500 mA/);
  const screenshotDir=process.env.SJKS_SCREENSHOTS;
  if(screenshotDir)await rider.screenshot({path:screenshotDir+'/rider-charging.png'});
  for(const state of [false,null]){
    p.charging=state;p.netCurrentMA=state===false?-500:null;
    await chargeRow.filter({hasText:state===false?/^Not charging$/:/^Unavailable$/}).waitFor();
    await rider.waitForFunction(()=>document.querySelector('[data-component=battery] .battery-caption')===null);
    assert.equal(await meter.locator('.battery-bolt').count(),0);
    assert(!/charging/i.test(await meter.innerText()));
    assert.equal(await meter.locator('[role=meter]').getAttribute('aria-valuenow'),'80');
  }
  // Main maps old voltage-inferred states to unavailable, never Charging.
  p.chargingEstimated=false;p.charging=null;p.netCurrentMA=null;
  await chargeRow.filter({hasText:/^Unavailable$/}).waitFor();
  await rider.waitForFunction(()=>!document.querySelector('.battery-bolt'));
  assert(!/charging/i.test(await meter.innerText()));
  p.chargingEstimated=false;
  p.charging=true;p.netCurrentMA=500;await meter.locator('.battery-caption').filter({hasText:/^Charging$/}).waitFor();
  p.sampleValid=false;p.batteryPercent=null;await meter.locator('.battery-caption').filter({hasText:'Last reading'}).waitFor();
  assert.equal(await meter.locator('.battery-bolt').count(),0);assert(!/charging/i.test(await meter.innerText()));assert.equal(await meter.locator('[role=meter]').getAttribute('aria-valuenow'),'80');
  Object.assign(p,{sampleValid:true,batteryPercent:80,charging:true});await meter.locator('.battery-caption').filter({hasText:/^Charging$/}).waitFor();
  await rider.route('**/api/v1/rider/status*',route=>route.abort());
  await rider.clock.install();await rider.clock.fastForward(7000);
  await meter.locator('.battery-caption').filter({hasText:'Last reading'}).waitFor();
  assert.equal(await meter.locator('.battery-bolt').count(),0);assert(!/charging/i.test(await meter.innerText()));
  await rider.clock.resume();await rider.unroute('**/api/v1/rider/status*');
  for(const width of [320,390,1024]){
    await rider.setViewportSize({width,height:844});
    assert(await rider.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    const box=await meter.locator('svg').boundingBox();assert(box.width<=40&&box.height<=16);
  }
  // The bolt stays centred over the battery body at every fill level.
  Object.assign(p,{sampleValid:true,batteryPercent:80,charging:true,chargingEstimated:false});
  await meter.locator('.battery-bolt').waitFor();
  for(const percent of [0,10,20,50,80,100]){
    Object.assign(p,{batteryPercent:percent});
    await meter.locator(`[role=meter][aria-valuenow="${percent}"]`).waitFor();
    assert.equal(await meter.locator('.battery-caption').innerText(),'Charging');
    assert(!/voltage|estimated|not charging|charge your vest before/i.test(await meter.innerText()));
    const geometry=await meter.evaluate(el=>{
      const body=el.querySelector('.battery-shell').getBBox(),bolt=el.querySelector('.battery-bolt').getBBox();
      return {dx:Math.abs(body.x+body.width/2-bolt.x-bolt.width/2),dy:Math.abs(body.y+body.height/2-bolt.y-bolt.height/2)};
    });
    assert(geometry.dx<1&&geometry.dy<1);
  }
  p.batteryPercent=80;await meter.locator('[aria-valuenow="80"]').waitFor();
  await rider.setViewportSize({width:390,height:844});
  await meter.evaluate(el=>{el.open=false;});
  if(screenshotDir)await rider.screenshot({path:screenshotDir+'/rider-charging-final.png'});
  await meter.locator('summary').click();
  if(screenshotDir)await rider.screenshot({path:screenshotDir+'/rider-charging-popover.png'});
  for(const state of [false,null]){
    p.charging=state;await rider.waitForFunction(()=>!document.querySelector('.battery-bolt'));
    assert(!/not charging|voltage|estimated|checking charging/i.test(await meter.innerText()));
  }
  assert.deepEqual(errors,[]);
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0);
  console.log('PASS simple rider Charging only; centred bolt at 0/10/20/50/80/100%; no technical/negative charging copy; admin unknown status preserved; stale/HTTP-loss clears bolt; retained percent, compact layouts and no automatic writes');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1);});
