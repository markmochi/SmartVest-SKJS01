import {api,session} from './api.js';
import {createRiderScene} from './rider-scene.js';

const STEPS=[
 {action:'upright',operation:3,title:'Sit upright',detail:'Wear the vest normally. Sit upright with level shoulders.',button:'Save upright position'},
 {action:'forward',operation:4,title:'Lean forward',detail:'Lean forward 20–30° at your hips. Keep your shoulders level and hold still.',button:'Save forward lean'},
 {action:'neutral',operation:5,title:'Your normal riding position',detail:'Hold your normal, relaxed riding position.',button:'Save normal position'}
];
const CAPTURE_ERRORS={3:'You moved during the reading. Settle into the position and try again.',4:'The reading was interrupted. Keep still and try again.',5:'Your vest lost its movement sensor. Check the equipment connection.',6:'The reading took too long. Keep the vest still and try again.',7:'Lean forward a little more or less, about 20–30°, then try again.',8:'Your vest could not save this position. Try again before riding.',9:'The reading was cancelled. Your earlier saved settings have not been replaced by this reading.'};
const COMMAND_ERRORS={1:'Another reading or recording is in progress. Let it finish first.',2:'The movement sensor is not connected. Check the equipment connection.',3:'Save the earlier position first.',4:'This reading could not be started. Try again.',5:'Your vest restarted. Wait for live readings before trying again.',6:'This option is unavailable on the connected vest.',7:'A recording changed. Wait for it to finish before trying again.',8:'Your vest could not save this position. Try again before riding.'};
const finite=value=>typeof value==='number'&&Number.isFinite(value);
const shortest=value=>((value+180)%360+360)%360-180;
const clamp=(value,min,max)=>Math.max(min,Math.min(max,value));

export function riderPositionMarkup({admin=false}={}){
 return `<section id="riderPosition" class="rp" data-live-owned aria-labelledby="rpTitle">
  <header class="rp-header"><div><h2 id="rpTitle">Your riding position</h2><p class="rp-intro">Front and side views of how your upper body is leaning.</p></div><span class="rp-live" data-rp-live>Connecting</span></header>
  <div class="rp-studio" data-rp-studio data-rp-state="waiting"><div class="rp-view-labels" aria-hidden="true"><span>FRONT VIEW <small>Facing the rider</small></span><span>SIDE VIEW <small>Facing forward →</small></span></div><canvas data-rp-canvas role="img" aria-label="Three-dimensional motorcycle rider, shown from the front and side"></canvas><div class="rp-unavailable" data-rp-unavailable><strong data-rp-unavailable-title>Waiting for your position</strong><span data-rp-unavailable-help>Live readings will appear when your vest is ready.</span></div><div class="rp-studio-caption" aria-hidden="true"><span>RIDER\u0027S RIGHT <b>·</b> RIDER\u0027S LEFT</span><span>BACK <b>·</b> FORWARD</span></div></div>
  <div class="rp-readings"><div class="rp-reading"><span class="rp-reading-name">Sideways lean</span><div><strong data-rp-side>—</strong><span data-rp-side-direction>Waiting for a reading</span></div><div class="rp-meter" aria-hidden="true"><i></i><b data-rp-side-marker></b></div></div><div class="rp-reading"><span class="rp-reading-name">Forward / back lean</span><div><strong data-rp-forward>—</strong><span data-rp-forward-direction>Waiting for a reading</span></div><div class="rp-meter" aria-hidden="true"><i></i><b data-rp-forward-marker></b></div></div></div>
  <div class="rp-condition" data-rp-condition><span class="rp-condition-dot" aria-hidden="true"></span><div><strong data-rp-condition-title>Waiting for live readings</strong><span data-rp-condition-detail>Keep your vest connected to see your position.</span></div><span class="rp-detection" data-rp-detection>Checking alerts</span></div>
  <p class="rp-estimate" data-rp-estimate>Estimated from your vest. Upper-body movement is measured; motorcycle lean is illustrative.${admin?' This view matches the rider\u0027s overview.':''}</p>
  <div class="rp-calibration"><div class="rp-calibration-heading"><div><h3>Make it match your riding position</h3><p data-rp-saved>Checking your saved positions…</p></div><button class="secondary" type="button" data-rp-adjust aria-expanded="false" aria-controls="rpCalibrationSteps">Set riding position</button></div>
   <div id="rpCalibrationSteps" data-rp-setup class="rp-setup" hidden><p class="rp-setup-note">Set up away from traffic while stationary, with your usual vest fit. Hold each position for two seconds. Complete all three steps before riding. Crash monitoring may be unavailable during setup.</p><ol class="rp-steps" aria-label="Position setup steps">${STEPS.map((step,index)=>`<li data-rp-step="${index}"><span>${index+1}</span><b>${['Upright','Forward lean','Normal position'][index]}</b></li>`).join('')}</ol><div class="rp-step-content"><div><span class="rp-step-count" data-rp-step-count>STEP 1 OF 3</span><h3 data-rp-step-title>${STEPS[0].title}</h3><p data-rp-step-detail>${STEPS[0].detail}</p></div><div class="rp-capture-control"><button type="button" data-rp-capture>${STEPS[0].button}</button><button type="button" class="secondary" data-rp-cancel>Close setup</button></div></div><div class="rp-capture-progress" data-rp-progress-wrap hidden><progress data-rp-progress max="240" value="0" aria-label="Still position reading"></progress><span data-rp-progress-label>Keep still…</span></div><p class="rp-message" data-rp-message role="status" aria-live="polite"></p><p class="rp-save-note">Settings survive a restart. Repeat setup after moving or replacing the sensor, changing its mounting, or noticing drift.</p><details class="rp-extra"><summary>Position drifting while you are still?</summary><p>Keep the vest still. Refreshing this reading corrects gradual drift while retaining your saved riding position.</p><button type="button" class="secondary" data-rp-gyro>Refresh stillness reading</button></details></div>
  </div>
 </section>`;
}

