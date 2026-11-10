const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start();
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox']});
 try{
  for(const role of ['rider','developer']){
   h.state.fileLibrary=null;h.state.requests=[];
   const ctx=await browser.newContext({viewport:{width:390,height:844}}),p=await ctx.newPage(),errors=[];p.on('pageerror',e=>errors.push(e.message));p.setDefaultTimeout(15000);
   const login=await ctx.request.post(base+'/api/v1/login',{data:{username:role==='rider'?'admin':'developer',password:role==='rider'?'admin123':'developer-test-password'}});assert(login.ok());
   // The admin diagnostics API and the shared library report the same card.
   h.state.cameraMonitor={schema:1,cameras:['front','rear'].map(id=>({id,online:true,statusFresh:true,heartbeatAgeMs:300,recordingReported:true,sdReported:true,telemetry:{schema:1,filesSupported:2,normalRetained:7,lockedRetained:6,totalBytes:8*1073741824,freeBytesAtBoundary:5*1073741824,latestUtcMs:1700000780000,currentFrames:12,frameAgeMs:30}})),logs:[]};
   let failResult=false,priorBusy=false,priorPolls=0,stalled=false;
   await p.route('**/api/v1/cameras/files*',async route=>{
    const url=new URL(route.request().url()),job=url.searchParams.get('job');
    if(priorBusy&&route.request().method()==='GET'&&url.searchParams.get('camera')==='front'&&!job){const reply=await route.fetch();await route.fulfill({response:reply,json:{...(await reply.json()),busy:true,requestId:900}});return;}
    if(job==='900'){++priorPolls;if(priorPolls===2)priorBusy=false;await route.fulfill({status:priorBusy?202:200,json:{id:900,pending:priorBusy,error:''}});return;}
    if(priorBusy&&route.request().method()==='POST')throw new Error('Started a second camera operation before the previous one finished');
    if(stalled&&job){await route.fulfill({status:503,json:{error:'Temporarily unavailable'}});return;}
    if(failResult&&job){failResult=false;await route.fulfill({status:503,json:{error:'Temporary allocation failure'}});return;}
    await route.continue();
   });
   await p.goto(base+(role==='rider'?'/cameras':'/admin/cameras'));
   await p.locator('#frontStorage').filter({hasText:'13 total'}).waitFor();
   assert.match(await p.locator('#frontStorage').innerText(),/7 normal · 6 protected/);
   const cleanup=p.locator('[data-library="front"] [data-delete="7"]');assert(await cleanup.isEnabled());
   await ctx.request.get(base+'/api/v1/cameras/files');
   h.state.fileLibrary.filesVersion=1;h.state.cameraMonitor.cameras[0].telemetry.filesSupported=1;
   await p.waitForFunction(()=>document.querySelector('[data-library="front"] [data-delete="7"]').disabled);
   assert.match(await cleanup.getAttribute('title'),/Update this camera/);
   h.state.fileLibrary.filesVersion=2;h.state.cameraMonitor.cameras[0].telemetry.filesSupported=2;
   await p.waitForFunction(()=>!document.querySelector('[data-library="front"] [data-delete="7"]').disabled);
   await p.waitForTimeout(4200);assert(!h.state.requests.some(r=>r.body?.operation===1||r.body?.operation===2),'closed lists must not request SD scans');
   await p.locator('#frontFiles2 > summary').click();await p.locator('#frontList2 .history-item').nth(4).waitFor();assert.equal(await p.locator('#frontList2 .history-item').count(),5);assert.match(await p.locator('#frontList2').innerText(),/Recording interruption · recovered footage/);assert.match(await p.locator('#frontList2').innerText(),/Incident protection · crash alert or test/);assert.match(await p.locator('#frontList2').innerText(),/Recording time unavailable/);assert.match(await p.locator('#frontList2').innerText(),/Recovered: time unavailable/);
   await p.locator('[data-library="front"] [data-file-action="next"][data-kind="2"]').click();await p.locator('#frontPage2').filter({hasText:'Page 2'}).waitFor();assert.equal(await p.locator('#frontList2 .history-item').count(),1);
   await p.locator('[data-library="front"] [data-file-action="back"][data-kind="2"]').click();await p.locator('#frontPage2').filter({hasText:'Page 1'}).waitFor();
   priorBusy=true;await p.locator('[data-library="front"] [data-file-action="refresh"][data-kind="2"]').click();
   await p.locator('#frontFileStatus').filter({hasText:'Finishing the previous camera request'}).waitFor();
   await p.locator('#frontFileStatus').filter({hasText:'List updated.'}).waitFor();assert.equal(priorPolls,2);
   // Switching lists while a page is being read must load the newly opened list.
   await p.locator('#rearFiles1 > summary').click();await p.locator('#rearFiles2 > summary').click();
   await p.locator('#rearList2 .history-item').nth(4).waitFor();
   await p.locator('[data-key="front-manage"] > summary').click();await p.locator('[data-library="front"] [data-delete="3"]').click();await p.locator('#confirmNo').click();assert(!h.state.requests.some(r=>r.body.operation===3));
   failResult=true;await p.locator('[data-library="front"] [data-delete="3"]').click();await p.locator('#confirmYes').click();await p.locator('#frontStorage').filter({hasText:'6 total'}).waitFor();assert.equal(h.state.requests.filter(r=>r.body.operation===3).length,1,'result retry must not replay deletion');assert.equal(h.state.fileLibrary.rows.front.length,6);assert(h.state.fileLibrary.rows.front.every(r=>r.protected));assert.equal(h.state.fileLibrary.rows.rear.length,13);
   await p.locator('[data-library="front"] [data-delete="7"]').click();assert.match(await p.locator('#confirmText').innerText(),/recovery copies/);await p.locator('#confirmNo').click();assert(!h.state.requests.some(r=>r.body.operation===7));
   await p.locator('[data-library="front"] [data-delete="7"]').click();await p.locator('#confirmYes').click();
   await p.locator('#frontStorage').filter({hasText:'5 total'}).waitFor();assert.equal(h.state.requests.filter(r=>r.body.operation===7).length,1);assert(h.state.fileLibrary.rows.front.every(r=>r.protectionReason!==2));assert.equal(h.state.fileLibrary.rows.rear.length,13);
   await p.locator('[data-library="front"] [data-delete="4"]').click();assert.match(await p.locator('#confirmText').innerText(),/all protected clips/);await p.locator('#confirmYes').click();await p.locator('#frontStorage').filter({hasText:'0 total'}).waitFor();assert.equal(h.state.fileLibrary.rows.front.length,0);
   for(const width of [320,390,768,1440]){await p.setViewportSize({width,height:900});assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));if(role==="rider"&&width<768){const tops=await p.locator(".rider-nav a:visible").evaluateAll(links=>links.map(link=>Math.round(link.getBoundingClientRect().top)));assert.equal(new Set(tops).size,1,"six mobile tabs must share one row");}}
   await p.setViewportSize({width:390,height:844});await p.evaluate(()=>{document.activeElement?.blur();scrollTo(0,0);});await p.screenshot({path:testOutput(role+'-camera-files.png'),fullPage:true});await p.clock.install();stalled=true;
   await p.locator('[data-library="rear"] [data-file-action="refresh"][data-kind="2"]').click();
   await p.locator('#rearFileStatus').filter({hasText:'Reading saved clips'}).waitFor();
   await p.clock.fastForward(126000);
   await p.locator('#rearFileStatus').filter({hasText:'The camera did not finish in time'}).waitFor();
   assert(await p.locator('[data-library="rear"] [data-delete="4"]').isEnabled());
   assert.deepEqual(errors,[]);await p.unrouteAll({behavior:'wait'});await ctx.close();console.log('PASS',role,'camera library: totals, five-row paging, confirmation/cancellation, scoped normal/all deletion and responsive layout');
  }
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1)});
