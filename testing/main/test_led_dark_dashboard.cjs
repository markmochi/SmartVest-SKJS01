const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {createHarness}=require('./preview_server.cjs');
const {chromium}=require(process.env.SJKS_PLAYWRIGHT||'playwright');
(async()=>{
 const h=createHarness(),base=await h.start(),browser=await chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true});
 const errors=[];
 async function login(role,url){const p=await browser.newPage({viewport:{width:1440,height:1000}});p.on('pageerror',e=>errors.push(e.message));await p.goto(base+url);await p.fill('#authUser',role==='developer'?'developer':'admin');await p.fill('#authPass',role==='developer'?'developer-test-password':'admin123');await p.click('#loginSubmit');await p.locator('#workspace').waitFor();return p;}
 async function refresh(p){await p.waitForFunction(()=>!document.getElementById('refreshPage').disabled);await p.click('#refreshPage');await p.waitForFunction(()=>!document.getElementById('refreshPage').disabled);}
 try{
  const admin=await login('developer','/admin/tools');await admin.locator('[data-led="critical"]').waitFor();
  assert.equal(await admin.locator('[data-led]').count(),6);assert(await admin.locator('[data-led="critical"]').isDisabled());
  assert.equal(h.state.requests.filter(r=>r.path.endsWith('/power/led-test')).length,0);
  h.state.status.benchTestsEnabled=true;await refresh(admin);assert(await admin.locator('[data-led="critical"]').isEnabled());
  const snapshot=JSON.stringify({settings:h.state.settings,incidents:h.state.incidents,battery:h.state.status.power.batteryPercent});
  for(const mode of ['critical','low','full','charging','blue','off']){
   await admin.locator('[data-led="'+mode+'"]').click();await admin.waitForFunction(mode=>document.getElementById('ledPreview').dataset.mode===mode,mode);
   assert.equal(await admin.locator('[data-led="'+mode+'"]').getAttribute('aria-pressed'),'true');
   assert.match(await admin.locator('#ledStatus').innerText(),/requested/);
  }
  assert.equal(h.state.requests.filter(r=>r.path.endsWith('/power/led-test')).length,6);
  assert.equal(JSON.stringify({settings:h.state.settings,incidents:h.state.incidents,battery:h.state.status.power.batteryPercent}),snapshot);
  await admin.click('#stopLedTest');await admin.waitForFunction(()=>document.getElementById('ledPreview').dataset.mode==='none');
  assert.equal(h.state.status.power.ledTestActive,false);assert(await admin.locator('#stopLedTest').isDisabled());
  await admin.route('**/api/v1/power/led-test',r=>r.fulfill({status:503,contentType:'application/json',body:JSON.stringify({error:'Test service unavailable.'})}));
  await admin.locator('[data-led="full"]').click();await admin.locator('#ledMessage').filter({hasText:'Test service unavailable.'}).waitFor();assert.equal(await admin.locator('#ledPreview').getAttribute('data-mode'),'none');await admin.unroute('**/api/v1/power/led-test');
  await admin.locator('[data-led="blue"]').click();await admin.waitForFunction(()=>document.getElementById('ledPreview').dataset.mode==='blue');
  await admin.waitForFunction(()=>document.getElementById('ledPreview').dataset.mode==='none',null,{timeout:19000});await admin.locator('#ledStatus').filter({hasText:'Automatic battery indication is restored.'}).waitFor();assert.equal(h.state.status.power.ledTestActive,false);
  console.log('PASS all six acknowledged LED modes, failure feedback, stop, timed recovery, no load writes or battery/incident/settings changes');
  for(const width of [320,390,768,1024,1440,1920]){await admin.setViewportSize({width,height:900});assert(await admin.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));}
  await admin.emulateMedia({reducedMotion:'reduce'});await admin.locator('[data-led="critical"]').click();await admin.waitForFunction(()=>document.getElementById('ledPreview').dataset.mode==='critical');assert.equal(await admin.locator('.led-lens').evaluate(el=>getComputedStyle(el).animationName),'none');await admin.click('#stopLedTest');
  const rider=await login('rider','/');assert.equal(await rider.locator('[data-led],#led-test').count(),0);
  const rejection=await rider.evaluate(async()=>{const s=await fetch('/api/v1/session').then(r=>r.json());return (await fetch('/api/v1/power/led-test',{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':s.csrf},body:'{"mode":"full"}'})).status;});assert.equal(rejection,403);
  for(const p of [admin,rider])for(const theme of ['cream','dim']){
   if(await p.evaluate(()=>document.documentElement.dataset.theme)!==theme)await p.evaluate(value=>window.smartvestBoot.setAppearance(value),theme);
   assert.equal(await p.evaluate(()=>getComputedStyle(document.documentElement).colorScheme),theme==='cream'?'light':'dark');
   const colors=await p.evaluate(()=>{const s=getComputedStyle(document.documentElement);return ['--ink','--muted','--paper','--canvas','--pine','--good','--warn','--bad'].map(v=>s.getPropertyValue(v).trim());});
   function rgb(hex){return hex.slice(1).match(/../g).map(v=>parseInt(v,16)/255).map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4).reduce((a,v,i)=>a+v*[.2126,.7152,.0722][i],0);}
   const [ink,muted,paper,canvas,accent,good,warn,bad]=colors.map(rgb),contrast=(a,b)=>(Math.max(a,b)+.05)/(Math.min(a,b)+.05);
   for(const fg of [ink,muted,accent,good,warn,bad])for(const bg of [paper,canvas])assert(contrast(fg,bg)>=4.5,'text contrast '+contrast(fg,bg));
  }
  if(process.env.SJKS_EXPERIENCE_PREVIEW){fs.mkdirSync(process.env.SJKS_EXPERIENCE_PREVIEW,{recursive:true});await admin.setViewportSize({width:1440,height:1100});await admin.screenshot({path:path.join(process.env.SJKS_EXPERIENCE_PREVIEW,'dark-led-desktop.png'),fullPage:true});await admin.setViewportSize({width:390,height:844});await admin.screenshot({path:path.join(process.env.SJKS_EXPERIENCE_PREVIEW,'dark-led-mobile.png'),fullPage:true});await rider.setViewportSize({width:1440,height:1100});await rider.screenshot({path:path.join(process.env.SJKS_EXPERIENCE_PREVIEW,'dark-rider-desktop.png'),fullPage:true});}
  assert.deepEqual(errors,[]);console.log('PASS admin-only controls/API, responsive 320–1920, reduced motion, both-role cream/dim schemes and normal text palette contrast >=4.5:1');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1);});