export function mountRiderPosition({external=false,refresh=null}={}){
 const root=document.getElementById('riderPosition');
 if(!root)return {dispose(){},update(){}};
 const el=name=>root.querySelector(`[data-rp-${name}]`),generation=session.generation;
 const scene=createRiderScene(el('canvas')),estimateText=el('estimate').textContent;
 const motion=window.matchMedia('(prefers-reduced-motion: reduce)');
 let disposed=false,timer=0,frame=0,request=null,inFlight=false,failures=0,lastPacket=null,lastKey=null,lastNew=0,lastPose=0,lastFrame=0,lastText=0,latest=null;
 let target=null,shown=null,freshBefore=false,setup=false,setupDone=false,step=0,pending=null,posting=false,message='',drawDirty=true,onScreen=true;
 const resizeObserver=new ResizeObserver(()=>{root.dataset.layout=el('canvas').getBoundingClientRect().width<560?'stacked':'columns';drawDirty=true;});resizeObserver.observe(el('canvas'));
 const viewObserver=typeof IntersectionObserver==='function'?new IntersectionObserver(entries=>{
  const visible=entries[0].isIntersecting;if(visible===onScreen)return;onScreen=visible;drawDirty=true;
  if(live()&&!document.hidden&&!inFlight){clearTimeout(timer);if(visible)poll();else timer=setTimeout(poll,pollingDelay());}
 }):null;viewObserver?.observe(el('studio'));
 const live=()=>!disposed&&root.isConnected&&session.logged&&session.generation===generation;
 const set=(name,text)=>{const node=el(name);if(node.textContent!==text)node.textContent=text;};
 function sourceFresh(now){return Boolean(lastPacket&&lastPacket.online&&lastPacket.healthy&&now-lastNew<=650);}
 function freshness(now){return Boolean(lastPacket?.online&&lastPacket.healthy&&lastPacket.pose?.valid&&target&&now-lastPose<=1600);}
 // Brief transport gaps retain the last pose without claiming it is current.
 function holding(now){return Boolean(target&&shown&&lastPacket?.online&&lastPacket.healthy&&lastPacket.pose?.valid&&now-lastPose<=2200);}
 function tell(text,tone=''){message=text;set('message',text);el('message').dataset.tone=tone;}
 function savedText(){const c=latest?.calibration||{},ready=(c.flags&7)===7;set('saved',ready?'Your riding position is saved on the vest.':'Set three still positions so the view matches you.');set('adjust',ready?'Adjust position':'Set riding position');}
 function controls(){
  const busy=Boolean(pending||posting),connected=sourceFresh(performance.now())&&session.connected;
  el('adjust').disabled=busy;el('cancel').disabled=busy;
  el('capture').disabled=busy||setupDone||!connected;
  el('gyro').disabled=busy||!connected;
  el('progress-wrap').hidden=!busy;
  if(!busy&&!connected&&!message&&setup)tell('Wait for a fresh movement reading before saving a position.');
 }
 function showStep(){
  const current=STEPS[step];set('step-count',`STEP ${step+1} OF 3`);set('step-title',current.title);set('step-detail',current.detail);set('capture',current.button);
  root.querySelectorAll('[data-rp-step]').forEach((node,index)=>{node.dataset.state=index<step?'done':index===step?'current':'waiting';if(index===step)node.setAttribute('aria-current','step');else node.removeAttribute('aria-current');});
  controls();
 }
 function closeSetup(){if(pending||posting)return;setup=false;el('setup').hidden=true;el('adjust').setAttribute('aria-expanded','false');el('adjust').focus();}
 // The backend ACK confirms that a capture started. Only its new saved revision confirms completion.
 function watchCapture(data){
  if(!pending)return;
  const command=data.command||{},cal=data.calibration||{};
  if(data.boot!==pending.boot){pending=null;tell('Your vest restarted. Wait for live readings, then start the position setup again.','error');controls();return;}
  if(command.token===pending.token){
   if(command.state===3||command.state===4){pending=null;tell(command.state===4?'Your vest did not confirm the request. Wait for an update before trying again.':COMMAND_ERRORS[command.result]||'This reading could not be started. Try again.','error');controls();return;}
   if(command.state===2)pending.accepted=true;
  }
  if(cal.operation===pending.operation){
   if(cal.result===1){pending.collecting=true;el('progress').value=clamp(cal.samples||0,0,240);set('progress-label','Keep still while your position is saved…');tell('Reading your position. Keep still.');}
   if(pending.accepted&&cal.result===2&&cal.revision!==pending.revision){
    const action=pending.action;pending=null;savedText();
    if(action==='gyro'){tell('Stillness reading saved. Your normal riding position has been kept.','success');}
    else if(step<2){step++;showStep();tell('Position saved. Move into the next position when you are ready.','success');el('capture').focus();}
    else {setupDone=true;tell('All three positions are saved. Your riding view is ready.','success');set('capture','Setup complete');el('cancel').textContent='Done';root.querySelectorAll('[data-rp-step]').forEach(node=>node.dataset.state='done');}
    controls();if(action==='neutral')el('capture').disabled=true;return;
   }
   if(pending.accepted&&pending.collecting&&cal.result>=3){pending=null;tell(CAPTURE_ERRORS[cal.result]||'This position could not be saved. Keep still and try again.','error');controls();return;}
  }
  if(performance.now()>pending.deadline){pending=null;tell('Could not confirm that this position was saved. Check your connection and the latest reading before trying again.','error');controls();}
 }
 function update(data){
  if(!live()||!data||typeof data!=='object')return;
  const now=performance.now(),pose=data.pose||{},wasVisible=freshness(now)||holding(now);
  if(lastPacket&&data.boot!==lastPacket.boot){target=null;shown=null;lastKey=null;freshBefore=false;}
  latest=data;lastPacket=data;
  const key=String(data.boot)+':'+String(data.sequence);
  if(key!==lastKey){
   lastKey=key;lastNew=now-clamp(finite(data.ageMs)?data.ageMs:10000,0,10000);
   if(pose.valid&&now-lastNew<=650&&finite(pose.sideDeg)&&finite(pose.forwardDeg)){
    lastPose=lastNew;
    const next={side:shortest(pose.sideDeg),forward:shortest(pose.forwardDeg)};
    if(!target||Math.abs(shortest(next.side-target.side))>.12||Math.abs(shortest(next.forward-target.forward))>.12){target=next;drawDirty=true;}
    if(!shown||!wasVisible){shown={...next};drawDirty=true;}
   }
  }
  savedText();watchCapture(data);controls();condition(freshness(now));
 }
 function condition(fresh){
  const d=latest?.detector||{},cal=latest?.calibration||{};
  const held=!fresh&&holding(performance.now());let title,detail,tone='waiting';
  if(held){title='Updating position';detail='Showing the last position while waiting for the next reading.';}
  else if(!fresh){title=lastNew?'Live position paused':'Waiting for your position';detail=!latest?.online?'Waiting for the vest\u0027s movement sensor.':!latest?.healthy?'Check your equipment connection.':(cal.flags&7)!==7?'Set up your three positions below.':'Waiting for a fresh reading. The view is held still.';}
  else if(d.valid&&d.state===4){title='Crash-like motion detected';detail='Check the incident status for alerts and recordings.';tone='danger';}
  else if(d.valid&&d.state===5){title='Recovering from strong movement';detail='Your vest is checking that movement has settled.';tone='warning';}
  else if(d.valid&&d.state===3){title='Checking unusual movement';detail='Your vest is watching the movement before deciding.';tone='warning';}
  else if(Math.max(Math.abs(target.side),Math.abs(target.forward))>=60){title='Strong lean';detail='Your vest is watching the movement. Lean alone does not confirm a crash.';tone='warning';}
  else {title='Following your position';detail='Sideways and forward lean are relative to your saved riding position.';tone='live';}
  root.dataset.tone=tone;set('condition-title',title);set('condition-detail',detail);
  set('detection',fresh?(d.automaticReady?'Crash monitoring ready':'Crash monitoring waiting'):'Monitoring status unavailable');
  el('detection').dataset.ready=String(fresh&&Boolean(d.automaticReady));
  set('live',fresh?'Live position':held?'Updating position':lastNew?'View paused':'Connecting');el('live').dataset.live=String(fresh);
  el('unavailable').hidden=fresh||held;
  if(!fresh&&!held){set('unavailable-title',scene.supported===false?'3D view unavailable in this browser':title);set('unavailable-help',scene.supported===false?'Live angle readings remain available below.':detail);}
  if(scene.supported===false||el('canvas').dataset.sceneUnavailable==='true'){el('unavailable').hidden=false;set('unavailable-title','3D view unavailable in this browser');set('unavailable-help','Live angle readings remain available below.');}
  el('studio').dataset.rpState=fresh?'live':held?'holding':'waiting';
  set('estimate',estimateText+(fresh&&(Math.abs(target.side)>80||Math.abs(target.forward)>50)?' At extreme angles, the illustration is limited; use the angle readings.':''));
 }
 function readings(fresh){
  for(const [name,key,positive,negative] of [['side','side','To your right','To your left'],['forward','forward','Leaning forward','Leaning back']]){
   const value=shown?.[key]||0,zero=Math.abs(value)<.6;
   set(name,fresh?`${Math.round(Math.abs(value))}°`:'—');set(name+'-direction',fresh?(zero?'At your normal position':value>0?positive:negative):'Waiting for a reading');
   el(name+'-marker').style.left=`${50+clamp(value,-90,90)/90*44}%`;
  }
 }
 function animate(now){
  if(!live()){dispose();return;}if(document.hidden){frame=0;return;}
  const fresh=freshness(now),dt=lastFrame?Math.min(now-lastFrame,80):16;lastFrame=now;
  if(fresh&&shown&&target){
   const distance=Math.max(Math.abs(shortest(target.side-shown.side)),Math.abs(shortest(target.forward-shown.forward)));
   const tau=motion.matches?0:distance>12?24:distance>2?42:68,alpha=tau?1-Math.exp(-dt/tau):1;
   if(distance>.012){shown.side=shortest(shown.side+shortest(target.side-shown.side)*alpha);shown.forward=shortest(shown.forward+shortest(target.forward-shown.forward)*alpha);drawDirty=true;}
   if(drawDirty)scene.setPose(shown.side,shown.forward);
  }
  if(now-lastText>=100||fresh!==freshBefore){condition(fresh);readings(fresh||holding(now));controls();lastText=now;}
  if(fresh!==freshBefore)drawDirty=true;freshBefore=fresh;
  if(drawDirty&&onScreen){scene.render();drawDirty=false;}
  frame=requestAnimationFrame(animate);
 }
 function pollingDelay(){return Math.min(2400,(onScreen||setup||pending||posting?100:800)*2**Math.min(failures,5));}
 async function poll(){
  if(!live()){dispose();return;}if(document.hidden||inFlight)return;
  if(external){refresh?.();return;}
  clearTimeout(timer);request=new AbortController();inFlight=true;const started=performance.now();
  try{const data=await api('rider/pose','GET',undefined,{signal:request.signal,timeoutMs:1500,priority:'background'});if(!live()||document.hidden)return;failures=0;data.ageMs=(finite(data.ageMs)?Math.max(0,data.ageMs):10000)+performance.now()-started;update(data);}
  catch(error){if(live()&&!document.hidden&&error.name!=='AbortError'){failures++;if(pending&&performance.now()>pending.deadline){pending=null;tell('The connection was interrupted. Check the latest reading before trying again.','error');controls();}}}
  finally{inFlight=false;request=null;if(live()&&!document.hidden)timer=setTimeout(poll,failures?pollingDelay():Math.max(50,pollingDelay()-(performance.now()-started)));}
 }
 async function capture(action){
  if(!live()||pending||posting||!sourceFresh(performance.now()))return;
  const chosen=action==='gyro'?{action,operation:2}:STEPS[step],before=latest.calibration||{};
  posting=true;tell('Sending your position request…');controls();el('progress').value=0;set('progress-label','Keep your position still…');
  const boot=latest.boot,revision=before.revision;
  try{
   const result=await api('rider/calibration','POST',{action:chosen.action});if(!live())return;
   if(!result.token)throw new Error('Your vest did not confirm the position request.');
   pending={action:chosen.action,operation:chosen.operation,token:result.token,boot,revision,accepted:false,collecting:false,deadline:performance.now()+9000};tell('Waiting for your vest to start. Keep still.');
  }catch(error){if(live())tell(error.offline?'Your vest did not respond. Check its Wi-Fi connection before trying again.':error.status===409?error.message:error.status===503?'Wait for a fresh movement reading before trying again.':'This position could not be saved. Check your connection and try again.','error');}
  finally{posting=false;if(live()){controls();if(!inFlight){clearTimeout(timer);poll();}}}
 }
 el('adjust').onclick=()=>{setup=!setup;el('setup').hidden=!setup;el('adjust').setAttribute('aria-expanded',String(setup));if(setup){step=0;setupDone=false;el('cancel').textContent='Close setup';tell('');showStep();if(!inFlight){clearTimeout(timer);poll();}}};
 el('cancel').onclick=closeSetup;el('capture').onclick=()=>capture(STEPS[step].action);el('gyro').onclick=()=>capture('gyro');
 function visibility(){clearTimeout(timer);lastFrame=0;if(document.hidden){request?.abort();cancelAnimationFrame(frame);frame=0;}else if(live()){drawDirty=true;frame=requestAnimationFrame(animate);poll();}}
 function dispose(){if(disposed)return;disposed=true;clearTimeout(timer);cancelAnimationFrame(frame);request?.abort();resizeObserver.disconnect();viewObserver?.disconnect();document.removeEventListener('visibilitychange',visibility);window.removeEventListener('vestsignout',dispose);window.removeEventListener('pagehide',dispose);scene.dispose();}
 document.addEventListener('visibilitychange',visibility);window.addEventListener('vestsignout',dispose);window.addEventListener('pagehide',dispose);
 showStep();if(!document.hidden){frame=requestAnimationFrame(animate);poll();}
 return {dispose,update};
}
