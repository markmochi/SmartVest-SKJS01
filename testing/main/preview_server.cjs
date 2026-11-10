// Local QA harness with simulated API responses; never contacts a physical vest.
const http=require('node:http'),fs=require('node:fs'),path=require('node:path'),zlib=require('node:zlib'),crypto=require('node:crypto');
const fixtures=require('./fixtures.cjs');
const root=path.resolve(__dirname,'../../Main_and_Cameras'),manifest=JSON.parse(fs.readFileSync(path.join(__dirname,'../../Main_and_Cameras/ui/manifest.json'),'utf8')),assets=new Map(manifest.assets.map(asset=>[asset.path,asset]));
function pendingMessages(state,compact=false){
 const items=[];
 for(const row of state.incidents)for(const [recipient,r]of (row.recipients||[]).entries())if(['waiting','queued','retry','sending'].includes(r.state))items.push({boot:row.boot,sequence:row.sequence,recipient,name:r.name,phone:r.phone,state:r.state==='waiting'?'queued':r.state,cancellable:r.cancellable!==false});
 for(const job of state.jobs)if(!job.eventBoot&&['queued','retry','sending'].includes(job.state))items.push({id:job.id,name:'Saved contact',phone:job.phone,state:job.state,cancellable:job.cancellable!==false});
 const cancellable=items.filter(i=>i.cancellable).length,reason=!cancellable&&items.length?'Waiting for a send result from the mobile network':state.status.gsm.readyToSend?'Waiting for the next sending turn':'Waiting for mobile service';return compact?{items:items.filter(i=>!i.boot).map(i=>[i.id,i.phone,i.name||'',i.state,i.cancellable]),incidents:items.filter(i=>i.boot).map(i=>[i.boot,i.sequence,i.recipient,i.cancellable]),cancellable,reason}:{items,cancellable,reason};
}
function createHarness(){const state={status:fixtures.status(),sessions:new Map(),sessionOffsetMs:0,adminUsername:'developer',adminPassword:'developer-test-password',riderUsername:'admin',riderPassword:'admin123',factory:true,riderSetupComplete:true,revision:3,requests:[],offline:false,settings:{rider:'Alex',contacts:[{name:'Jamie',phone:'+639170000001'}],notifyAdded:false,maxContacts:5,settingsAvailable:true},jobs:[{id:14,kind:'crash',state:'accepted',phone:'+639170000001',text:'Demo alert text',detail:'Submitted; carrier delivery not yet reported.',attempts:1,modemReference:7,eventBoot:981,deliveryStatus:-1,test:false},{id:13,kind:'check',state:'failed',phone:'+639170000001',text:'Demo bench message',detail:'Network rejected submission.',attempts:1,modemReference:-1,eventBoot:0,deliveryStatus:-1,test:true}],incidents:[{boot:981,sequence:1,test:false,sensorUptimeMs:64000,triggerTimeKnown:true,message:'Simulated incident alert.',front:'protected_reported',rear:'failed_or_timeout',noRecipients:false,recipients:[{name:'Jamie',phone:'+639170000001',state:'accepted'},{name:'Second contact',phone:'+639170000002',state:'delivered'}]}]};
 const server=http.createServer(async(req,res)=>{const url=new URL(req.url,'http://localhost');const pathname=url.pathname;if(state.offline&&pathname.startsWith('/api/')){req.socket.destroy();return;}const headers={'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','X-Frame-Options':'DENY','Referrer-Policy':'no-referrer','Content-Security-Policy':manifest.contentSecurityPolicy};for(const [key,value]of Object.entries(headers))res.setHeader(key,value);
 function json(value,code=200){res.writeHead(code,{'Content-Type':'application/json'});res.end(JSON.stringify(value));}
 let body={};if(req.method==='POST'){let raw='';for await (const chunk of req)raw+=chunk;try{body=JSON.parse(raw||'{}')}catch{return json({error:'Invalid JSON'},400);}}
 if(pathname.startsWith('/api/')){state.requests.push({path:pathname,query:url.search,method:req.method,body});const token=/SVSID=([^;]+)/.exec(req.headers.cookie||'')?.[1];const sessionNow=Date.now()+state.sessionOffsetMs;let session=state.sessions.get(token);if(session&&sessionNow-session.startedAt>=30*60*1000){state.sessions.delete(token);session=null;}const endpoint=pathname.slice('/api/v1/'.length);if(endpoint==='login'){const developer=body.username===state.adminUsername&&body.password===state.adminPassword;const rider=body.username===state.riderUsername&&(body.password===state.riderPassword||(state.factory&&['admin123','legacy-demo-password'].includes(body.password)));if(!developer&&!rider)return json({error:'Incorrect username or password.'},401);const id=crypto.randomBytes(32).toString('hex');const entry={startedAt:sessionNow,csrf:crypto.randomBytes(32).toString('hex'),role:developer?'developer':'rider',defaultPassword:state.factory,riderSetupComplete:state.riderSetupComplete,minPasswordLength:12};state.sessions.set(id,entry);res.setHeader('Set-Cookie','SVSID='+id+'; HttpOnly; SameSite=Strict; Path=/');return json(entry);}if(!session)return json({error:'Please sign in.'},401);if(req.method==='POST'&&req.headers['x-csrf-token']!==session.csrf)return json({error:'Verification failed'},403);if(!endpoint.startsWith('rider/')&&!['session','logout','messages/cancel','cameras/files'].includes(endpoint)&&session.role!=='developer')return json({error:'This account cannot access that action.'},403);
 if(state.ledDeadline){const ms=Math.max(0,state.ledDeadline-Date.now());Object.assign(state.status.power,{ledTestActive:ms>0,ledTestMode:ms>0?state.status.power.ledTestMode:'none',ledTestRemainingMs:ms});if(!ms)state.ledDeadline=0;}
 if(endpoint==='messages/cancel'){
  if(req.method!=='POST')return json({error:'POST required'},405);
  const all=body.all===true&&Object.keys(body).length===1,byId=Number.isInteger(body.id)&&body.id>0&&Object.keys(body).length===1,byIncident=Number.isInteger(body.boot)&&Number.isInteger(body.sequence)&&Number.isInteger(body.recipient)&&Object.keys(body).length===3;
  if(!all&&!byId&&!byIncident)return json({error:'Choose a pending text.'},400);
  const selected=pendingMessages(state).items.filter(i=>all||(byId&&i.id===body.id)||(byIncident&&i.boot===body.boot&&i.sequence===body.sequence&&i.recipient===body.recipient)),inProgress=selected.filter(i=>!i.cancellable).length;
  if(!all&&!selected.some(i=>i.cancellable))return json({error:'This text has already been handed to the mobile network or is no longer waiting.'},409);
  let cancelled=0;
  for(const item of selected)if(item.cancellable){
   if(item.id){state.jobs.find(j=>j.id===item.id).state='cancelled';}
   else {state.incidents.find(r=>r.boot===item.boot&&r.sequence===item.sequence).recipients[item.recipient].state='cancelled';}
   ++cancelled;
  }
  return json({ok:true,cancelled,notCancelled:0,alreadySending:inProgress,message:inProgress?'Pending texts cancelled. Texts already handed to the mobile network cannot be recalled.':'Pending texts cancelled.'});
 }
 if(endpoint==='power/led-test'){
 if(req.method!=='POST')return json({error:'POST required'},405);if(Object.keys(body).length!==1||typeof body.mode!=='string')return json({error:'Choose one LED test mode.'},400);
 const modes=['full','low','critical','charging','blue','off','stop'];if(!modes.includes(body.mode))return json({error:'Unknown LED test mode.'},400);
 if(body.mode!=='stop'&&(!state.status.benchTestsEnabled||!state.status.power.indicatorEnabled))return json({error:'Bench tests disabled'},403);
 state.ledDeadline=body.mode==='stop'?0:Date.now()+15000;
 Object.assign(state.status.power,{ledTestActive:body.mode!=='stop',ledTestMode:body.mode==='stop'?'none':body.mode,ledTestRemainingMs:body.mode==='stop'?0:15000});
 return json({ok:true,power:state.status.power});
 }
 if(endpoint==='rider/pose'){
 const f=require('./imu-fixtures.cjs');if(!state.imuLab)state.imuLab=f.fixture();const i=state.imuLab.imu;
 if(!state.freezePoseSequence){i.sample.sequence+=20;i.sample.timeUs+=100000;}
 const fresh=i.online&&i.healthy&&(state.poseAgeMs||0)<350,valid=fresh&&i.pose.valid&&(i.calibration.flags&7)===7;
 const result={boot:i.boot,sequence:i.sample.sequence,ageMs:state.poseAgeMs||0,online:i.online,healthy:i.healthy,pose:{valid,sideDeg:valid?i.pose.sideDeg:null,forwardDeg:valid?i.pose.forwardDeg:null},calibration:{flags:i.calibration.flags,operation:i.calibration.operation,result:i.calibration.result,samples:i.calibration.samples,revision:i.calibration.revision||0},command:{token:i.command.token,state:i.command.state,result:i.command.result},detector:{valid:fresh&&Boolean(i.detector.valid),state:i.detector.state,candidates:i.detector.candidates,automaticReady:fresh&&Boolean(i.detector.automaticReady)}};
 if(state.poseDelayMs)setTimeout(()=>json(result),state.poseDelayMs);else json(result);return;
 }
 if(endpoint==='rider/calibration'){
 if(req.method!=='POST')return json({error:'POST required'},405);
 if(!state.imuLab)state.imuLab=require('./imu-fixtures.cjs').fixture();const i=state.imuLab.imu;
 if(Object.keys(body).length!==1||!['upright','forward','neutral','gyro'].includes(body.action))return json({error:'Choose one position setup step.'},400);
 if(!i.online||!i.healthy||(state.poseAgeMs||0)>=350)return json({error:'Position unavailable'},409);
 if(i.calibration.result===1)return json({error:'Your vest is finishing another action.'},409);
 if(body.action==='forward'&&!(i.calibration.flags&8))return json({error:'Save the upright position first.'},409);
 if(body.action==='neutral'&&!(i.calibration.flags&2))return json({error:'Save the upright and forward positions first.'},409);
 return require('./imu-fixtures.cjs').handle(state,'sensing/command',body,json);
 }
 if(endpoint==='sensing/trial')return json({error:'Not found'},404);
 if(endpoint.startsWith('sensing/'))return require('./imu-fixtures.cjs').handle(state,endpoint,body,(data,status)=>{
 if(endpoint==='sensing/imu'&&data.imu){const i=data.imu,ageMs=state.poseAgeMs||0,valid=i.online&&i.healthy&&ageMs<=350&&i.pose.valid&&(i.calibration.flags&7)===7;
 data={...data,rider:{boot:i.boot,sequence:i.sample.sequence,ageMs,online:i.online,healthy:i.healthy,pose:{valid,sideDeg:valid?i.pose.sideDeg:null,forwardDeg:valid?i.pose.forwardDeg:null},calibration:i.calibration,command:i.command,detector:{...i.detector,valid,automaticReady:valid&&Boolean(i.detector.automaticReady)}}};}
 return json(data,status);
 });
 if(endpoint==='session')return json({...session,riderSetupComplete:state.riderSetupComplete});if(endpoint==='logout'){state.sessions.delete(token);res.setHeader('Set-Cookie','SVSID=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0');return json({ok:true});}
 if(endpoint==='cameras/files')return require('./camera-files-fixtures.cjs').handle(state,req,url,body,json);
 if(endpoint==='cameras/monitor')return json({download:state.fileLibrary?.download||{state:0},...require('./diagnostic-fixtures.cjs').camera(state.cameraMonitor||{schema:1,mainUptimeMs:125000,logCapacity:48,logOverwritten:0,clock:{source:'unsynced',utcMs:null,offsetMinutes:480},retention:{normalClips:30,segmentSeconds:60,preClips:5,postClips:5},cameras:state.status.cameras,logs:[]},url.searchParams)});if(endpoint==='cameras/clock'){state.clockWrites=(state.clockWrites||0)+1;if(state.cameraMonitor)state.cameraMonitor.clock={source:'device',utcMs:body.utcMs,offsetMinutes:480};return json({ok:true});}if(endpoint==='cameras/test')return json({ok:true,boot:881,sequence:3},202);if(endpoint==='status'){const rows=state.jobs.slice().sort((a,b)=>b.id-a.id);const response={...state.status,sms:{waiting:rows.filter(j=>['queued','retry'].includes(j.state)).length,sending:rows.filter(j=>j.state==='sending').length,stage:state.status.gsm.stage,recent:rows.slice(0,5).map(({id,kind,phone,state,detail,attempts,test,text})=>({id,kind,phone,state,detail,attempts,test,text}))}};const view=url.searchParams.get('view');if(view){
 const sections={overview:['gsm','gps','power','network','sensing','cameras'],ride:['gsm','gps','power','network','sensing','cameras'],tools:['gsm','sms','power'],gsm:['gsm','sms'],gps:['gps','serialMonitor'],power:['power'],network:['network'],system:['http','radio','serialMonitor','emergency']};
 if(!sections[view])return json({error:'Unknown status view'},400);
 for(const key of ['gsm','sms','gps','power','network','sensing','cameras','http','radio','serialMonitor','emergency'])if(!sections[view].includes(key))delete response[key];
 if(view==='overview')response.gsm=Object.fromEntries(['enabled','online','simReady','readyToSend','simStatus','registrationText','registration','rxAgeMs'].map(key=>[key,response.gsm[key]]));
 if(view==='tools'){
  response.gsm=Object.fromEntries(['enabled','online','simReady','readyToSend','simStatus','registrationText','registration','rxAgeMs','simCarrier','simAgeMs'].map(key=>[key,response.gsm[key]]));
  response.power=Object.fromEntries(['ledTestSupported','ledTestActive','ledTestMode','ledTestRemainingMs','indicatorState','indicatorEnabled','indicatorThresholdsVerified'].map(key=>[key,response.power[key]]));
 }
 if(view==='system')response.emergency={loaded:response.emergency?.loaded,writable:response.emergency?.writable,error:response.emergency?.error};
 }return json(response);} if(endpoint==='rider/status'){
 const response={...fixtures.rider(state.status),settingsRevision:state.revision,contacts:state.settings.contacts,activity:{loaded:true,writable:true,incidents:state.incidents},messageQueue:pendingMessages(state,true)},view=url.searchParams.get('view');
 if(view){
  if(!['overview','equipment','activity'].includes(view))return json({error:'Unknown rider status view'},400);
  delete response.alerts;if(view!=='activity'){delete response.activity;delete response.messageQueue;}
  if(view!=='overview')delete response.contacts;
  if(view!=='equipment')delete response.cameras;
  if(view==='overview')response.cellular={available:response.cellular?.available};
  if(view==='activity'){delete response.location;delete response.cellular;response.monitoring={};}
 }
 return json(response);
} 
 if(endpoint==='rider/setup'){if(session.role!=='rider')return json({error:'Use rider setup'},403);if(body.revision!==state.revision)return json({error:'Details changed. Review contacts.'},409);if(!state.settings.contacts.length)return json({error:'Add an emergency contact'},400);state.riderSetupComplete=true;state.revision++;return json({ok:true});}
 if(['settings','rider/settings'].includes(endpoint)){if(req.method==='GET')return json({...state.settings,riderSetupComplete:state.riderSetupComplete,revision:state.revision});if(body.revision!==state.revision)return json({error:'Settings changed elsewhere. Restore saved settings.'},409);state.settings={...state.settings,rider:body.rider,contacts:body.contacts,notifyAdded:body.notifyAdded};state.revision++;return json({ok:true,revision:state.revision,notificationsQueued:body.notifyAdded?body.contacts.length:0});}
 if(endpoint==='rider/account'){if(session.role!=='rider')return json({error:'Use your own account page.'},403);if(req.method==='GET')return json({username:state.riderUsername,defaultPassword:state.factory,minPasswordLength:12});if(body.current!==state.riderPassword&&!(state.factory&&['admin123','legacy-demo-password'].includes(body.current)))return json({error:'Current rider password is incorrect.'},403);if(body.password&&body.password.length<12)return json({error:'Use a private password of 12–64 characters.'},400);state.riderUsername=body.username;state.riderPassword=body.password||state.riderPassword;if(body.password)state.factory=false;for(const [key,value]of state.sessions)if(value.role==='rider')state.sessions.delete(key);res.setHeader('Set-Cookie','SVSID=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0');return json({ok:true});}
 if(endpoint==='admin/account'){
  if(req.method==='GET')return json({username:state.adminUsername,minPasswordLength:12,resetPending:Boolean(state.resetPending)});
  if(body.current!==state.adminPassword)return json({error:'Your current password is incorrect.'},403);
  if(body.username===state.riderUsername||body.username==='admin')return json({error:'Username is reserved.'},400);
  state.adminUsername=body.username;state.adminPassword=body.password||state.adminPassword;
  for(const [key,value]of state.sessions)if(value.role==='developer')state.sessions.delete(key);
  res.setHeader('Set-Cookie','SVSID=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0');return json({ok:true});
 }
 if(endpoint==='admin/reset-data'){
  if(body.current!==state.adminPassword)return json({error:'Your current password is incorrect.'},403);
  if(body.confirm!=='RESET')return json({error:'Type RESET.'},400);
  if(pendingMessages(state).items.length||state.resetBlocked)return json({error:'Finish pending alerts and camera protection, then retry.'},409);
  state.riderSetupComplete=false;state.settings={...state.settings,rider:'',contacts:[],notifyAdded:true};state.revision++;state.jobs=[];state.incidents=[];
  state.riderUsername='admin';state.riderPassword='admin123';state.factory=true;
  if(state.cameraMonitor)state.cameraMonitor.logs=[];
  for(const [key,value]of state.sessions)if(value.role==='rider')state.sessions.delete(key);
  state.resetPending=Boolean(state.resetDeferred);return json({ok:!state.resetPending,resetPending:state.resetPending,riderUsername:'admin'},state.resetPending?202:200);
 }
 if(endpoint==='jobs')return json({jobs:state.jobs,messageQueue:pendingMessages(state)});if(endpoint==='jobs/action'){if(body.action==='clear_history')state.jobs=state.jobs.filter(j=>['queued','sending','retry'].includes(j.state));else {const job=state.jobs.find(j=>j.id===body.id);if(job)job.state='cancelled';}return json({ok:true,cleared:2});}
 if(endpoint==='incidents')return json({loaded:true,writable:true,error:'',incidents:state.incidents,messageQueue:pendingMessages(state,true)});if(endpoint==='integrations')return json({frontKey:'a'.repeat(64),rearKey:'b'.repeat(64)});if(endpoint==='gsm/trace')return json(require('./diagnostic-fixtures.cjs').trace(state.traceMonitor||{entries:[{atMs:120000,direction:'RX',text:'+CREG: 1,1'}],lastFailure:null},url.searchParams));
 if(['messages/check','messages/introduction','messages/crash'].includes(endpoint)){const id=Math.max(0,...state.jobs.map(j=>j.id))+1;state.jobs.push({id,kind:endpoint==='messages/check'?'check':endpoint==='messages/crash'?'manual_crash':'addition',phone:body.phone,state:'queued',detail:'Waiting for cellular service.',attempts:0,text:endpoint==='messages/check'?'SmartVest test\nNo action needed.':endpoint==='messages/crash'?'SmartVest CRASH ALERT\nRider: '+state.settings.rider+'\nCall their phone now. No answer? Call 911.\nLocation unavailable.':"Hello from "+state.settings.rider+"'s SmartVest!\nThey added you as an emergency contact. Please save this number as SmartVest for future alerts."});return json({ok:true,jobId:id,state:'queued'},202);}if(['crash/test','storage/retry'].includes(endpoint))return json({ok:true,boot:881,sequence:2},endpoint==='crash/test'?202:200);return json({error:'Unknown test endpoint'},404);}
 if(req.method!=='GET'&&req.method!=='HEAD'){res.writeHead(405);return res.end('Method not allowed');}
 const asset=assets.get((pathname==='/admin/'?'/admin':pathname).replace(/(\/assets\/[^/]+\/)_r\/\d{1,6}\//,'$1'));let file;if(asset){file=path.resolve(root,asset.servedSource);if(!file.startsWith(root+path.sep)){res.writeHead(403);return res.end();}}if(!file||!fs.existsSync(file)){res.writeHead(404);return res.end('Not found');}
 const mime={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.svg':'image/svg+xml'}[path.extname(file)];const packed=zlib.gzipSync(fs.readFileSync(file));res.writeHead(200,{'Content-Type':mime,'Content-Encoding':'gzip','Content-Length':packed.length});res.end(req.method==='HEAD'?undefined:packed);
 });return {state,server,async start(port=0){await new Promise(resolve=>server.listen(port,'127.0.0.1',resolve));return 'http://127.0.0.1:'+server.address().port;},async close(){await new Promise(resolve=>server.close(resolve));}};}
module.exports={createHarness};if(require.main===module){const harness=createHarness();harness.start(Number(process.argv[2]||8090)).then(url=>console.log('Simulated SmartVest preview:',url));}
