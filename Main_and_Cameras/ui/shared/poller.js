import {api,session} from './api.js';
import {$,toast} from './dom.js';
export const FRESH_MS=7000,STALE_MS=18000;
let current=null;
function labels(text,state){for(const id of ['connection','mobileConnection']){const el=$(id);if(el){if(el.textContent!==text)el.textContent=text;const name='connection '+(id==='mobileConnection'?'mobile-connection ':'')+state;if(el.className!==name)el.className=name;}}}
function updateWriteControls(){document.querySelectorAll('[data-write]').forEach(button=>{button.disabled=!session.connected||session.writing||button.dataset.blocked==='true';});}
export function setConnection(connected,issue=null){
 session.connected=connected;labels(connected?'Connected to Vest':issue?.label||'Reconnecting to Vest',connected?'live':issue?'delayed':'offline');
 $('connectionBanner').hidden=connected||Boolean(issue?.quiet);$('workspace').classList.toggle('stale',!connected);updateWriteControls();
 if(!connected)$('connectionDetail').textContent=issue?.detail||'Reconnecting automatically. Keep this device connected to SmartVest Wi-Fi. Last readings stay visible until a new update arrives.';
}
function updating(value){const el=$('updateIndicator');if(el)el.hidden=!value;}
export function received(){setConnection(true);updating(false);const text='Updated '+new Date().toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit',second:'2-digit'}),node=$('receivedTime');if(node.textContent!==text)node.textContent=text;}
export function startPolling(path,render,{interval=2000,after=null,onAge=null,validate=null,load=api,fixedRate=false}={}){
 stopPolling();let inFlight=null,request=null,followup=null,followupRequest=null,stopped=false,timer=null,failures=0,lastReceived=Date.now(),hasReceived=false,issue=null,retrySoon=false,resumingUntil=0;const generation=session.generation,riderStatus=path.split('?')[0]==='rider/status';
 function live(){return !stopped&&session.logged&&generation===session.generation;}
 function waitingFailed(){
  if(!live()||hasReceived)return;const host=$('pageContent'),loading=host?.querySelector(':scope > .loading');
  if(loading&&host.children.length===1){loading.className='empty';loading.innerHTML='<h2>Connecting to your vest</h2><p>Keep SmartVest Wi-Fi connected. Your readings will appear automatically.</p>';}
  if(!hasReceived)setConnection(false,issue||{label:'Connecting to Vest',detail:'Connecting automatically. Keep this device on SmartVest Wi-Fi.'});
 }
 const firstUpdate=setTimeout(waitingFailed,15000);
 // Internet availability is not local-vest reachability. Successful replies
 // and their age are authoritative, including inside captive-portal browsers.
 function assess(){if(!live()||document.hidden)return;if(Date.now()<resumingUntil)return;const elapsed=Date.now()-lastReceived;if(issue){setConnection(false,issue);updating(false);}else if(elapsed>=STALE_MS){setConnection(false);updating(false);}else if(failures||elapsed>=Math.max(FRESH_MS,interval+2000)){updating(true);}onAge?.(elapsed);if(riderStatus)window.dispatchEvent(new Event('vestage'));}
 async function refresh({afterCurrent=false}={}){
  if(inFlight){await inFlight;if(!afterCurrent)return;if(inFlight)return inFlight;}
  if(!live()||document.hidden)return;
  clearTimeout(timer);const cycleAt=Date.now();request=new AbortController();
  inFlight=(async()=>{let phase='request';retrySoon=false;try{const requestAt=Date.now(),data=await load(path,'GET',undefined,{signal:request.signal,timeoutMs:4500});if(!live()||document.hidden)return;phase='display';if(validate&&!validate(data))throw new Error('Incomplete data received.');render(data,{elapsedMs:Date.now()-requestAt});if(riderStatus)window.dispatchEvent(new CustomEvent('veststatus',{detail:data}));lastReceived=Date.now();resumingUntil=0;hasReceived=true;clearTimeout(firstUpdate);failures=0;issue=null;received();if(after&&!followup){followupRequest=request;followup=Promise.resolve().then(()=>live()?after(data):undefined).catch(()=>{}).finally(()=>{followup=null;followupRequest=null;});}}
   catch(error){if(live()&&error.name!=='AbortError'){failures++;retrySoon=phase==='request'&&error.status===503&&failures<=2;if(!error.offline&&(phase!=='request'||!hasReceived||failures>=3||error.status<500)){const display=phase!=='request';issue={label:display?'Display unavailable':'Updates unavailable',detail:display?'The latest reading could not be displayed. Retrying automatically.':'Your vest is taking longer to reply. Retrying automatically.'};if(session.role==='developer')issue.detail+=(error.status?' HTTP '+error.status+'.':'')+' '+error.message;}else issue=null;waitingFailed();assess();}}
   finally{request=null;if(live()&&!document.hidden)timer=setTimeout(()=>refresh(),retrySoon?250*failures:fixedRate&&!failures?Math.max(50,interval-(Date.now()-cycleAt)):Math.min(6000,interval*2**Math.min(failures,2)));}
  })();try{await inFlight;}finally{inFlight=null;}
 }
 const watchdog=setInterval(assess,1000);
 function resume(){if(document.hidden){clearTimeout(timer);request?.abort();followupRequest?.abort();return;}resumingUntil=Date.now()+FRESH_MS;setConnection(false,{label:'Updating readings',quiet:true});refresh({afterCurrent:true});}
 current={refresh,stop(){stopped=true;request?.abort();followupRequest?.abort();clearTimeout(firstUpdate);clearTimeout(timer);clearInterval(watchdog);document.removeEventListener('visibilitychange',resume);window.removeEventListener('online',resume);window.removeEventListener('offline',resume);}};
 document.addEventListener('visibilitychange',resume);window.addEventListener('online',resume);window.addEventListener('offline',resume);$('refreshPage').disabled=false;$('refreshPage').onclick=async()=>{const button=$('refreshPage');button.disabled=true;try{await refresh({afterCurrent:true});}finally{if(live())button.disabled=false;}};refresh();return current;
}
export function stopPolling(){current?.stop();current=null;}
export async function action(path,body,button,success){
 if(!session.connected||session.writing)return;const generation=session.generation;session.writing=true;updateWriteControls();
 try{const result=await api(path,'POST',body);if(generation===session.generation){toast(success);await current?.refresh({afterCurrent:true});}return result;}
 catch(error){if(generation===session.generation){toast(error.message,true);if(error.offline)updating(true);}throw error;}
 finally{if(generation===session.generation){session.writing=false;updateWriteControls();}}
}
