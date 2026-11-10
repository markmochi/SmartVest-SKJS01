// Only reviewed, actionable form messages cross into the rider interface.
// Unknown device errors remain available to the developer role.
export function friendlyError(status,path,raw=''){
 if(path==='login'&&status===429&&/sign-in is already/.test(raw))return 'Another sign-in is being checked. Please wait a moment.';
 if(path==='login'&&status===409)return 'Sign-in details changed. Please sign in again.';
 if(status===429)return 'Please wait a minute before trying again.';
 if(status===401)return path==='login'?'Incorrect username or password.':'Your session has ended. Sign in again.';
 if(path==='login'&&status>=500)return 'Sign-in is temporarily unavailable. Wait a moment and try again.';
 if(/current.*password.*(incorrect|correct)/i.test(raw))return 'Your current password is not correct.';
 if(/Settings changed elsewhere/.test(raw))return 'Your saved contacts changed. Choose Restore saved, then try again.';
 if(/message is being sent/i.test(raw))return 'An alert is being sent. Wait for it to finish before saving changes.';
 if(/queue.*full/i.test(raw))return 'Your vest has messages waiting to send. Try saving again shortly.';
 if(/mobile number can only/.test(raw))return 'Each mobile number can only be added once.';
 if(/Every contact needs/.test(raw))return 'Each contact needs a name and a valid Philippine mobile number (09… or +639…).';
 if(/Rider name:/.test(raw))return 'Use 1–40 English letters, numbers, spaces, apostrophes, dots or hyphens for your name.';
 if(/Add between one/.test(raw))return 'Add between one and five emergency contacts.';
 if(/3-32 letters/.test(raw))return 'Use 3–32 letters, numbers, dots, hyphens or underscores for your username.';
 if(/private password/.test(raw))return 'Choose a private password of 12–64 characters, different from the original password.';
 if(/Enter a new username/.test(raw))return 'Enter a new username or password first.';
 if(path==='rider/calibration'&&status===409){if(/upright position first/.test(raw))return 'Save your upright position first.';if(/upright and forward/.test(raw))return 'Save your upright and forward positions first.';if(/finishing another action/.test(raw))return 'Your vest is busy. Wait a moment and try again.';return 'Your position is not updating. Check your vest connection and try again.';}
 if(path==='messages/cancel'){
  if(status===409)return /handed to the mobile network/.test(raw)?'This text has already been handed to the mobile network and cannot be cancelled. Check its send result.':'This text is no longer waiting to send. Refresh to see its result.';
  if(status>=500)return 'The cancellation could not be saved. Check the updated queue before trying again.';
 }
 if(path.startsWith('cameras/files')){
  if(status===403&&/another session/.test(raw))return 'Only the person downloading or an administrator can cancel this transfer.';
  if(status===409){
   if(/One clip can download/.test(raw))return 'One clip can download at a time. Wait for it to finish.';
   if(/ready shortly/.test(raw))return 'Camera downloads will be ready shortly. Try again.';
   if(/already ended/.test(raw))return 'This download has already ended.';
   if(/camera request to finish/.test(raw))return 'Wait for the camera request to finish, then try again.';
   if(/incident protection/.test(raw))return 'Your vest is protecting an incident recording. Wait until it finishes before managing clips.';
   if(/another request/.test(raw))return 'This camera is busy with another request. Try again shortly.';
   if(/updated camera/.test(raw))return 'The camera needs a current connection, a working memory card and the updated firmware.';
   return 'This camera request has expired. Update the list before trying again.';
  }
  if(status>=500)return 'Your vest could not confirm this camera request. Update the list before trying again.';
 }
 if(status===403)return 'This action is unavailable. Refresh the page and sign in again.';
 if(status===400)return 'Check the details you entered and try again.';
 if(status>=500)return path==='rider/status'?'Equipment information is temporarily unavailable. Trying again.':'Your vest could not complete this request. Try again. If you were saving changes, check the saved details first.';
 return 'This request could not be completed. Refresh the page and try again.';
}
