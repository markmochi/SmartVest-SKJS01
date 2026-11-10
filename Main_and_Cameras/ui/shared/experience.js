import {session} from './api.js';
import {$,toast} from './dom.js';
export function initExperience(){
 const jump=$('pageJump'),dialog=$('pageSearch'),input=$('pageSearchInput'),results=$('pageSearchResults'),density=$('viewDensity'),menu=$('mobileMenu'),nav=$('sideNav'),backdrop=$('navBackdrop'),compact=matchMedia('(max-width: 980px)');
 let matches=[],selected=0,previous=null;
 function spacing(value){document.body.dataset.density=value==='compact'?'compact':'comfortable';density?.setAttribute('aria-pressed',String(value==='compact'));}
 try{spacing(localStorage.getItem('sv-density'));}catch{spacing('comfortable');}
 density?.addEventListener('click',()=>{const value=document.body.dataset.density==='compact'?'comfortable':'compact';spacing(value);try{localStorage.setItem('sv-density',value);}catch{}toast(value==='compact'?'Compact spacing on.':'Comfortable spacing on.');});
 function drawer(open,focus=false){
  open=Boolean(open&&session.logged&&compact.matches&&nav);if(nav){nav.classList.toggle('open',open);nav.inert=compact.matches&&!open;nav.setAttribute('aria-hidden',String(compact.matches&&!open));}
  document.body.dataset.menuOpen=String(open);menu?.setAttribute('aria-expanded',String(open));if(backdrop)backdrop.hidden=!open;
  if($('main'))$('main').inert=open;
  if(open)(nav.querySelector('[aria-current="page"]')||nav.querySelector('a'))?.focus();else if(focus&&session.logged)menu?.focus();
 }
 function closeSearch(){if(dialog?.open)dialog.close();}
 function pick(index){
  selected=Math.max(0,Math.min(index,matches.length-1));const rows=[...results.querySelectorAll('[role="option"]')];rows.forEach((row,i)=>{row.classList.toggle('is-selected',i===selected);row.setAttribute('aria-selected',String(i===selected));});
  if(matches.length){input.setAttribute('aria-activedescendant',rows[selected].id);rows[selected].scrollIntoView({block:'nearest'});}else input.removeAttribute('aria-activedescendant');
 }
 function filter(){
  const query=input.value.trim().toLowerCase();matches=[...document.querySelectorAll('nav a[data-page]')].filter(link=>link.textContent.toLowerCase().includes(query));results.replaceChildren();
  matches.forEach((link,i)=>{const button=document.createElement('button');button.type='button';button.className='page-search-result';button.id='page-option-'+i;button.dataset.searchIndex=i;button.setAttribute('role','option');button.tabIndex=-1;const label=document.createElement('span');label.textContent=link.textContent.trim();button.append(label);if(link.hasAttribute('aria-current')){const current=document.createElement('small');current.textContent='Current page';button.append(current);}results.append(button);});
  if(!matches.length){const empty=document.createElement('p');empty.className='page-search-empty';empty.textContent='No matching pages. Try another name.';results.append(empty);}pick(0);
 }
 function search(){if(!session.logged||!dialog||document.querySelector('dialog[open]'))return;previous=document.activeElement;if(compact.matches&&nav?.contains(previous))previous=menu;drawer(false);input.value='';filter();dialog.showModal();results.scrollTop=0;pick(0);input.setAttribute('aria-expanded','true');input.focus();}
 function navigate(){const link=matches[selected];if(link){closeSearch();link.click();}}
 jump?.addEventListener('click',search);$('pageSearchClose')?.addEventListener('click',closeSearch);
 dialog?.addEventListener('close',()=>{input.setAttribute('aria-expanded','false');if(session.logged&&previous?.isConnected)previous.focus();});
 dialog?.addEventListener('click',event=>{const row=event.target.closest('[data-search-index]');if(row){pick(Number(row.dataset.searchIndex));navigate();}else if(event.target===dialog)closeSearch();});
 input?.addEventListener('input',filter);
 input?.addEventListener('keydown',event=>{const keys=['ArrowDown','ArrowUp','Home','End','Enter'];if(!keys.includes(event.key))return;event.preventDefault();if(event.key==='Enter')navigate();else pick(event.key==='Home'?0:event.key==='End'?matches.length-1:selected+(event.key==='ArrowDown'?1:-1));});
 menu?.addEventListener('click',()=>drawer(!nav.classList.contains('open')));backdrop?.addEventListener('click',()=>drawer(false,true));
 const resize=()=>drawer(false);compact.addEventListener('change',resize);
 document.addEventListener('keydown',event=>{
  if(event.key==='Escape'&&dialog?.open){event.preventDefault();closeSearch();return;}
  if(event.key==='Escape'&&nav?.classList.contains('open')){event.preventDefault();drawer(false,true);return;}
  if(event.key==='Tab'&&nav?.classList.contains('open')){event.preventDefault();const items=[...nav.querySelectorAll('a[href]'),menu],index=items.indexOf(document.activeElement);items[(index+(event.shiftKey?-1:1)+items.length)%items.length]?.focus();}
  const editing=event.target.closest('input,textarea,select,[contenteditable]:not([contenteditable="false"])');
  if(!editing&&!event.altKey&&(event.key==='/'||((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='k'))&&session.logged){event.preventDefault();search();}
 });
 document.addEventListener('click',async event=>{
  if(nav?.classList.contains('open')&&!event.target.closest('#sideNav,#mobileMenu'))drawer(false);
  const button=event.target.closest('[data-copy]');if(!button||button.onclick||!session.logged)return;
  const source=$(button.dataset.copy),text=source?(source.value??source.textContent):button.dataset.copy,generation=session.generation;if(!text)return;
  let copied=false;try{await navigator.clipboard.writeText(text);copied=true;}catch{const field=document.createElement('textarea');field.value=text;field.readOnly=true;field.style.cssText='position:fixed;opacity:0;pointer-events:none';document.body.append(field);field.select();try{copied=document.execCommand('copy');}catch{}field.remove();button.focus();}
  if(generation===session.generation)toast(copied?(button.dataset.copyLabel||'Text')+' copied.':'Copy is unavailable. Select the text and use your device\u0027s Copy command.',!copied);
 });
 function reset(){closeSearch();drawer(false);if(jump)jump.hidden=true;}
 document.addEventListener('visibilitychange',()=>{if(document.hidden){closeSearch();drawer(false);}});window.addEventListener('vestsignout',reset);window.addEventListener('pagehide',()=>{compact.removeEventListener('change',resize);closeSearch();drawer(false);});window.addEventListener('pageshow',event=>{if(event.persisted){compact.addEventListener('change',resize);drawer(false);}});
 drawer(false);
 return {open(){if(jump)jump.hidden=false;drawer(false);}};
}
