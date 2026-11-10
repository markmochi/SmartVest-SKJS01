const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const {createHarness}=require('./preview_server.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start(),bytes=Buffer.alloc(262144);for(let i=0;i<bytes.length;i++)bytes[i]=i%251;
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox']});
 try{
  for(const role of ['rider','developer']){
   h.state.fileLibrary=null;h.state.requests=[];
   h.state.cameraMonitor={schema:1,cameras:['front','rear'].map(id=>({id,online:true,statusFresh:true,heartbeatAgeMs:100,recordingReported:true,sdReported:true,telemetry:{schema:1,filesSupported:1,downloadsSupported:1,normalRetained:7,lockedRetained:6,totalBytes:8*1073741824,freeBytesAtBoundary:5*1073741824,currentFrames:12,frameAgeMs:30}})),logs:[]};
   let filename="SmartVest_front_PROTECTED_INTERRUPTION.avi";
   const ctx=await browser.newContext({viewport:{width:390,height:844},acceptDownloads:true}),p=await ctx.newPage(),errors=[];p.on('pageerror',error=>errors.push(error.message));p.setDefaultTimeout(15000);
   const login=await ctx.request.post(base+'/api/v1/login',{data:{username:role==='rider'?'admin':'developer',password:role==='rider'?'admin123':'developer-test-password'}});assert(login.ok());const auth=await login.json();
   await p.route('http://192.168.4.*:8081/**',route=>route.fulfill({status:200,headers:{'Content-Type':'video/x-msvideo','Content-Length':String(bytes.length),'Content-Disposition':'attachment; filename="'+filename+'"','Cache-Control':'no-store'},body:bytes}));
   await p.goto(base+(role==='rider'?'/cameras':'/admin/cameras'));await p.locator('#frontStorage').filter({hasText:'13 total'}).waitFor();
   await p.locator('#frontFiles2 > summary').click();const button=p.locator('#frontList2 [data-download]').first();await button.waitFor();
   await button.click();assert.match(await p.locator('#confirmText').innerText(),/Recording on the front camera pauses/);await p.locator('#confirmNo').click();assert(!h.state.requests.some(r=>r.body.operation===5));
   await button.click();const pending=p.waitForEvent('download');await p.locator('#confirmYes').click();const saved=await pending;
   assert.equal(saved.suggestedFilename(),'SmartVest_front_PROTECTED_INTERRUPTION.avi');assert.equal(await saved.failure(),null);assert.deepEqual(await fs.readFile(await saved.path()),bytes);
   assert.equal(h.state.fileLibrary.rows.front.length,13);assert.equal(h.state.fileLibrary.rows.rear.length,13);
   assert.equal(h.state.requests.filter(r=>r.body.operation===5).length,1);
   const blocked=await ctx.request.post(base+'/api/v1/cameras/files?camera=rear',{headers:{'X-CSRF-Token':auth.csrf},data:{operation:5,beforeName:'anything',beforeSession:1,beforeStart:1}});assert.equal(blocked.status(),409);
   await p.locator('#cameraDownloadStatus').filter({hasText:'Waiting for your browser'}).waitFor();
   assert(await p.locator('[data-library="front"] [data-delete="4"]').isDisabled());assert(await p.locator('[data-library="rear"] [data-delete="4"]').isDisabled());
   h.state.fileLibrary.download.state=2;h.state.fileLibrary.download.sent=bytes.length/2;
   await p.locator('#cameraDownloadStatus').filter({hasText:'50% sent'}).waitFor();
   for(const width of [320,390,768,1440]){await p.setViewportSize({width,height:844});assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));}
   await p.setViewportSize({width:390,height:844});await p.evaluate(()=>{document.activeElement?.blur();scrollTo(0,0);});await p.screenshot({path:testOutput(role+'-progress.png'),fullPage:true});

   await p.locator('[data-cancel-download]').click();await p.locator('#cameraDownloadStatus').filter({hasText:'Download stopped'}).waitFor();
   filename='SmartVest_front_PROTECTED_INCIDENT.avi';await p.locator('#frontList2 [data-download]').nth(1).click();const second=p.waitForEvent('download');await p.locator('#confirmYes').click();const incidentSaved=await second;assert.equal(incidentSaved.suggestedFilename(),filename);assert.equal(await incidentSaved.failure(),null);
   h.state.fileLibrary.download.state=3;h.state.fileLibrary.download.sent=bytes.length;
   await p.locator('#cameraDownloadStatus').filter({hasText:'Transfer sent. Check your downloads.'}).waitFor();
   await p.waitForFunction(()=>!document.querySelector('[data-library="front"] [data-delete="4"]').disabled);
   assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));await p.evaluate(()=>{document.activeElement?.blur();scrollTo(0,0);});await p.screenshot({path:testOutput(role+'-download.png'),fullPage:true});assert.deepEqual(errors,[]);
   await p.unrouteAll({behavior:'wait'});await ctx.close();console.log('PASS',role,'native saved AVI bytes, confirmation, one global transfer, progress, cancellation, retry and restoration');
  }
 }finally{await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exit(1)});
