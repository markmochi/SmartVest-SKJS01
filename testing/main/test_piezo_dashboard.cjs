const {testOutput,testOutputDir}=require('../paths.cjs').suite(__filename);
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness();h.state.imuLab=require('./imu-fixtures.cjs').fixture();h.state.piezoTrialScale=.05;h.state.riderUsername='rider';h.state.riderPassword='rider-test-password';h.state.factory=false;const originalJobs=h.state.jobs.length;fs.mkdirSync(testOutputDir('previews'),{recursive:true});
 const base=await h.start(),browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader']}),errors=[];
 const page=await browser.newPage({viewport:{width:1440,height:1000},acceptDownloads:true});page.setDefaultTimeout(12000);page.on('pageerror',e=>errors.push(e.message));
 try{
  await page.goto(base+'/admin/sensing');await page.fill('#authUser','developer');await page.fill('#authPass','developer-test-password');await page.click('#loginSubmit');
  await page.locator('#riderPosition').waitFor();assert(await page.locator('#sensingpiezo').isHidden());
  const writesBeforeTabs=h.state.requests.filter(r=>r.method==='POST').length;
  assert.equal(await page.locator('#trialNotes').count(),0,'the separate IMU trial recorder is removed');
  await page.locator('#sensingTabimu').focus();await page.keyboard.press('ArrowRight');
  assert.equal(await page.locator('#sensingTabpiezo').getAttribute('aria-selected'),'true');assert(await page.locator('#sensingimu').isHidden());
  await page.locator('#piezoState').filter({hasText:'Listening for impacts'}).waitFor();
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&r.path.includes('piezo')).length,0);
  await page.fill('#frontImpactThreshold','650');await page.click('#sensingTabimu');assert(await page.locator('#boardCommunication').isVisible());
  await page.locator('#sensingTabimu').focus();await page.keyboard.press('End');assert.equal(h.state.requests.filter(r=>r.method==='POST').length,writesBeforeTabs);await page.waitForTimeout(700);assert.equal(await page.inputValue('#frontImpactThreshold'),'650');
  await page.click('#saveImpactThresholds');await page.locator('#impactActionMessage').filter({hasText:'Thresholds saved on the vest'}).waitFor();
  assert.equal(await page.getAttribute('#frontImpactThreshold','max'),'800');assert.equal(h.state.imuLab.piezo.rear.threshold,100);assert.equal(h.state.imuLab.piezo.front.threshold,650);assert.equal(h.state.imuLab.piezo.revision,2);
  h.state.piezoTrialScale=.1;await page.click('#normalImpactTrial');await page.click('#sensingTabimu');await page.locator('#piezoTabState').filter({hasText:'Recording'}).waitFor();assert.equal(h.state.piezoRunning.kind,'normal');await page.click('#sensingTabpiezo');await page.locator('#impactTrialState').filter({hasText:'Movement recorded'}).waitFor();
  await page.click('#tapImpactTrial');await page.locator('#impactTrialState').filter({hasText:'Trials recorded'}).waitFor();
  await page.locator('#impactReport').filter({hasText:'Clear separation'}).waitFor();
  const before=h.state.imuLab.piezo.front.threshold;await page.click('#useImpactSuggestion');assert.notEqual(Number(await page.inputValue('#frontImpactThreshold')),before);assert.equal(h.state.imuLab.piezo.front.threshold,before);
  const download=page.waitForEvent('download');await page.click('#saveImpactReport');const file=await download,report=JSON.parse(fs.readFileSync(await file.path(),'utf8'));assert.equal(report.format,'smartvest-impact-trial-v1');assert.equal(report.status.taps.front.taps,4);assert(report.comparison.values);
  const bank=h.state.imuLab.piezo.front;bank.lastPeak=100;bank.supportPeak=900;bank.imuMatched=true;
  await page.locator('#frontLastHit').filter({hasText:'Strong hit 900 matched'}).waitFor();
  bank.supportHeadroom=false;await page.locator('#frontImpactState').filter({hasText:'insufficient range for 800-point support'}).waitFor();bank.supportHeadroom=true;bank.imuMatched=false;
  assert.equal(h.state.jobs.length,originalJobs);
  console.log('PASS admin impact panel: no automatic writes, draft retention, confirmed persistence, two trials, bounded comparison, explicit suggestion application, JSON export and no SMS');
  h.state.piezoFailure='Storage verification failed';await page.click('#saveImpactThresholds');await page.locator('#impactActionMessage').filter({hasText:'Storage verification failed'}).waitFor();h.state.piezoFailure=null;
  h.state.imuLab.piezo.fresh=false;await page.locator('#piezoState').filter({hasText:'unavailable'}).waitFor();assert.equal(await page.locator('#saveImpactThresholds').isDisabled(),true);assert.equal(await page.locator('#frontHit').innerText(),'—');h.state.imuLab.piezo.fresh=true;
  await page.locator('#piezoState').filter({hasText:'Listening'}).waitFor();
  h.state.imuLab.piezo.normal.gaps=1;await page.locator('#impactReport').filter({hasText:'interrupted'}).waitFor();assert(await page.locator('#useImpactSuggestion').isDisabled());h.state.imuLab.piezo.normal.gaps=0;
  h.state.imuLab.piezo.taps.front.weakestTap=80;await page.locator('#impactReport').filter({hasText:'overlap'}).waitFor();assert(await page.locator('#useImpactSuggestion').isDisabled());
  h.state.imuLab.piezo.taps.front.weakestTap=900;
  for(const width of [320,390,768,1440]){await page.click('#sensingTabimu');await page.evaluate(()=>scrollTo(0,document.body.scrollHeight));await page.click('#sensingTabpiezo');const top=await page.locator('#piezoPanel').evaluate(el=>el.getBoundingClientRect().top);assert(top>0&&top<600,'Switching tabs reveals content below the board summary: '+top);assert(await page.locator('#boardCommunication').isVisible());await page.setViewportSize({width,height:900});await page.waitForTimeout(70);const d=await page.evaluate(()=>({w:innerWidth,s:document.documentElement.scrollWidth}));assert(d.s<=d.w+1,JSON.stringify({width,...d}));}
  await page.emulateMedia({reducedMotion:'reduce'});assert.equal(await page.locator('#frontImpactFill').evaluate(el=>getComputedStyle(el).transitionDuration),'0s');
  await page.setViewportSize({width:390,height:844});await page.locator('#piezoPanel').screenshot({path:testOutput('previews/piezo-phone.png')});
  console.log('PASS tab keyboard control, retained drafts, background trial badge, scroll reset; stale/fault/overlap guidance, disabled unsafe actions, responsive 320–1440 layouts and reduced motion');
  const rider=await browser.newContext();const rp=await rider.newPage();await rp.goto(base+'/');await rp.fill('#authUser','rider');await rp.fill('#authPass','rider-test-password');await rp.click('#loginSubmit');await rp.locator('#workspace').waitFor({state:'visible'});
  const denied=await rp.evaluate(async()=>{const s=await(await fetch('/api/v1/session')).json();return(await fetch('/api/v1/sensing/piezo',{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':s.csrf},body:JSON.stringify({action:'baseline',revision:1})})).status;});assert.equal(denied,403);await rider.close();
  assert.deepEqual(errors,[]);console.log('PASS rider denial and no browser exceptions');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1)});
