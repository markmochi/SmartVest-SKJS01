import {api,session} from '../shared/api.js';
import {$,esc,badge,kv,confirmAction,toast,message} from '../shared/dom.js';
import {startPolling,action} from '../shared/poller.js';
import {stateLabel,stateTone,isDiagnosticMessage} from '../shared/history.js';
import {age} from '../shared/dom.js';
export function mountTools(context={isCurrent:()=>true}){let settings=null,contacts=[],status={},settingsLoad=null;
 $('pageContent').innerHTML=`<section id="led-test" class="panel led-test"><div class="panel-head"><div><h2>Vest indicator light</h2><p>Check the physical RGB light · Administrator test</p></div><span class="status neutral">15-second test</span></div><div class="panel-body"><div class="led-overview"><div id="ledPreview" class="led-preview" data-mode="none" role="img" aria-label="No LED test running"><span class="led-lens"></span><span class="led-base"></span></div><div><h3 id="ledPattern">Normal battery indicator</h3><p class="hint">Requested pattern. Check the vest to confirm.</p><p id="ledStatus" role="status">Checking availability…</p><p class="hint">Tests end after 15 seconds, including when you leave this page. Battery and crash monitoring continue.</p></div></div><div class="led-patterns" role="group" aria-label="Choose an indicator light test">${ledModes.map(([mode,title,help])=>`<button type="button" class="secondary led-choice" data-led="${mode}" data-write aria-pressed="false"><span class="led-swatch" data-color="${mode}"></span><span><strong>${title}</strong><small>${help}</small></span></button>`).join('')}</div><div class="actions"><button id="stopLedTest" class="secondary" data-write>Return to battery indicator</button><span id="ledAvailability" class="hint"></span></div><p id="ledMessage" class="form-message" role="status"></p></div></section><div class="split section-gap"><section class="panel"><div class="panel-head"><h2>SMS Test</h2></div><div class="panel-body"><p>Send an introduction or crash SMS to a saved contact. This test does not create an incident or lock cameras.</p><div class="field"><label for="messageRecipient">Saved contact</label><select id="messageRecipient" disabled><option value="">Waiting for saved contacts</option></select></div><div class="actions"><button id="sendIntroduction" data-write>Send introduction</button><button id="sendCrashSms" class="secondary" data-write>Send crash SMS</button></div><p id="contactMessage" class="form-message" role="status"></p><div id="messageStatus" class="section-gap" data-live></div></div><a class="link-row" href="/admin/activity">Open message history</a></section><section class="panel"><div class="panel-head"><h2>Camera pairing</h2></div><div class="panel-body"><p>Use the matching key when configuring each camera.</p><div class="actions"><button id="showKeys" class="secondary">Reveal camera keys</button><button id="hideKeys" class="quiet" hidden>Hide keys</button></div><div id="pairingKeys" class="section-gap" hidden></div><p id="pairingMessage" class="form-message" role="status"></p></div></section></div><section class="panel section-gap"><div class="panel-head"><h2>Live SMS queue</h2><span id="queueSummary" class="small-text" data-live></span></div><div id="queueProgress" class="panel-body" data-live></div></section>`;
 const updateLed=mountLedTests(context);
 function update(data){
  status=data;updateLed(data);
  const g=data.gsm||{},sms=data.sms||{},ready=Boolean(contacts.length&&settings?.rider&&g.enabled);
  for(const id of ['sendIntroduction','sendCrashSms']){$(id).dataset.blocked=String(!ready);$(id).disabled=!session.connected||session.writing||!ready;}
  $('messageStatus').innerHTML=kv([['SIM card',g.simStatus||'Checking SIM card'],['SIM carrier',g.simReady?(g.simCarrier||'Checking'):'Unavailable'],['Network',g.simReady?(g.registrationText||'Checking'):'Not confirmed'],['SMS service',g.simReady&&g.readyToSend?'Ready':'Waiting for service'],['Modem activity',sms.stage||g.stage||'Not reported'],['Last SIM check',age(g.simAgeMs)]]);
  $('queueSummary').textContent=data.sms?(sms.waiting||0)+' waiting · '+(sms.sending||0)+' sending':'Waiting for queue update';
  const rows=(Array.isArray(sms.recent)?sms.recent:[]).filter(row=>!isDiagnosticMessage(row));
  $('queueProgress').innerHTML=rows.length?rows.map(row=>`<article class="history-item"><div class="history-title"><div><h3>Message #${esc(row.id)}</h3><p>${esc(row.phone||'Saved recipient')}</p></div>${badge(stateLabel(row.state),stateTone(row.state))}</div><p>${esc(row.detail||stateLabel(row.state))}</p></article>`).join(''):'<p class="small-text">'+(data.sms?'No messages queued.':'Current queue information is unavailable.')+'</p>';
 }
 update(status);
 $('sendIntroduction').onclick=async()=>{const phone=$('messageRecipient').value;if(!phone||!await confirmAction('Send this contact an introduction?','Recipient: '+phone+'\nThis sends an SMS when service is available. SIM charges may apply.','Queue message'))return;try{await action('messages/introduction',{phone},$('sendIntroduction'),'Message queued. Live progress appears below.');message('contactMessage','Queued for '+phone+'. Watch Live SMS queue below.',true);}catch(error){message('contactMessage',error.message);}};
 $('sendCrashSms').onclick=async()=>{const phone=$('messageRecipient').value;if(!phone||session.writing||!session.connected||!await confirmAction('Send this contact a crash-alert SMS?','Recipient: '+phone+'\nThis sends the normal crash-alert wording, with the current or last known location when available. Let the recipient know you are checking SMS. It does not create an incident or lock cameras. SIM charges may apply.','Queue crash SMS'))return;try{await action('messages/crash',{phone},$('sendCrashSms'),'Crash SMS queued. Live progress appears below.');message('contactMessage','Crash SMS queued for '+phone+'. Watch Live SMS queue below.',true);}catch(error){message('contactMessage',error.message);}};
 function hideKeys(){const keys=$('pairingKeys');if(!keys)return;keys.replaceChildren();keys.hidden=true;$('hideKeys').hidden=true;$('showKeys').textContent='Reveal camera keys';}
 $('hideKeys').onclick=hideKeys;$('showKeys').onclick=async()=>{if(!$('pairingKeys').hidden){hideKeys();return;}const button=$('showKeys');button.disabled=true;try{const data=await api('integrations','GET',undefined,{signal:context.signal});if(!context.isCurrent())return;$('pairingKeys').innerHTML=[['frontKey','Front camera key'],['rearKey','Rear camera key']].map(([key,label])=>`<div class="field"><label for="${key}">${label}</label><textarea id="${key}" readonly rows="2" class="mono" spellcheck="false">${esc(data[key])}</textarea><div class="actions"><button type="button" class="secondary small" data-copy="${key}">Copy key</button></div></div>`).join('');$('pairingKeys').hidden=false;$('hideKeys').hidden=false;button.textContent='Hide camera keys';document.querySelectorAll('[data-copy]').forEach(copy=>{copy.onclick=async()=>{const source=$(copy.dataset.copy);try{if(navigator.clipboard?.writeText){await navigator.clipboard.writeText(source.value);toast('Camera key copied.');return;}}catch{}source.focus();source.select();if(document.execCommand('copy'))toast('Camera key copied.');else toast('Key selected. Use your device\u0027s Copy command.');};});}catch(error){if(context.isCurrent()&&error.name!=='AbortError')message('pairingMessage',error.message);}finally{button.disabled=false;}};
 const visibility=()=>{if(document.hidden)hideKeys();};document.addEventListener('visibilitychange',visibility);window.addEventListener('sessionexpired',hideKeys);context.signal?.addEventListener('abort',()=>{document.removeEventListener('visibilitychange',visibility);window.removeEventListener('sessionexpired',hideKeys);hideKeys();},{once:true});
 function loadSettings(){
  if(settings||settingsLoad||!context.isCurrent())return settingsLoad;
  settingsLoad=(async()=>{try{
   const data=await api('settings','GET',undefined,{signal:context.signal,timeoutMs:4500});if(!context.isCurrent())return;
   settings=data;contacts=Array.isArray(data.contacts)?data.contacts:[];
   $('messageRecipient').innerHTML=contacts.length?contacts.map(c=>`<option value="${esc(c.phone)}">${esc(c.name)} / ${esc(c.phone)}</option>`).join(''):'<option value="">No contacts saved</option>';
   $('messageRecipient').disabled=!contacts.length;message('contactMessage','');update(status);
  }catch(error){if(context.isCurrent()&&error.name!=='AbortError'){
   $('messageRecipient').innerHTML='<option value="">Saved contacts unavailable</option>';message('contactMessage',error.message);
  }}finally{settingsLoad=null;}})();return settingsLoad;
 }
 startPolling('status?view=tools',update,{interval:2000,after:loadSettings});loadSettings();}

