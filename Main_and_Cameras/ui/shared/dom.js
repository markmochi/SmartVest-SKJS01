import {icon} from './icons.js';
export const $=id=>document.getElementById(id);
export const esc=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export const known=value=>typeof value==='number'&&Number.isFinite(value);
export const num=(value,digits=0,suffix='')=>known(value)?value.toLocaleString(undefined,{minimumFractionDigits:digits,maximumFractionDigits:digits})+suffix:'Unavailable';
export function age(value){
 if(!known(value))return 'Not observed';if(value<1000)return 'Less than 1 second ago';
 const divisor=value<60000?1000:value<3600000?60000:3600000,count=Math.floor(value/divisor),unit=divisor===1000?'second':divisor===60000?'minute':'hour';
 return count+' '+unit+(count===1?'':'s')+' ago';
}
export const yes=(value,positive='Yes',negative='No')=>value===true?positive:value===false?negative:'Unknown';
export const badge=(text,tone='neutral')=>`<span class="status ${['good','warn','bad','neutral'].includes(tone)?tone:'neutral'}">${esc(text)}</span>`;
export function kv(rows){return '<dl class="key-values">'+rows.map(([name,value])=>`<dt>${esc(name)}</dt><dd>${esc(value??'Unavailable')}</dd>`).join('')+'</dl>';}
export function metrics(rows){return `<div class="metric-strip" data-live>${rows.map(([name,value,help])=>`<div class="metric"><span class="label">${esc(name)}</span><span class="value">${esc(value??'—')}</span>${help?`<small>${esc(help)}</small>`:''}</div>`).join('')}</div>`;}
export function panel(title,body,subtitle=''){return `<section class="panel"><div class="panel-head"><div><h2>${esc(title)}</h2>${subtitle?`<p>${esc(subtitle)}</p>`:''}</div></div><div class="panel-body">${body}</div></section>`;}
export function notice(text,tone='neutral'){return `<div class="notice ${tone}" role="status">${icon(tone==='good'?'check':'warning')}<p>${esc(text)}</p></div>`;}
export function empty(title,help,kind='activity'){return `<div class="empty">${icon(kind)}<h3>${esc(title)}</h3><p>${esc(help)}</p></div>`;}
export function showError(error,container='pageContent'){$(container).innerHTML=notice(error.message||'This page could not be loaded.','bad')+'<button id="retryPage" class="secondary">Try again</button>'; $('retryPage').onclick=()=>location.reload();}
export function message(id,text,good=false){const el=$(id);if(!el)return;el.textContent=text;el.classList.toggle('success',good);}
let toastTimer;
export function toast(text,error=false){const el=$('toast');el.textContent=text;el.classList.toggle('error',error);el.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>{el.hidden=true},error?12000:6500);}
let settle=null,previousFocus=null;
export function confirmAction(title,text,action='Continue'){const dialog=$('confirmDialog');if(settle)return Promise.resolve(false);previousFocus=document.activeElement;$('confirmTitle').textContent=title;$('confirmText').textContent=text;$('confirmYes').textContent=action;dialog.showModal();$('confirmNo').focus();return new Promise(resolve=>{settle=resolve;});}
export function closeConfirmation(result=false){if(!settle)return;const resolve=settle;settle=null;$('confirmDialog').close();previousFocus?.focus();resolve(result);}
export function initCommon(){document.addEventListener('toggle',rememberToggle,true);document.querySelectorAll('[data-reveal]').forEach(button=>{button.onclick=()=>{const input=$(button.dataset.reveal);const reveal=input.type==='password';input.type=reveal?'text':'password';button.textContent=reveal?'Hide':'Show';button.setAttribute('aria-pressed',String(reveal));};});$('confirmNo').onclick=()=>closeConfirmation(false);$('confirmYes').onclick=()=>closeConfirmation(true);$('confirmDialog').addEventListener('cancel',event=>{event.preventDefault();closeConfirmation(false)});document.addEventListener('visibilitychange',()=>{if(document.hidden)closeConfirmation(false)});}
export function password(id,label,autocomplete='new-password',required=true){return `<div class="field"><label for="${id}">${esc(label)}</label><div class="password-field"><input id="${id}" name="${id}" type="password" ${required?'required':''} autocomplete="${autocomplete}" maxlength="64"><button type="button" data-reveal="${id}" aria-controls="${id}" aria-pressed="false">Show</button></div></div>`;}
export function mapLink(position,label='Open in Maps'){if(!position||!known(position.latitude)||!known(position.longitude)||Math.abs(position.latitude)>90||Math.abs(position.longitude)>180)return '';const query=position.latitude.toFixed(6)+','+position.longitude.toFixed(6);return `<a class="button secondary" href="https://maps.google.com/?q=${esc(query)}" target="_blank" rel="noopener noreferrer">${esc(label)} ${icon('arrow')}</a>`;}
export function positionBlock(position={}){
 const fresh=['fresh','current'].includes(position.state);
 const stale=['stale','last_known'].includes(position.state);
 const valid=position.enabled!==false&&(fresh||stale)&&known(position.latitude)&&known(position.longitude)&&Math.abs(position.latitude)<=90&&Math.abs(position.longitude)<=180;
 const label=position.enabled===false?'Disabled':valid?(fresh?'Current fix':'Last known position'):(position.receiving||position.decoding?'Receiving / no fix':'No fix');
 const missing=position.enabled===false?'GPS is disabled.':position.decoding?'Receiving GPS data. Waiting for a position fix.':position.receiving?'GPS bytes received; no valid position yet.':'No position available.';
 return `<div class="position-block" data-live>${badge(label,valid?(fresh?'good':'warn'):'neutral')}${valid?`<p class="coordinate-label">Latitude</p><p class="coordinates" data-coordinate="latitude">${position.latitude.toFixed(6)}</p><p class="coordinate-label">Longitude</p><p class="coordinates" data-coordinate="longitude">${position.longitude.toFixed(6)}</p><p class="hint">${fresh?'Updated':'Last fix'} ${age(position.ageMs).toLowerCase()}</p><div class="actions">${mapLink(position)}<button type="button" class="secondary copy-button" data-copy="${position.latitude.toFixed(6)}, ${position.longitude.toFixed(6)}" data-copy-label="Coordinates">Copy coordinates</button></div>`:`<p class="small-text">${esc(missing)}</p>`}</div>`;
}
const detailsKey=()=>`sv-details:${document.body.dataset.area}:${document.body.dataset.page}`;
const rememberDetail=el=>el.matches('details[data-key]')&&/^(?:(?:gsm|gps|sensing|power|network)-details|camera-[01]|cam-(?:front|rear)|latency-\d+-\d+)$/.test(el.dataset.key)&&!el.querySelector('input,textarea,[data-write],[data-reveal]');
let detailState;const restoredDetails=new WeakSet();
function storedDetails(){if(!detailState){try{detailState=JSON.parse(sessionStorage.getItem(detailsKey())||'{}');if(!detailState||Array.isArray(detailState)||typeof detailState!=='object')detailState={};}catch{detailState={};}}return detailState;}
function rememberToggle(event){if(!rememberDetail(event.target))return;const state=storedDetails();state[event.target.dataset.key]=event.target.open;const keys=Object.keys(state);if(keys.length>32)delete state[keys[0]];try{sessionStorage.setItem(detailsKey(),JSON.stringify(state));}catch{}}
// Patch matching nodes in place: readings can change without replacing focused
// controls, expanded diagnostics, canvas nodes, or the user's text selection.
function patchNode(old,next){
 if(old.nodeType!==next.nodeType||old.nodeName!==next.nodeName||
    (old.nodeType===1&&(old.id!==next.id||old.dataset.key!==next.dataset.key))){old.replaceWith(next.cloneNode(true));return;}
 if(old.nodeType!==1){if(old.nodeValue!==next.nodeValue)old.nodeValue=next.nodeValue;return;}
 if(old.hasAttribute('data-live-owned')&&next.hasAttribute('data-live-owned'))return;
 const preserveOpen=old.tagName==='DETAILS';
 for(const attr of [...old.attributes])if(!next.hasAttribute(attr.name)&&!(preserveOpen&&attr.name==='open'))old.removeAttribute(attr.name);
 for(const attr of next.attributes)if(!(preserveOpen&&attr.name==='open')&&old.getAttribute(attr.name)!==attr.value)old.setAttribute(attr.name,attr.value);
 const children=[...old.childNodes],incoming=[...next.childNodes];
 incoming.forEach((child,i)=>{if(children[i])patchNode(children[i],child);else old.append(child.cloneNode(true));});
 children.slice(incoming.length).forEach(child=>child.remove());
}
export function renderContent(html,id='pageContent'){
 const host=$(id),template=document.createElement('template');template.innerHTML=html;
 const old=[...host.childNodes],next=[...template.content.childNodes];
 next.forEach((node,i)=>{if(old[i])patchNode(old[i],node);else host.append(node.cloneNode(true));});old.slice(next.length).forEach(node=>node.remove());
 for(const detail of host.querySelectorAll('details[data-key]'))if(!restoredDetails.has(detail)&&rememberDetail(detail)){restoredDetails.add(detail);if(storedDetails()[detail.dataset.key]===true)detail.open=true;}
}
