import {esc,badge,toast,confirmAction} from './dom.js';
import {session} from './api.js';
import {action} from './poller.js';

export function messageQueue(queue,records=[]){
 if(!queue||!Array.isArray(queue.items))return '';
 const refs=(queue.incidents||[]).filter(ref=>Array.isArray(ref)&&ref.length>=4).map(ref=>{const row=records.find(r=>r.boot===ref[0]&&r.sequence===ref[1]),r=row?.recipients?.[ref[2]];return {boot:ref[0],sequence:ref[1],recipient:ref[2],cancellable:ref[3]===true,name:r?.name||ref[4],phone:r?.phone||ref[5],state:r?.state||(ref[3]?'queued':'sending')};});
 const manual=queue.items.map(item=>Array.isArray(item)?{id:item[0],phone:item[1],name:item[2],state:item[3],cancellable:item[4]===true}:item);
 const items=[...refs,...manual],count=items.filter(item=>item.cancellable===true).length;
 return `<section class="panel message-queue" data-key="message-queue"><div class="panel-head"><div><h2>Pending text messages</h2><p>${items.length?esc(queue.reason||'Waiting to send'):'No text messages are waiting to send.'}</p></div><button class="danger small" type="button" data-write data-message-cancel="all" data-blocked="${!count}" ${!count?'disabled':''}>Cancel all pending texts${count?' ('+count+')':''}</button></div>${items.length?`<details class="message-queue-details" data-key="pending-texts"><summary>View pending texts (${items.length})</summary><p class="queue-help">You can cancel a text before it is handed to the mobile network. A submitted text cannot be recalled.</p><ul class="message-queue-list">${items.map(item=>{
  const key=item.id?'job-'+item.id:'incident-'+item.boot+'-'+item.sequence+'-'+item.recipient;
  const target=item.id?{id:item.id}:{boot:item.boot,sequence:item.sequence,recipient:item.recipient};
  return `<li data-key="queue-${esc(key)}"><div class="queue-recipient"><strong>${esc(item.name||'Emergency contact')}</strong><span>${esc(item.phone||'Number not recorded')}</span><small>${item.boot?'Crash alert':'Text message'}</small></div><div class="queue-result">${badge(item.cancellable?(item.state==='sending'?'Preparing to send':item.state==='retry'?'Waiting to retry':'Waiting to send'):'Awaiting send result',item.cancellable?'warn':'neutral')}${item.cancellable?`<button class="secondary small" type="button" data-write data-message-cancel="${esc(JSON.stringify(target))}" aria-label="Cancel pending text to ${esc(item.name||item.phone||'this contact')}">Cancel text</button>`:'<span class="hint">Already handed to the mobile network</span>'}</div></li>`;
 }).join('')}</ul></details>`:''}</section>`;
}
export function bindMessageQueue(){
 document.querySelectorAll('[data-message-cancel]').forEach(button=>{
  button.disabled=!session.connected||session.writing||button.dataset.blocked==='true';
  button.onclick=async()=>{
   const all=button.dataset.messageCancel==='all';
   if(!await confirmAction(all?'Cancel all pending texts?':'Cancel this pending text?',all?'This stops every text still waiting or preparing to send. Texts already handed to the mobile network continue to their send result.':'This stops this text if it has not yet been handed to the mobile network. Other contacts and camera recordings are unaffected.',all?'Cancel all pending texts':'Cancel text'))return;
   try{
    const result=await action('messages/cancel',all?{all:true}:JSON.parse(button.dataset.messageCancel),button,'Queue updated.');
    if(result)toast(result.message||'Queue updated.',Boolean(result.notCancelled));
   }catch{}
  };
 });
}
