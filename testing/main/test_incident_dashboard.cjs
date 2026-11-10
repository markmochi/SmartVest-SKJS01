// Simulated event records only. No physical vest, camera or SMS requests.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {launch,login,theme}=require('./dashboard-test-helpers.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
(async()=>{
 const h=createHarness(),browser=await launch(),errors=[];
 const row={boot:981,sequence:1,occurredAtUtcMs:Date.parse('2026-10-28T12:01:02Z'),timeBasis:'estimated_detection',movement:{recorded:true,accelerationG:2.8,rotationDps:156},location:{state:'last_known',latitude:14.5995,longitude:120.9842},mobile:{state:'detected',service:'ready'},impact:{state:'available',recorded:true,front:true,rear:false},cameraPresence:{front:'detected',rear:'not_detected'},front:'protected_reported',rear:'pending',battery:{state:'detected',percent:60,voltage:11.2},recipients:[{name:'Jamie',phone:'+639170000001',state:'accepted'},{name:'Second contact',phone:'+639170000002',state:'failed'}]};
 h.state.incidents=[row];const base=await h.start();
 try{
  for(const role of ['admin','rider']){
   const context=await browser.newContext({viewport:{width:1440,height:1080}}),page=await context.newPage();
   page.on('pageerror',error=>errors.push(error.message));
   await page.goto(base+(role==='admin'?'/admin/incidents':'/activity'));await login(page,role);
   await page.locator('.incident-card').waitFor();
   const card=page.locator('.incident-card');
   assert.equal(await card.locator('.incident-recipients').evaluate(el=>el.open),false);await card.locator('.incident-recipients summary').click();
   assert.match(await card.innerText(),/October 28, 2026 \| 8:01:02 PM/);
   assert.match(await card.innerText(),/2\.80/);assert.match(await card.innerText(),/Last known position recorded/);
   assert.match(await card.innerText(),/Mixed sending results/);assert.match(await card.innerText(),/Jamie/);
   assert.match(await card.innerText(),/delivery not confirmed/);assert.match(await card.innerText(),/Could not send/);
   assert.match(await card.innerText(),/Recording protected/);
   assert.equal(await card.locator('a.text-link').getAttribute('href'),'https://maps.google.com/?q=14.599500%2C%20120.984200');
   if(role==='rider')assert(!/NEO-6M|SIM800L|INA226|uptime|UART/i.test(await card.innerText()));
   for(const appearance of ['cream','dim']){
    await theme(page,appearance);
    for(const width of [320,390,1440]){
     await page.setViewportSize({width,height:1080});
     const size=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));
     assert(size.scroll<=size.width+1,`${role} ${appearance}: ${JSON.stringify(size)}`);
     if(width===390||width===1440)await page.screenshot({path:testOutput(`previews/${role}-${appearance}-${width}.png`),fullPage:true});
    }
   }
   row.location={state:'not_detected'};row.mobile={state:'not_detected'};row.recipients=[];row.noRecipients=true;
   await page.reload();await page.locator('.incident-card').waitFor();
   const location=page.locator('.incident-module').filter({has:page.locator('h4',{hasText:role==='admin'?'GPS location':'Location'})});
   assert.match(await location.innerText(),/Not detected/);assert(!/fix|unavailable/i.test(await location.innerText()));
   row.location={state:'last_known',latitude:14.5995,longitude:120.9842};row.mobile={state:'detected',service:'ready'};row.noRecipients=false;row.recipients=[{name:'Jamie',phone:'+639170000001',state:'accepted'},{name:'Second contact',phone:'+639170000002',state:'failed'}];
   await context.close();
  }
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0,'reading alerts has no control side effects');
  assert.deepEqual(errors,[]);
  console.log('PASS admin/rider saved incident cards, exact date, missing-module wording, mixed SMS results, protected footage, maps, two themes and 320/390/1440px layouts; no control writes');
 }finally{await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
