import {api,session,setSession,readApi} from './api.js';
import {$,initCommon,message,closeConfirmation,confirmAction,toast} from './dom.js';
import {stopPolling,setConnection} from './poller.js';
import {initExperience} from './experience.js';
async function start(){
if(!window.smartvestBoot?.claim(import.meta.url))return;
let busy=false,active=null,attempt=0;
let startup=window.smartvestBoot;
const area=/^\/admin(?:\/|$)/.test(location.pathname)?'developer':'rider';
document.body.dataset.area=area;
// One flash layout serves both roles; the API enforces the signed-in permissions.
if(area==='rider'){
 $('sideNav')?.remove();$('mobileMenu')?.remove();document.querySelector('.dev-layout')?.classList.remove('dev-layout');
}else{document.querySelector('.rider-nav')?.remove();$('batterySlot')?.remove();}
if($('consoleLabel'))$('consoleLabel').textContent=area==='developer'?'Administrator dashboard':'Rider dashboard';
const normalized=path=>path==='/admin/'?'/admin':path;
const routeLink=path=>[...document.querySelectorAll('nav a[data-page]')].find(link=>link.getAttribute('href')===normalized(path));
let page='overview',currentRoute=location.pathname+location.hash,confirming=false;
function selectRoute(path){
 const current=routeLink(path);page=current?.dataset.page||'overview';document.body.dataset.page=page;
 for(const link of document.querySelectorAll('nav a[data-page]'))link.removeAttribute('aria-current');
 if(current){current.setAttribute('aria-current','page');$('pageTitle').textContent=current.querySelector('span').textContent;}
 document.title=$('pageTitle').textContent+' | SmartVest';
}
selectRoute(location.pathname);
const experience=initExperience();
function disposeMount(){document.body.classList.remove('rider-setup');active?.controller.abort();active=null;stopPolling();window.dispatchEvent(new Event('vestsignout'));}
function newMount(){
 disposeMount();const id=++attempt,generation=session.generation,controller=new AbortController();
 const context={controller,signal:controller.signal,isCurrent:()=>active===context&&id===attempt&&!controller.signal.aborted&&generation===session.generation};
 active=context;return context;
}
function loadingFailed(){
 startup?.failed();
 if(session.logged)setConnection(false,{label:'Loading dashboard',detail:'Loading page content. Retrying automatically.'});
}

function loginView(text=''){
 disposeMount();closeConfirmation();setSession(null);startup?.finish();busy=false;$('loginSubmit').disabled=false;
 $('workspace').hidden=true;$('authBoot').hidden=true;$('authWrap').hidden=false;
 $('authUser').value='';$('authPass').value='';$('authPass').type='password';
 $('loginReveal').textContent='Show';$('loginReveal').setAttribute('aria-pressed','false');
 $('logout').hidden=true;$('connection').hidden=true;if($('mobileMenu'))$('mobileMenu').hidden=true;
 $('skipContent').href='#authWrap';document.title='Sign in | SmartVest';
 message('loginMessage',text);$('authUser').focus();
}
async function mountCurrentPage(){
 const context=newMount();startup?.begin(loadingFailed);
 $('authBoot').hidden=true;$('authWrap').hidden=true;$('workspace').hidden=false;
 $('logout').hidden=false;$('connection').hidden=false;experience.open();
 if($('mobileMenu'))$('mobileMenu').hidden=false;
 $('skipContent').href='#main';document.title=$('pageTitle').textContent+' | SmartVest';
 $('authPass').value='';$('receivedTime').textContent='';setConnection(false,{label:'Connecting to Vest',quiet:true});
 try{
  if(area==='rider'&&!session.riderSetupComplete){const setup=await startup.module(new URL('./onboarding.js',import.meta.url),context.isCurrent);if(context.isCurrent()){startup?.finish();await setup.mountOnboarding(context,async()=>{session.riderSetupComplete=true;window.sjksDirty=false;selectRoute(location.pathname);await mountCurrentPage();});}return;}
  const module=await startup.module(new URL(area==='developer'?'../developer/app.js':'../rider/app.js',import.meta.url),context.isCurrent);
  if(!context.isCurrent())return;await module.mount(page,context);
  if(context.isCurrent())startup?.finish();
 }catch(error){if(context.isCurrent()&&error.name!=='AbortError')loadingFailed();}
}
async function open(data){
 setSession(data);
 if(area!==data.role){location.replace(data.role==='developer'?'/admin':'/');return;}
 await mountCurrentPage();
}
async function navigate(url,pop=false){
 if(confirming||document.body.classList.contains('rider-setup'))return;
 if(session.writing){toast('Wait for the current action to finish.',true);if(pop)history.pushState(null,'',currentRoute);return;}
 if(window.sjksDirty){
  confirming=true;let leave=false;
  try{leave=await confirmAction('Leave this page?',page==='ride-test'?'The recording will stop. Your saved readings remain available.':'Unsaved changes on this page will be discarded.','Leave page');}
  finally{confirming=false;}
  if(!leave){if(pop)history.pushState(null,'',currentRoute);return;}
 }
 window.sjksDirty=false;closeConfirmation();
 if(!pop)history.pushState(null,'',url.pathname+url.hash);
 const destination=url.pathname+url.hash;currentRoute=destination;selectRoute(url.pathname);
 $('pageContent').innerHTML='<div class="loading"><h2>Loading…</h2></div>';
 await mountCurrentPage();
 if(currentRoute===destination){$('main').focus({preventScroll:true});window.scrollTo(0,0);}
}
initCommon();
// Keep the shell and loaded modules when changing dashboard pages. Each new
// mount disposes old polls/scenes; signed-in API permissions remain authoritative.
document.addEventListener('click',event=>{
 const link=event.target.closest('a[href]');
 if(!link||event.defaultPrevented||event.button!==0||event.ctrlKey||event.metaKey||event.shiftKey||event.altKey||link.target||link.hasAttribute('download')||!session.logged)return;
 const url=new URL(link.href,location.href);
 if(url.origin!==location.origin||url.search||!routeLink(url.pathname))return;
 if(normalized(url.pathname)===normalized(location.pathname))return;
 event.preventDefault();navigate(url);
});
window.addEventListener('popstate',()=>{
 if(!session.logged||!routeLink(location.pathname))return;
 const url=new URL(location.href);
 if(normalized(url.pathname)===normalized(currentRoute.split('#')[0]))return;
 navigate(url,true);
});
$('loginForm').addEventListener('submit',async event=>{
 event.preventDefault();if(busy)return;busy=true;const button=$('loginSubmit');button.disabled=true;message('loginMessage','Signing in...');
 try{const data=await api('login','POST',{username:$('authUser').value.trim(),password:$('authPass').value,utcMs:Date.now()});message('loginMessage','');await open(data);}
 catch(error){if(session.logged)loadingFailed();else message('loginMessage',error.message);}
 finally{busy=false;button.disabled=false;}
});
$('logout').onclick=async()=>{
 if(window.sjksDirty&&!await confirmAction('Discard unsaved changes?','Changes on this page have not been saved.','Sign out'))return;
 try{await api('logout','POST',{});window.sjksDirty=false;location.replace('/');}
 catch(error){message('connectionDetail',error.message);$('connectionBanner').hidden=false;}
};
window.addEventListener('sessionexpired',()=>{window.sjksDirty=false;loginView('Your session has ended. Sign in again.');});
window.addEventListener('beforeunload',event=>{if(window.sjksDirty&&session.logged){event.preventDefault();event.returnValue='';}});
window.addEventListener('pagehide',disposeMount);
// Frozen live views are disposed; revalidate them through a fresh document.
window.addEventListener('pageshow',event=>{if(!event.persisted)return;window.sjksDirty=false;disposeMount();setSession(null);closeConfirmation();location.reload();});
if(startup){
 const context=newMount();startup?.watch(loadingFailed);
 try{const data=await readApi('session',{signal:context.signal,onRetry:loadingFailed});if(context.isCurrent())await open(data);}
 catch(error){if(context.isCurrent())loginView(error.status===401?'':error.message);}
}

}
await start();
