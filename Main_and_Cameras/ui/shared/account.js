import {api,session,readApi} from './api.js';
import {$,password,confirmAction,message} from './dom.js';
import {received,startPolling} from './poller.js';
export async function mountAccount(context={isCurrent:()=>true}){
 let busy=false,account=null,minimum=session.minPasswordLength;
 const administrator=document.body.dataset.area==='developer',path=administrator?'admin/account':'rider/account';
 const theme=document.documentElement.dataset.theme;
 $('pageContent').innerHTML=`<section class="panel account-panel appearance-settings" aria-labelledby="appearanceTitle"><div class="panel-head"><h2 id="appearanceTitle">Appearance</h2></div><div class="panel-body"><div class="appearance-options" role="group" aria-label="Color theme"><button type="button" class="secondary appearance-choice" data-theme-choice="cream" aria-pressed="${theme!=='dim'}">Light</button><button type="button" class="secondary appearance-choice" data-theme-choice="dim" aria-pressed="${theme==='dim'}">Dim</button></div><p class="hint">Saved in this browser.</p></div></section><section class="panel account-panel"><div class="panel-head"><h2>${administrator?'Administrator':'Rider'} sign-in details</h2></div>
 <form id="accountForm"><div class="panel-body">
 <div class="field"><label for="accountUsername">Username</label><input id="accountUsername" name="username" autocomplete="username" autocapitalize="none" spellcheck="false" required minlength="3" maxlength="32" pattern="[A-Za-z0-9_.-]{3,32}" value=""></div>
 ${password('currentPassword','Current password','current-password')}
 ${password('newPassword','New password (optional)','new-password',false)}
 <p id="accountPasswordHelp" class="hint">Leave blank to keep your password. New passwords need ${minimum}–64 characters.</p>
 ${password('repeatPassword','Confirm new password','new-password',false)}
 <p id="accountMessage" class="form-message" role="alert" aria-live="polite"></p></div>
 <div class="action-footer"><p>Sign in again after saving.</p><button id="saveAccount" type="submit" data-write>Save changes</button></div></form></section>`;
 $('saveAccount').dataset.blocked='true';
 document.querySelectorAll('#accountForm input,#accountForm button').forEach(node=>node.disabled=true);
 account=await readApi(path,{signal:context.signal,onRetry:()=>message('accountMessage','Waiting for saved sign-in details. Retrying automatically.')});
 if(!context.isCurrent())return;
 minimum=account.minPasswordLength||session.minPasswordLength;$('accountUsername').value=account.username||'';
 $('accountPasswordHelp').textContent='Leave blank to keep your password. New passwords need '+minimum+'–64 characters.';
 document.querySelectorAll('#accountForm input,#accountForm button').forEach(node=>node.disabled=false);
 $('saveAccount').dataset.blocked='false';message('accountMessage','');received();
 $('newPassword').minLength=minimum;$('repeatPassword').minLength=minimum;
 document.querySelectorAll('#accountForm [data-reveal]').forEach(button=>button.onclick=()=>{const input=$(button.dataset.reveal);const reveal=input.type==='password';input.type=reveal?'text':'password';button.textContent=reveal?'Hide':'Show';button.setAttribute('aria-pressed',String(reveal));});
 $('accountForm').addEventListener('input',()=>{window.sjksDirty=true;message('accountMessage','');});
 startPolling(administrator?'admin/account':'rider/status?view=overview',data=>{if(administrator){if(account.resetPending&&!data.resetPending)message('resetMessage','Data reset completed. Use the initial rider credentials from your vest configuration.',true);account={...account,resetPending:data.resetPending};if(!window.sjksDirty&&!busy){account=data;$('accountUsername').value=data.username||'';}if($('resetData')){$('resetData').dataset.blocked=String(busy||Boolean(data.resetPending));if(data.resetPending)message('resetMessage','Data reset is finishing. This page updates automatically.');}}},{interval:5000});
 if(administrator)mountReset();
 $('accountForm').onsubmit=async event=>{
  event.preventDefault();if(busy||!session.connected)return;
  const username=$('accountUsername').value.trim(),next=$('newPassword').value;
  if(next!==$('repeatPassword').value){message('accountMessage','The new passwords do not match.');$('repeatPassword').focus();return;}
  if(next&&next.length<minimum){message('accountMessage','Use at least '+minimum+' characters.');return;}
  if(username===account.username&&!next){message('accountMessage','Enter a new username or password first.');return;}
  if(!await confirmAction('Save sign-in details?','You will need these details to sign in again.','Save and sign out'))return;
  busy=true;session.writing=true;$('saveAccount').dataset.blocked='true';$('saveAccount').disabled=true;
  try{await api(path,'POST',{username,current:$('currentPassword').value,password:next});if(!context.isCurrent())return;window.sjksDirty=false;$('accountForm').reset();location.replace(administrator?'/admin':'/');}
  catch(error){if(context.isCurrent())message('accountMessage',error.message);}
  finally{if(context.isCurrent()){session.writing=false;busy=false;$('saveAccount').dataset.blocked='false';$('saveAccount').disabled=!session.connected;}}
 };
 function mountReset(){
  const section=document.createElement('section');section.className='panel section-gap';
  section.innerHTML=`<div class="panel-head"><h2>Reset data</h2></div><form id="resetForm"><div class="panel-body"><p>Clear the rider name, emergency contacts, message history, incident records and recent camera events. Rider sign-in returns to the initial credentials from your vest configuration, and rider sessions end.</p><p>Your administrator account, calibration, camera pairing and SD-card recordings are kept. New live events will appear normally. Finish active alerts and camera protection before resetting.</p>${password('resetPassword','Administrator password','current-password')}<div class="field"><label for="resetConfirm">Type RESET to confirm</label><input id="resetConfirm" autocomplete="off" required pattern="RESET"></div><p id="resetMessage" class="form-message" role="status"></p></div><div class="action-footer"><p>This cannot be undone.</p><button id="resetData" class="danger" data-write type="submit">Reset data</button></div></form>`;
  $('pageContent').append(section);
  section.querySelector('[data-reveal]').onclick=event=>{const button=event.currentTarget,input=$('resetPassword'),show=input.type==='password';input.type=show?'text':'password';button.textContent=show?'Hide':'Show';button.setAttribute('aria-pressed',String(show));};
  $('resetData').dataset.blocked=String(Boolean(account.resetPending));$('resetData').disabled=!session.connected||account.resetPending;
  $('resetForm').onsubmit=async event=>{
   event.preventDefault();if(busy||session.writing||!session.connected||account.resetPending)return;
   if($('resetConfirm').value!=='RESET')return;
   if(!await confirmAction('Reset vest data?','The rider profile and saved dashboard records will be cleared. Rider sign-in returns to the initial credentials from your vest configuration. Your administrator login and SD-card recordings are kept.','Reset data'))return;
   busy=true;session.writing=true;$('resetData').disabled=true;$('saveAccount').disabled=true;
   try{const result=await api('admin/reset-data','POST',{current:$('resetPassword').value,confirm:'RESET'});if(!context.isCurrent())return;$('resetForm').reset();account.resetPending=Boolean(result.resetPending);message('resetMessage',account.resetPending?'Reset is saved and finishing. This page updates automatically.':'Data reset completed. Use the initial rider credentials from your vest configuration.',!account.resetPending);}
   catch(error){if(context.isCurrent())message('resetMessage',error.message);}
   finally{if(context.isCurrent()){busy=false;session.writing=false;$('resetPassword').value='';$('resetData').disabled=!session.connected||account.resetPending;$('saveAccount').disabled=!session.connected;}}
  };
 }

}
