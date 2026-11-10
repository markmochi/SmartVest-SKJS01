import {esc,badge,empty} from './dom.js';

const present=value=>typeof value==='number'&&Number.isFinite(value);
const unknown='Not recorded for this alert';
const dateFormat=new Intl.DateTimeFormat('en-US',{timeZone:'Asia/Manila',month:'long',day:'numeric',year:'numeric'}),timeFormat=new Intl.DateTimeFormat('en-US',{timeZone:'Asia/Manila',hour:'numeric',minute:'2-digit',second:'2-digit',hour12:true});
const recipientText={waiting:'Waiting to send',queued:'Waiting to send',retry:'Waiting to retry',sending:'Sending',accepted:'Sent to mobile network · delivery not confirmed',sent:'Sent to mobile network · delivery not confirmed',delivered:'Delivery confirmed',undelivered:'Not delivered',failed:'Could not send',cancelled:'Cancelled',expired:'Could not send in time',unknown:'Send result unknown'};
const recipientTone=state=>state==='delivered'?'good':['failed','undelivered','expired'].includes(state)?'bad':['waiting','queued','retry','sending','unknown'].includes(state)?'warn':'neutral';
export function incidentDate(row={}){
 const value=row.occurredAtUtcMs;
 if(!present(value)||value<1577836800000||value>=4102444800000)return 'Date and time unavailable';
 const date=new Date(value);return dateFormat.format(date)+' | '+timeFormat.format(date);
}
function cameraSummary(row,key){
 const outcome=row[key],presence=row.cameraPresence?.[key];
 if(outcome==='protected_reported')return {text:'Recording protected',tone:'good',note:'The camera confirmed the recording window was protected.'};
 if(outcome==='received')return {text:'Protecting recording',tone:'warn',note:'The camera received the request. Protection is not complete yet.'};
 if(presence==='not_detected')return {text:'Not detected',tone:'neutral'};
 if(outcome==='failed_or_timeout')return {text:'Recording incomplete',tone:'bad',note:'Complete incident footage could not be confirmed.'};
 if(presence==='detected')return {text:'Waiting for confirmation',tone:'warn',note:'The camera was connected when this alert was recorded.'};
 return {text:unknown,tone:'neutral'};
}
function locationSummary(value={}){
 if(value.state==='not_detected')return {text:'Not detected',tone:'neutral'};
 if(value.state==='no_fix')return {text:'Detected · position unavailable',tone:'warn'};
 if(['current','last_known'].includes(value.state)&&present(value.latitude)&&present(value.longitude)&&Math.abs(value.latitude)<=90&&Math.abs(value.longitude)<=180){
  return {text:value.state==='current'?'Position recorded':'Last known position recorded',tone:value.state==='current'?'good':'warn',note:value.latitude.toFixed(6)+', '+value.longitude.toFixed(6),coordinates:value.latitude.toFixed(6)+', '+value.longitude.toFixed(6)};
 }
 return {text:unknown,tone:'neutral'};
}
function mobileSummary(row){
 const recipients=Array.isArray(row.recipients)?row.recipients:[],value=row.mobile||{};
 if(recipients.length&&recipients.every(r=>r.state==='delivered'))return {text:'Delivery confirmed for all contacts',tone:'good'};
 if(recipients.some(r=>['accepted','sent','delivered'].includes(r.state))){
  const incomplete=recipients.some(r=>!['accepted','sent','delivered'].includes(r.state));
  return {text:incomplete?'Mixed sending results · check each contact below':'Text sent · see each contact below',tone:incomplete?'warn':'good'};
 }
 if(value.state==='not_detected')return {text:'Not detected',tone:'neutral'};
 if(!recipients.length||row.noRecipients)return {text:'No emergency contacts saved',tone:'warn'};
 if(recipients.every(r=>r.state==='cancelled'))return {text:'Pending texts cancelled',tone:'neutral'};
 if(recipients.every(r=>['failed','undelivered','expired','cancelled'].includes(r.state)))return {text:'Emergency text not sent successfully',tone:'bad'};
 if(recipients.some(r=>r.state==='sending'))return {text:'Sending emergency text',tone:'warn'};
 const states={sim_unavailable:'Detected · SIM card unavailable',sim_locked:'Detected · SIM card locked',not_registered:'Detected · mobile network unavailable',ready:'Ready · alert waiting to send'};
 return {text:states[value.service]||'Waiting for a confirmed send result',tone:'warn'};
}
function impactSummary(value={}){
 if(value.recorded){
  const names=[value.front?'front':null,value.rear?'rear':null].filter(Boolean);
  return names.length?{text:'Impact detected',tone:'warn',note:(names.length===2?'Front and rear sensors':'The '+names[0]+' sensors')+' reported an impact for this alert.'}:{text:'No qualifying impact recorded',tone:'neutral'};
 }
 if(value.state==='unavailable')return {text:'Readings unavailable',tone:'neutral'};
 return {text:'Impact result not recorded',tone:'neutral'};
}
function moduleCard(title,summary,extra=''){
 return `<section class="incident-module"><h4>${esc(title)}</h4><div class="incident-module-state">${badge(summary.text,summary.tone||'neutral')}</div>${summary.note?`<p class="hint">${esc(summary.note)}</p>`:''}${extra}</section>`;
}
export function incidentRecords(rows,{admin=false,loaded=true,writable=true}={}){
 const records=(Array.isArray(rows)?rows:[]).filter(r=>r.diagnostic!==true&&!/^SmartVest test(?:\r?\n|$)|^TEST(?: CRASH)?:/i.test(r.message||'')).slice().reverse().sort((a,b)=>(present(b.occurredAtUtcMs)?b.occurredAtUtcMs:0)-(present(a.occurredAtUtcMs)?a.occurredAtUtcMs:0));
 const warning=!loaded||!writable?'<div class="notice warn" role="status">Alert storage needs attention. These records may not include the latest alert.</div>':'';
 return warning+`<section class="incident-records"><div class="panel incident-list-heading"><div><h2>${admin?'Incident records':'Crash alert activity'}</h2><p>Philippine time (UTC+08). Each card is a separate crash alert. Equipment details are saved with the alert; sending and recording results update when confirmed.</p></div>${badge(records.length+' '+(records.length===1?'record':'records'),'neutral')}</div>${records.length?records.map((row,index)=>{
  const movement=row.movement||{},recipients=Array.isArray(row.recipients)?row.recipients:[],location=locationSummary(row.location),mobile=mobileSummary(row),battery=row.battery||{};
  const batteryStatus=battery.state==='not_detected'?{text:'Not detected'}:battery.state==='detected'?{text:present(battery.percent)?battery.percent+'% estimated charge':'Detected',tone:'neutral',note:present(battery.voltage)?battery.voltage.toFixed(2)+' V recorded':null}:{text:unknown};
  const numbers=movement.recorded&&present(movement.accelerationG)&&present(movement.rotationDps)?`<div class="incident-movement-values"><div><span>Acceleration peak</span><strong>${movement.accelerationG.toFixed(2)} <small>g</small></strong></div><div><span>Rotation speed</span><strong>${Math.round(movement.rotationDps)} <small>degrees/second</small></strong></div></div><p class="hint">1 g is normal gravity. These are the movement peaks saved for this alert.</p>`:`<p class="hint">Movement values were not recorded for this alert.</p>`;
  return `<article class="panel incident-card" data-key="incident-${esc(row.boot)}-${esc(row.sequence)}"><header class="incident-heading"><div><div class="incident-heading-top">${badge('ALERT','bad')}<span class="incident-number">Alert ${records.length-index} of ${records.length}</span></div><h3>Crash detected</h3><p class="incident-date">${esc(incidentDate(row))}</p>${present(row.occurredAtUtcMs)&&row.timeBasis==='recorded'?'<p class="hint">Time the alert was recorded; the exact detection time was unavailable.</p>':''}</div></header><div class="incident-body"><section class="incident-movement"><h4>${admin?'Movement at detection':'Recorded movement'}</h4>${numbers}</section><div class="incident-module-grid">${moduleCard(admin?'GPS location · NEO-6M':'Location',location,location.coordinates?`<a class="text-link" target="_blank" rel="noopener noreferrer" href="https://maps.google.com/?q=${encodeURIComponent(location.coordinates)}">View recorded position</a>`:'')}${moduleCard(admin?'Emergency SMS · SIM800L':'Emergency text messages',mobile)}${moduleCard(admin?'Impact sensors · piezo':'Impact sensors',impactSummary(row.impact))}${moduleCard('Front camera',cameraSummary(row,'front'))}${moduleCard('Rear camera',cameraSummary(row,'rear'))}${moduleCard(admin?'Battery monitor · INA226':'Vest battery',batteryStatus)}</div><details class="incident-recipients" data-key="contacts-${esc(row.boot)}-${esc(row.sequence)}"><summary><span>Emergency contacts</span><span class="incident-contact-count">${recipients.length} ${recipients.length===1?'contact':'contacts'}</span><span class="contact-disclosure"><span class="when-closed">View details</span><span class="when-open">Hide details</span></span></summary>${recipients.length?`<ul>${recipients.map(r=>`<li><div><strong>${esc(r.name||'Emergency contact')}</strong><span>${esc(r.phone||'Number not recorded')}</span></div>${badge(recipientText[r.state]||'Status not recorded',recipientTone(r.state))}</li>`).join('')}</ul>`:'<p>No emergency contacts were saved when this alert was recorded.</p>'}</details>${admin&&row.message?`<details class="incident-message" data-key="message-${esc(row.boot)}-${esc(row.sequence)}"><summary>Saved alert message</summary><p>${esc(row.message)}</p></details>`:''}</div></article>`;
 }).join(''):empty('No crash alerts recorded','An alert will appear here when your vest records a crash-like event.','incidents')}</section>`;
}