const ledModes=[['critical','Blinking red','Battery 0–10% · critically low'],['low','Steady red','Battery above 10%, up to 30% · low'],['full','Steady green','Green channel check · manual pattern only'],['charging','Blinking green','Pattern check only · charging is not measured'],['blue','Steady blue','Blue channel check · no battery meaning'],['off','Light off','Normal above 30%, or no fresh battery reading']];
function mountLedTests(context){
 let power={},enabled=false,deadline=0,confirmed=false,timer=null;
 function show(){
  if(!session.logged||!$('ledPreview'))return;
  const active=Boolean(power.ledTestActive&&deadline>performance.now()),mode=active?power.ledTestMode:'none';
  const title=ledModes.find(row=>row[0]===mode)?.[1]||'Normal battery indicator';
  $('ledPreview').dataset.mode=session.connected?mode:'none';$('ledPreview').setAttribute('aria-label',session.connected?title:'Connection lost; physical light not confirmed');
  $('ledPattern').textContent=title;
  $('ledStatus').textContent=!session.connected?'Connection lost. The vest ends its test automatically.':active?title+' requested · '+Math.ceil((deadline-performance.now())/1000)+' seconds remaining.':power.ledTestActive?'Test timer ended. Waiting for vest confirmation.':confirmed?'Automatic battery indication is restored.':'Choose a pattern, then check the light on your vest.';
  for(const b of document.querySelectorAll('[data-led]')){b.dataset.blocked=String(!enabled);b.disabled=!enabled||!session.connected||session.writing;b.setAttribute('aria-pressed',String(active&&b.dataset.led===mode));}
  const stop=$('stopLedTest');stop.dataset.blocked=String(!power.ledTestSupported||!active);stop.disabled=!power.ledTestSupported||!active||!session.connected||session.writing;
 }
 function update(data){
  power=data.power||{};enabled=Boolean(data.benchTestsEnabled&&power.indicatorEnabled&&power.ledTestSupported);
  deadline=performance.now()+Math.min(15000,Math.max(0,Number(power.ledTestRemainingMs)||0));
  $('ledAvailability').textContent=enabled?'Ready for testing.':!power.ledTestSupported?'This vest does not report light testing.':'Light testing is turned off in this vest\u0027s setup.';
  show();if(power.ledTestActive&&!timer)timer=setInterval(()=>{if(!document.hidden)show();},1000);else if(!power.ledTestActive){clearInterval(timer);timer=null;}
 }
 async function send(mode,button){
  if(!session.connected||session.writing)return;
  message('ledMessage','');try{const reply=await action('power/led-test',{mode},button,mode==='stop'?'Battery indicator restored.':'Light test requested. Check the vest.');if(reply?.power&&context.isCurrent()&&session.logged){confirmed=true;update({power:reply.power,benchTestsEnabled:enabled});}}catch(error){if(context.isCurrent())message('ledMessage',error.message);}
 }
 for(const b of document.querySelectorAll('[data-led]'))b.onclick=()=>send(b.dataset.led,b);
 $('stopLedTest').onclick=()=>send('stop',$('stopLedTest'));
 function dispose(){clearInterval(timer);timer=null;window.removeEventListener('vestsignout',dispose);window.removeEventListener('pagehide',dispose);}
 window.addEventListener('vestsignout',dispose);window.addEventListener('pagehide',dispose);context.signal?.addEventListener('abort',dispose,{once:true});
 return update;
}
