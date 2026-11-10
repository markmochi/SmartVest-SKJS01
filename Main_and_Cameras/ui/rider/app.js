import {riderPositionMarkup,mountRiderPosition} from '../shared/rider-position.js';
import {$,esc,known,age,badge,kv,mapLink,renderContent,showError} from '../shared/dom.js';
import {icon} from '../shared/icons.js';
import {startPolling,FRESH_MS,STALE_MS} from '../shared/poller.js';
import {mountBattery} from './battery.js';
export {batteryAge} from './battery.js';
import {incidentRecords} from '../shared/incidents.js';
import {messageQueue,bindMessageQueue} from '../shared/message-queue.js';

// Independent snapshots. Missing fields are not hardware failures; explicit
// false/null values are authoritative and must never be smoothed away.
export function snapshots(){
 const cache=new Map();
 return {
  put(key,value,valid){
   if(!valid)return;
   const last=cache.get(key);
   if(key==='battery'&&last&&known(last.data.percent)&&!known(value?.percent)){
    last.data={...last.data,current:false};return;
   }
   cache.set(key,{data:value,at:Date.now()});
  },
  get(key){const item=cache.get(key),elapsed=item?Math.max(0,Date.now()-item.at):Infinity;
   return {data:item?.data||{},elapsed,phase:!item?'loading':elapsed>=STALE_MS?'unconfirmed':elapsed>=FRESH_MS?'updating':'current'};}
 };
}
const bool=v=>typeof v==='boolean';
function state(s,good,label,negative='Unavailable'){
 if(s.phase==='loading')return badge('Checking…');
 // Preserve a previously reported failure, including during a lost connection.
 if(good===false)return badge(negative,'warn');
 if(s.phase==='unconfirmed')return badge('Status unavailable','neutral');
 return badge(good?label:'Checking…',good?'good':'neutral');
}
function freshness(s){return s.phase==='updating'?'<span class="component-update">Updating…</span>':s.phase==='unconfirmed'?'<p class="hint">Last update '+esc(age(s.elapsed).toLowerCase())+'. Current status is not confirmed.</p>':'';}
function card(key,title,s,body,status=''){return `<section class="panel equipment-card" data-key="${key}" data-component="${key}"><div class="panel-head"><h2>${title}</h2>${status}</div><div class="panel-body">${body}${freshness(s)}</div></section>`;}
function row(symbol,title,s,good,positive='Available',negative='Unavailable',note=''){
 return `<li data-key="${symbol}"><span class="check-icon">${icon(symbol)}</span><div><h3>${esc(title)}</h3>${note?`<p>${esc(note)}</p>`:''}${freshness(s)}</div>${state(s,good,positive,negative)}</li>`;
}
function mobile(s){
 const c=s.data,ready=c.available===true,signal=known(c.signalCsq)&&c.signalCsq>=0&&c.signalCsq<=31?(c.signalCsq>=20?'Strong':c.signalCsq>=10?'Fair':'Weak'):'Not available';
 const sim=c.simReady?'Ready':/PUK/i.test(c.simStatus||'')?'SIM needs a PUK code':/PIN/i.test(c.simStatus||'')?'SIM needs a PIN code':c.simDetected===false?'SIM card not detected':'Needs attention';
 const help=s.phase==='loading'?'Checking mobile service.':s.phase==='unconfirmed'&&ready?'Mobile service was available at the last update. Current service is not confirmed.':c.enabled===false?'Text alerts are unavailable. Have your vest checked before riding.':c.simReady===false?'Check that the vest\u0027s SIM card is installed and active.':!ready?'Text alerts are unavailable. Check mobile coverage and your SIM service.':'Your vest can send alerts to your saved contacts.';
 return card('cellular','Mobile service',s,`<p class="small-text">${help}</p>${s.phase!=='loading'?kv([['SIM card',sim],['Network',c.registered?(c.carrier||'Connected'):'Unavailable'],['Phone number',c.simReady?(c.phoneNumber||'Not stored on SIM'):'Unavailable'],['Signal',s.phase==='unconfirmed'?'Not confirmed':signal]]):''}`,state(s,c.available,'Text alerts available','Text alerts unavailable'));
}
function location(s){
 const p=s.data,valid=p.enabled!==false&&['current','last_known'].includes(p.state)&&known(p.latitude)&&known(p.longitude)&&Math.abs(p.latitude)<=90&&Math.abs(p.longitude)<=180;
 const current=valid&&p.state==='current'&&s.phase==='current';
 const label=s.phase==='loading'?'Checking location…':s.phase==='unconfirmed'?'Location Unavailable':current?'Location Available':valid?'Location Updating':p.enabled!==false&&(p.receiving||p.decoding)?'Searching for GPS Location':'Location Unavailable';
 const note=valid?(current?'Your location can be included in an alert.':'Showing your last known location. A current location is not confirmed.'):s.phase==='loading'?'Waiting for location information.':p.enabled===false?'Location is unavailable. Have your vest checked.':'Move outdoors for a clearer view of the sky. Alerts can still be sent without a location.';
 return card('location','Location',s,`<p class="small-text">${note}</p>${valid?`<div class="location-coordinates"><div><p class="coordinate-label">Latitude</p><p class="coordinates" data-coordinate="latitude">${p.latitude.toFixed(6)}</p></div><div><p class="coordinate-label">Longitude</p><p class="coordinates" data-coordinate="longitude">${p.longitude.toFixed(6)}</p></div></div><p class="hint">${current?'Updated':'Last location'} ${esc(age(known(p.ageMs)?p.ageMs+s.elapsed:null).toLowerCase())}</p><div class="actions">${mapLink(p,current?'Open in Maps':'View last location')}<button type="button" class="secondary copy-button" data-copy="${p.latitude.toFixed(6)}, ${p.longitude.toFixed(6)}" data-copy-label="Coordinates">Copy coordinates</button></div>`:''}`,badge(label,current?'good':valid&&s.phase!=='unconfirmed'?'warn':'neutral'));
}
function camera(s,index){
 const c=s.data,title=index?'Rear camera':'Front camera';
 const delayed=c.statusFresh===false&&c.label==='Status delayed';
 const missingCard=c.label==='SD unavailable';
 if(c.downloadActive&&!delayed&&s.phase!=='unconfirmed')return card('camera'+index,title,s,'<p class="small-text">This camera is sending a clip. Recording resumes automatically.</p>',badge('Recording paused for download','neutral'));
 const label=delayed?'Updating…':missingCard?'Memory card unavailable':c.label==='Waiting for camera'?'Connecting…':c.available===false?'Recording unavailable':'Recording';
 const help=s.phase==='loading'?'Checking camera and memory card.':missingCard?'Check that the memory card is inserted and working.':delayed?'Waiting for a current camera update. Recording is not yet confirmed.':c.available?(s.phase==='unconfirmed'?'The camera was recording at the last update. Current recording is not confirmed.':'Video is being saved to the memory card.'):'Check the camera\u0027s power and memory card.';
 return card('camera'+index,title,s,`<p class="small-text">${help}</p>`,delayed?badge(s.phase==='unconfirmed'?'Status unavailable':'Updating…',s.phase==='unconfirmed'?'neutral':'warn'):c.label==='Waiting for camera'?badge(s.phase==='unconfirmed'?'Status unavailable':'Connecting…'):state(s,c.available,'Recording',label));
}
export async function mount(page,context={isCurrent:()=>true}){
 if(!context.isCurrent())return;
 try{
  mountBattery();
  if(page==='cameras'){const m=await window.smartvestBoot.module(new URL('../shared/camera-files.js',import.meta.url),context.isCurrent);if(context.isCurrent())m.mountRiderCameras(context);return;}
  if(page==='contacts'){const m=await window.smartvestBoot.module(new URL('../shared/contacts.js',import.meta.url),context.isCurrent);if(!context.isCurrent())return;await m.mountContacts(false,context);return;}
  if(page==='account'){const m=await window.smartvestBoot.module(new URL('../shared/account.js',import.meta.url),context.isCurrent);if(!context.isCurrent())return;await m.mountAccount(context);return;}
  const cache=snapshots();let data={},activityState={},lastRows=[],pendingQueue=null,hasData=false;
  function ingest(next){
   data=next;hasData=true;if(next.messageQueue)pendingQueue=next.messageQueue;
   for(const key of ['monitoring','cellular'])cache.put(key,next[key],bool(next[key]?.available));
   cache.put('battery',next.battery,next.battery&&Object.hasOwn(next.battery,'percent'));
   cache.put('location',next.location,['current','last_known','unavailable'].includes(next.location?.state));
   cache.put('contacts',{available:next.contactsConfigured,contacts:next.contacts},bool(next.contactsConfigured));
   cache.put('storage',{available:next.storageAvailable},bool(next.storageAvailable));
   cache.put('readiness',{available:next.alertsReady},bool(next.alertsReady));
   for(let i=0;i<2;i++)cache.put('camera'+i,next.cameras?.[i],bool(next.cameras?.[i]?.available));
   if(next.activity){activityState=next.activity;if(Array.isArray(next.activity.incidents))lastRows=next.activity.incidents;}
   paint();
  }
  function paint(){
   if(page==='activity'){if(hasData)paintHistory();else renderContent('<section class="panel"><div class="panel-head"><h2>Alert activity</h2></div><div class="panel-body"><p>Waiting for saved alert activity.</p></div></section>');return;}
   const m=cache.get('monitoring'),c=cache.get('cellular'),b=cache.get('battery'),g=cache.get('location'),contacts=cache.get('contacts'),storage=cache.get('storage');
   const essential=[m,c,contacts,storage,cache.get('readiness')],failed=essential.some(s=>s.data.available===false),unconfirmed=essential.some(s=>['loading','unconfirmed'].includes(s.phase));
   const ready=!failed&&!unconfirmed,contactList=Array.isArray(contacts.data.contacts)?contacts.data.contacts:[];
   const checks=`<ul class="check-list">${row('sensing','Crash monitoring',m,m.data.available,'Active','Unavailable',m.data.available===false?'Automatic crash monitoring is unavailable. Have your vest checked before riding.':'')}${row('gsm','Text alerts',c,c.data.available)}${row('contacts','Emergency contacts',contacts,contacts.data.available,'Saved','Add contacts')}${row('system','Alert storage',storage,storage.data.available,'Available','Unavailable',storage.data.available===false?'Crash alerts cannot be saved reliably. Have your vest checked before riding.':'')}</ul>`;
   const cameras=`<div class="equipment-grid">${[0,1].map(i=>camera(cache.get('camera'+i),i)).join('')}</div>`;
   if(page==='overview'){
    $('pageTitle').textContent='Overview';
    const attention=[];
    if(m.data.available===false)attention.push(['Crash monitoring is unavailable','Have your vest checked before riding.','/equipment','Check equipment']);
    if(c.data.available===false)attention.push(['Text alerts are unavailable','Check mobile coverage and the vest\u0027s SIM service.','/equipment','Check mobile service']);
    if(contacts.data.available===false)attention.push(['Add an emergency contact','Choose someone who can receive a crash alert.','/contacts','Add a contact']);
    if(storage.data.available===false)attention.push(['Alert storage needs attention','Have your vest checked before riding.','/equipment','Check equipment']);
    if(b.data.low===true)attention.push(['Charge your vest before riding','The latest battery estimate is low.','#batteryStatus','View battery']);
    const safety=cache.get('readiness');
    if(safety.data.available===false&&!attention.length)attention.push(['Automatic alerts are not ready','Review monitoring and mobile service before riding.','/equipment','Check equipment']);
    renderContent(`<section class="ride-status ${failed?'needs-attention':ready?'is-ready':''}" data-key="readiness"><div><h2 role="status" aria-live="polite">${failed?'Your vest needs attention':unconfirmed?'Checking your vest':'Monitoring and alerts ready'}</h2><p>${failed?'Resolve the items below before riding.':unconfirmed?'Waiting for current equipment status.':'Crash monitoring and text alerts are available.'}</p>${data.rider?`<p class="rider-identity">Rider <strong>${esc(data.rider)}</strong></p>`:''}</div>${badge(failed?'Needs attention':ready?'Ready':'Checking...',failed?'warn':ready?'good':'neutral')}</section><section class="ride-attention" aria-label="Needs attention" data-key="attention" ${attention.length?'':'hidden'}>${attention.map(([title,note,url,action])=>`<div class="attention-row"><div><h3>${title}</h3><p>${note}</p></div><a class="text-link" href="${url}">${action} ${icon('arrow')}</a></div>`).join('')}</section><div class="rider-overview-grid">${riderPositionMarkup()}<div class="rider-overview-side"><section class="panel" data-key="ride-checks"><div class="panel-head"><h2>Monitoring & alerts</h2><a class="text-link" href="/equipment">Details ${icon('arrow')}</a></div>${checks}<a class="link-row" href="/activity"><span>View alert activity</span>${icon('arrow')}</a></section><div class="ride-location">${location(g)}</div><section class="panel ride-contacts" data-key="contacts" data-component="contacts"><div class="panel-head"><h2>Emergency contacts</h2></div><div class="panel-body">${contactList.length?`<p class="small-text">Crash alerts go to your saved contacts.</p><ul class="contact-summary">${contactList.map(x=>`<li><strong>${esc(x.name)}</strong><span>•••• ${esc(String(x.phone).slice(-4))}</span></li>`).join('')}</ul>`:`<p class="small-text">${contacts.data.available?'Your emergency contacts are saved.':hasData?'Add someone to receive your alerts.':'Checking saved contacts.'}</p>`}<a class="text-link" href="/contacts">Manage contacts ${icon('arrow')}</a>${freshness(contacts)}</div></section></div></div>`);
   }else renderContent(`<section class="panel" data-key="monitoring"><div class="panel-head"><h2>Monitoring & alerts</h2></div>${checks}</section><div class="ride-layout section-gap">${mobile(c)}${location(g)}</div><h2 class="section-title">Cameras</h2>${cameras}`);

  }
  function paintHistory(){renderContent(messageQueue(pendingQueue,lastRows)+incidentRecords(lastRows,{loaded:activityState.loaded,writable:activityState.writable}));bindMessageQueue();}
  paint();if(page==='overview')mountRiderPosition();startPolling('rider/status?view='+encodeURIComponent(page),ingest,{interval:2000,onAge:()=>{if(hasData)paint();},validate:d=>d&&typeof d==='object'&&!Array.isArray(d)&&['monitoring','cellular','location','battery','activity'].some(k=>k in d)});
 }catch(error){if(context.isCurrent()&&error.name!=='AbortError')showError(error);}
}

