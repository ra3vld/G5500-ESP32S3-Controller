// Report actual input, including navigation and edits that do not send commands.
(()=>{
 let pending=false,next=0;
 function activity(event){
  if(!event.isTrusted||document.hidden||pending||Date.now()<next)return;
  // These buttons already confirm explicitly; avoid racing their POST request.
  if(event.target.closest?.('#confirm,#hardwareTrialConfirm'))return;
  pending=true;
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),3000);
  fetch('/api/hardware/activity',{method:'POST',signal:controller.signal})
   .then(response=>{if(response.ok)next=Date.now()+10000;})
   .catch(()=>{}).finally(()=>{clearTimeout(timer);pending=false;});
 }
 for(const type of ['pointerdown','click','keydown','input','change','wheel'])
  document.addEventListener(type,activity,{capture:true,passive:true});
})();
