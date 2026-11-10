const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {createHarness}=require('./preview_server.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
const {prepare,launch,login,loaded,sensingReady,theme,contrast,riderPaths,adminPaths}=require('./dashboard-test-helpers.cjs');
(async()=>{
 const h=createHarness();prepare(h);
 const manifestPath=path.resolve(__dirname,'../../Main_and_Cameras/ui/manifest.json'),manifestBytes=fs.readFileSync(manifestPath),manifest=JSON.parse(manifestBytes),manifestSha256=crypto.createHash('sha256').update(manifestBytes).digest('hex');
 h.state.settings.rider='Alexandria Maximiliana Reyes Dela Cruz XX';
 h.state.settings.contacts=Array.from({length:5},(_,i)=>({name:'Alexandria Maximiliana Reyes Dela Cruz '+i,phone:'+63917000000'+i}));
 const base=await h.start(),browser=await launch(),results=[],errors=[];
 const widths=[320,390,768,1024,1440,1920];
 try{
  for(const role of ['rider','admin']){
   const context=await browser.newContext({viewport:{width:1440,height:1000}}),p=await context.newPage();
   p.on('pageerror',e=>errors.push(e.message));
   await p.goto(base+(role==='admin'?'/admin':'/'));
   await p.locator('#authWrap').waitFor({state:'visible'});
   assert.equal(await p.evaluate(()=>document.documentElement.dataset.theme),'cream');
   assert.equal(await p.locator('#appearanceToggle,[data-theme-choice]').count(),0,'appearance controls belong only to Account');
   assert.equal(await p.locator('.auth-art,.brand-mark svg').count(),0,'login uses the typographic mark, not the decorative vest');
   const copy=await p.locator('#authWrap').innerText();
   assert(!/Built around|The people who matter|Made for the rider|Stay informed|Welcome back/i.test(copy));
   assert.match(copy,/Crash monitoring/i);assert.match(copy,/(?:Video|Camera) recording/i);assert.match(copy,/Emergency SMS/i);
   for(const mode of ['cream','dim']){
    await theme(p,mode);
    for(const width of widths){
     await p.setViewportSize({width,height:900});
     assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),`${role} login ${mode} ${width} overflow`);
     const button=await p.locator('#loginSubmit').boundingBox();assert(button.height>=44&&button.width>=44);
    }
    await p.setViewportSize({width:1440,height:1000});
    await p.screenshot({path:testOutput('previews',`${role}-login-${mode}.png`),fullPage:true});
    await p.setViewportSize({width:390,height:844});
    await p.screenshot({path:testOutput('previews',`${role}-login-${mode}-mobile.png`),fullPage:true});
   }
   await theme(p,'cream');await login(p,role);
   const routes=role==='rider'?riderPaths:adminPaths;
   assert.equal(await p.locator('nav a[data-page]').count(),routes.length);
   for(const mode of ['cream','dim']){
    await theme(p,mode);
    for(const route of routes){
     await p.goto(base+route);await loaded(p);if(route==='/admin/sensing')await sensingReady(p);
     assert.equal(await p.evaluate(()=>document.documentElement.dataset.theme),mode,'appearance survives document navigation');
     for(const width of widths){
      await p.setViewportSize({width,height:900});
      const box=await p.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));
      assert(box.scroll<=box.width+1,`${route} ${mode} ${width}: ${JSON.stringify(box)}`);
      const text=await p.locator('#pageContent').innerText();assert(!/undefined|NaN|\[object Object\]/.test(text),route);
      if(role==='rider')assert(!/ESP32|UART|HTTP|JSON|heartbeat/i.test(text),route);
      const refresh=await p.locator('#refreshPage').boundingBox();assert(refresh&&refresh.height>=44,`${route}: refresh touch target`);
      results.push({role,route,mode,width,overflow:false});
     }
     await p.setViewportSize({width:1440,height:1000});
     const palette=await p.evaluate(()=>{const s=getComputedStyle(document.documentElement);return Object.fromEntries(['--ink','--muted','--pine','--paper','--canvas','--good','--warn','--bad'].map(k=>[k,s.getPropertyValue(k).trim()]));});
     for(const fg of ['--ink','--muted','--pine','--good','--warn','--bad'])for(const bg of ['--paper','--canvas'])assert(contrast(palette[fg],palette[bg])>=4.5,`${mode} ${fg}/${bg} contrast ${contrast(palette[fg],palette[bg])}`);
     await p.screenshot({path:testOutput('previews',`${role}-${route.replaceAll('/','_')||'overview'}-${mode}.png`),fullPage:true});
     if(['/','/equipment','/admin','/admin/sensing','/admin/cameras','/admin/tools'].includes(route)){
      await p.setViewportSize({width:390,height:844});
      await p.screenshot({path:testOutput('previews',`${role}-${route.replaceAll('/','_')||'overview'}-${mode}-mobile.png`),fullPage:true});
     }
    }
   }
   await p.goto(base+(role==='admin'?'/admin/account':'/account'));await loaded(p);
   assert.equal(await p.locator('.topbar [data-theme-choice],.topbar #appearanceToggle').count(),0);
   assert.equal(await p.locator('[data-theme-choice]').count(),2);
   assert.equal(await p.locator('[data-theme-choice=cream]').innerText(),'Light');
   await p.locator('#accountUsername').fill('unsaved_name');
   for(const mode of ['cream','dim']){
    await theme(p,mode);const selected=p.locator('[data-theme-choice="'+mode+'"]');assert.equal(await selected.getAttribute('aria-pressed'),'true');assert.equal(await p.inputValue('#accountUsername'),'unsaved_name');
    await selected.hover();await p.waitForTimeout(180);const colors=await selected.evaluate(e=>{const s=getComputedStyle(e);return [s.color,s.backgroundColor];});assert(contrast(...colors)>=4.5,'selected appearance control remains legible on hover');
   }
   await p.evaluate(()=>window.sjksDirty=false);
   await p.goto(base+(role==='admin'?'/admin/sensing':'/'));await loaded(p);if(role==='admin')await sensingReady(p);
   const canvas=p.locator('[data-rp-canvas]');assert.equal(await canvas.count(),1);
   await p.evaluate(()=>window.testCanvas=document.querySelector('[data-rp-canvas]'));
   for(let i=0;i<4;i++)await p.evaluate(()=>window.smartvestBoot.setAppearance(document.documentElement.dataset.theme==='dim'?'cream':'dim'));
   assert(await p.evaluate(()=>window.testCanvas===document.querySelector('[data-rp-canvas]')),'appearance must preserve the live canvas');
   assert.equal(await p.locator('[data-rp-canvas]').count(),1);
   await p.emulateMedia({reducedMotion:'reduce'});await p.waitForFunction(()=>!document.getElementById('refreshPage').disabled);
   await p.keyboard.press('Tab');await p.locator('#refreshPage').focus();
   assert(await p.locator('#refreshPage').evaluate(e=>getComputedStyle(e).outlineStyle!=='none'));
   await p.emulateMedia({forcedColors:'active'});assert(await p.locator('#refreshPage').isVisible());
   await p.emulateMedia({forcedColors:'none'});await p.setViewportSize({width:390,height:844});
   await p.goto(base+(role==='admin'?'/admin/contacts':'/contacts'));await loaded(p);
   await p.locator('.contact-row .row-tools button').first().click();await p.locator('#confirmDialog').waitFor({state:'visible'});
   assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));
   assert.equal(await p.evaluate(()=>document.activeElement.id),'confirmNo');
   await p.keyboard.press('Escape');assert(await p.locator('#confirmDialog').isHidden());
   await p.locator('#riderName').fill('Rider name edited locally');await p.locator('#riderName').focus();
   await p.evaluate(()=>window.smartvestBoot.setAppearance(document.documentElement.dataset.theme==='dim'?'cream':'dim'));assert.equal(await p.inputValue('#riderName'),'Rider name edited locally');
   await p.evaluate(()=>window.sjksDirty=false);
   await context.close();
  }
  assert.deepEqual(errors,[]);
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&!r.path.endsWith('/login')).length,0,'visual inspection does not send device commands');
  assert.equal(crypto.createHash('sha256').update(fs.readFileSync(manifestPath)).digest('hex'),manifestSha256,'assets must remain unchanged throughout browser verification');
  fs.writeFileSync(testOutput('matrix.json'),JSON.stringify({assetVersion:manifest.version,manifestSha256,compressedBytes:manifest.storedGzipBytes,scenarios:results.length,liveSensing:{accelerationG:Number(Math.hypot(100/2048,0,1).toFixed(3)),rotationDps:0,rateHz:200,temperatureC:23.2,automaticReady:true,completeRenderRequired:true},results,errors,scope:'Browser fixtures and viewport inspection; no physical hardware acceptance.'},null,2));
  console.log(`PASS ${results.length} route/theme/viewport combinations, both login designs, contrast, primary touch targets, stable canvas and no unintended writes`);
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
