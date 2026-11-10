import {$,message} from './dom.js';
import {api,session} from './api.js';
import {startPolling,stopPolling,received} from './poller.js';
import {mountContacts} from './contacts.js';
export async function mountOnboarding(context,done){
 let stepController=null,pose=null,busy=false,revision=0;
 document.body.classList.add('rider-setup');
 const dispose=()=>{stepController?.abort();pose?.dispose();pose=null;stopPolling();};
 context.signal.addEventListener('abort',dispose,{once:true});
 function stage(title,index){
  dispose();stepController=new AbortController();const controller=stepController;
  $('pageTitle').textContent=title;document.title=title+' | SmartVest';
  const host=$('pageContent');host.classList.remove('setup-enter');host.replaceChildren();
  requestAnimationFrame(()=>{if(context.isCurrent())host.classList.add('setup-enter');});
  $('receivedTime').textContent='Step '+index+' of 3';$('main').focus({preventScroll:true});window.scrollTo(0,0);
  return {...context,signal:controller.signal,isCurrent:()=>context.isCurrent()&&!controller.signal.aborted};
 }
 async function contacts(){
  const child=stage('Your people',2);
  await mountContacts(false,child,async saved=>{if(child.isCurrent())await position(saved);});
  if(!child.isCurrent())return;
  const intro=document.createElement('div');intro.className='setup-intro';
  intro.innerHTML='<p class="setup-eyebrow">A little preparation. A closer connection.</p><h2>Who should we reach?</h2><p>Add someone you trust. If your vest detects a possible crash, it can text your saved contacts and include your location when available. Delivery depends on cellular coverage.</p><p>You can add up to five people and change them later in Contacts.</p>';
  $('pageContent').prepend(intro);$('riderName').focus({preventScroll:true});
 }
 async function position(saved){
  const child=stage('Make yourself comfortable',3);revision=saved.revision;window.sjksDirty=false;
  $('pageContent').innerHTML='<div class="setup-intro"><p class="setup-eyebrow">Made to move with you</p><h2>Your vest. Your riding position.</h2><p>Your vest uses the positions saved during its setup. Check how the view follows you. If it needs adjusting, choose “Make it match your riding position” below.</p><p>This step is optional. You can adjust it later from Overview. Stay parked and away from traffic while adjusting.</p></div>'+'<div id="setupPosition"><p>Opening your riding view… You can continue without adjusting it.</p></div>'+'<div class="setup-finish"><p id="setupMessage" role="status"></p><button id="finishSetup" data-write>Continue to my dashboard</button><button id="setupBack" class="secondary">Review contacts</button></div>';
  startPolling('rider/status?view=overview',()=>{},{interval:5000});
  $('setupBack').onclick=()=>{if(!busy&&!calibrating())contacts().catch(failed);};
  function calibrating(){const panel=document.querySelector('[data-rp-setup]');if(panel&&!panel.hidden){message('setupMessage','Finish your position adjustment or close it before continuing.');return true;}return false;}
  $('finishSetup').onclick=async()=>{
   if(busy||!session.connected||calibrating())return;
   busy=true;session.writing=true;$('finishSetup').disabled=true;
   try{await api('rider/setup','POST',{revision});if(child.isCurrent()){session.writing=false;dispose();document.body.classList.remove('rider-setup');await done();}}
   catch(error){if(child.isCurrent())message('setupMessage',error.message);}
   finally{if(child.isCurrent()){session.writing=false;busy=false;$('finishSetup').disabled=!session.connected;}}
  };
  const view=await window.smartvestBoot.module(new URL('./rider-position.js',import.meta.url),child.isCurrent);
  if(!child.isCurrent())return;
  $('setupPosition').innerHTML=view.riderPositionMarkup();pose=view.mountRiderPosition();
 }
 function failed(error){if(context.isCurrent()&&error.name!=='AbortError')message('setupMessage',error.message);}
 stage('Welcome to SmartVest',1);
 $('pageContent').innerHTML='<section class="setup-welcome"><span class="setup-orbit" aria-hidden="true">SV</span><p class="setup-eyebrow">A thoughtful start to every ride</p><h2>Hello, Rider.</h2><p>Let’s make SmartVest yours.</p><p class="setup-subtitle">Choose your emergency contacts, then get comfortable with your riding view. Just a few moments before you begin.</p><button id="beginSetup">Let’s get started</button><p id="setupMessage" role="status"></p></section>';
 received();$('beginSetup').onclick=()=>contacts().catch(failed);$('beginSetup').focus({preventScroll:true});
}
