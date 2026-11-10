// UI fixtures only; no vest, modem or camera is contacted.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {launch,login,theme}=require('./dashboard-test-helpers.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
const alert=(sequence,time,recipients)=>({boot:42,sequence,occurredAtUtcMs:Date.parse(time),movement:{recorded:true,accelerationG:2.8,rotationDps:156},location:{state:'not_detected'},mobile:{state:'not_detected'},impact:{recorded:true,front:true},cameraPresence:{front:'detected',rear:'not_detected'},front:'protected_reported',rear:'pending',battery:{state:'detected',percent:60},recipients});
(async()=>{
 const h=createHarness(),browser=await launch(),errors=[],base=await h.start();
 try{
  for(const role of ['admin','rider']){
   h.state.incidents=[alert(1,'2026-10-28T12:01:02Z',[{name:'Jamie',phone:'+639170000001',state:'waiting'},{name:'Pat',phone:'+639170000002',state:'accepted'}]),alert(2,'2026-10-28T12:05:06Z',[{name:'Casey',phone:'+639170000003',state:'waiting'},{name:'Morgan',phone:'+639170000004',state:'retry'}])];
   h.state.jobs=[{id:301,kind:'addition',phone:'+639170000005',state:'queued'},{id:302,kind:'check',phone:'+639170000006',state:'sending',cancellable:false}];
   const context=await browser.newContext({viewport:{width:1440,height:1080}}),page=await context.newPage();
   page.on('pageerror',e=>errors.push(e.message));
   await page.goto(base+(role==='admin'?'/admin/incidents':'/activity'));await login(page,role);
   await page.locator('.incident-card').nth(1).waitFor();
   assert.equal(await page.locator('.incident-card').count(),2);
   assert.match(await page.locator('.incident-card').first().innerText(),/8:05:06 PM/);
   const boxes=await page.locator('.incident-card').evaluateAll(cards=>cards.map(c=>{const b=c.getBoundingClientRect();return {top:b.top,bottom:b.bottom,border:getComputedStyle(c).borderTopWidth};}));
   assert(boxes[1].top-boxes[0].bottom>=24);assert.equal(boxes[0].border,'1px');
   assert.deepEqual(await page.locator('.incident-recipients').evaluateAll(nodes=>nodes.map(n=>n.open)),[false,false]);
   const colours=await page.locator('.incident-module').first().evaluate(el=>({label:getComputedStyle(el.querySelector('h4')).color,status:getComputedStyle(el.querySelector('.status')).color}));
   assert.notEqual(colours.label,colours.status,'module heading and status use distinct colours');
   await page.locator('.incident-recipients summary').first().click();
   await page.waitForTimeout(2300);
   assert.deepEqual(await page.locator('.incident-recipients').evaluateAll(nodes=>nodes.map(n=>n.open)),[true,false]);
   await page.locator('.incident-recipients summary').first().click();
   for(const appearance of ['cream','dim']){
    await theme(page,appearance);
    for(const width of [320,390,768,1440]){
     await page.evaluate(()=>document.activeElement?.blur());await page.setViewportSize({width,height:1080});await page.evaluate(()=>scrollTo(0,0));await page.waitForFunction(()=>!document.querySelector('[data-message-cancel=all]').disabled);
     const size=await page.evaluate(()=>({w:innerWidth,s:document.documentElement.scrollWidth}));assert(size.s<=size.w+1,JSON.stringify({role,width,...size}));
     if([390,1440].includes(width))await page.screenshot({path:testOutput(`previews/${role}-${appearance}-${width}.png`),fullPage:true});
    }
   }
   await page.locator('.message-queue-details summary').click();
   const casey=page.locator('.message-queue-list li').filter({hasText:'Casey'});
   const writes=()=>h.state.requests.filter(r=>r.path.endsWith('/messages/cancel'));
   const before=writes().length;
   await casey.getByRole('button',{name:'Cancel pending text to Casey'}).click();await page.click('#confirmNo');assert.equal(writes().length,before);
   await casey.getByRole('button',{name:'Cancel pending text to Casey'}).click();await page.click('#confirmYes');
   await casey.waitFor({state:'detached'});
   assert.deepEqual(writes().at(-1).body,{boot:42,sequence:2,recipient:0});
   assert.equal(h.state.incidents[1].recipients[0].state,'cancelled');assert.equal(h.state.incidents[1].recipients[1].state,'retry');
   assert.equal(h.state.incidents[1].front,'protected_reported');
   await page.locator('[data-message-cancel="all"]').click();await page.click('#confirmYes');
   await page.waitForFunction(()=>document.querySelectorAll('.message-queue-list li').length===1);
   assert.equal(h.state.jobs[0].state,'cancelled');assert.equal(h.state.jobs[1].state,'sending');
   assert(await page.locator('[data-message-cancel="all"]').isDisabled());
   assert.match(await page.locator('.message-queue-list').innerText(),/Already handed to the mobile network/);
   assert.equal(h.state.incidents[0].recipients[1].state,'accepted');
   const forged=await page.request.post(base+'/api/v1/messages/cancel',{data:{all:true}});assert.equal(forged.status(),403,'writes require the session CSRF token');
   await page.reload();await page.locator('.incident-card').nth(1).waitFor();
   assert.deepEqual(await page.locator('.incident-recipients').evaluateAll(nodes=>nodes.map(n=>n.open)),[false,false]);
   if(role==='admin'){
    h.state.jobs=[{id:400,kind:'addition',phone:'+639170000005',state:'queued'}];
    await page.goto(base+'/admin/activity');await page.locator('.message-queue-details summary').click();await page.locator('.message-queue-list [data-message-cancel]').first().click();await page.click('#confirmYes');
    await page.locator('.message-queue-list').waitFor({state:'detached'});assert.equal(h.state.jobs[0].state,'cancelled');
   }
   await context.close();
  }
  assert.deepEqual(errors,[]);
  console.log('PASS separated dated alert cards, independent module/status colours, collapsed contacts with refresh preservation, both themes/320–1440 layouts, rider/admin per-text and cancel-all controls, submitted-text distinction and CSRF enforcement');
 }finally{await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
