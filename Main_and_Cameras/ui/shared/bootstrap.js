(() => {
 const root=document.documentElement,base=new URL(document.currentScript.dataset.assets+'/',location.href);
 const loaded=new Map();
 let timer=null,onTimeout=null,claimed=false,entry='',serial=0,entryTimer=null,preferred='';
 function appearance(value){
  const theme=value==='dim'?'dim':'cream';root.dataset.theme=theme;
  for(const button of document.querySelectorAll('[data-theme-choice]'))button.setAttribute('aria-pressed',String(button.dataset.themeChoice===theme));
  document.querySelector('meta[name="theme-color"]')?.setAttribute('content',theme==='dim'?'#1F211F':'#F3EFE5');
 }
 let saved='cream';try{saved=localStorage.getItem('sv-theme');}catch{}appearance(saved);
 function failed(){
  root.dataset.startup='recovering';
  const message=document.getElementById('startupMessage');
  if(message)message.textContent='Connecting to your vest. Loading will continue automatically.';
 }
 function begin(callback){
  clearTimeout(timer);onTimeout=callback||null;root.dataset.startup='loading';
  timer=setTimeout(()=>{timer=null;failed();onTimeout?.();},15000);
 }
 function finish(){clearTimeout(timer);timer=null;onTimeout=null;root.dataset.startup='ready';}
 function setAppearance(value){appearance(value);try{localStorage.setItem('sv-theme',root.dataset.theme);}catch{}window.dispatchEvent(new Event('themechange'));}
 const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
 function url(relative,retry){return new URL((retry?'_r/'+(++serial)+'/':preferred)+relative,base).href;}
 function remember(value){preferred=new URL(value).pathname.slice(base.pathname.length).match(/^_r\/\d+\//)?.[0]||'';}
 async function bounded(promise){let deadline;try{return await Promise.race([promise,new Promise((_,reject)=>{deadline=setTimeout(()=>reject(new Error('Loading delayed')),35000);})]);}finally{clearTimeout(deadline);}}
 async function module(input,current=()=>true){
  const target=new URL(input,base);if(target.origin!==base.origin||!target.pathname.startsWith(base.pathname))throw new Error('Invalid dashboard module');
  const relative=target.pathname.slice(base.pathname.length).replace(/^_r\/\d+\//,'');
  if(current()&&loaded.has(relative))return loaded.get(relative);
  for(let attempt=0;current();++attempt){
   if(document.hidden){await wait(500);continue;}
   try{const candidate=url(relative,attempt>0),result=await bounded(import(candidate));if(!current())break;remember(candidate);loaded.set(relative,result);return result;}
   catch{if(!current())break;failed();await wait(Math.min(6000,300*2**Math.min(attempt,5)));}
  }
  throw new DOMException('Page changed','AbortError');
 }
 window.smartvestBoot={begin,finish,failed,setAppearance,module,watch(callback){onTimeout=callback;},claim(value){if(claimed||value!==entry)return false;claimed=true;remember(value);clearTimeout(entryTimer);return true;},get expired(){return false;}};
 async function start(){
  window.smartvestRuntime=await module('shared/runtime.js');
  let attempts=0;
  function load(){
   if(claimed)return;const script=document.createElement('script'),expected=entry=url('shared/boot.js',attempts++>0);script.type='module';script.src=expected;
   function retry(){if(claimed||entry!==expected)return;clearTimeout(entryTimer);script.remove();failed();setTimeout(load,Math.min(6000,300*2**Math.min(attempts,4)));}
   script.onerror=retry;script.onload=()=>{if(entry===expected)clearTimeout(entryTimer);};entryTimer=setTimeout(retry,35000);document.head.append(script);
  }
  load();
 }
 function controls(){appearance(root.dataset.theme);document.addEventListener('click',event=>{const button=event.target.closest('[data-theme-choice]');if(button)setAppearance(button.dataset.themeChoice);});start();}
 begin();
 if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',controls,{once:true});else controls();
})();
