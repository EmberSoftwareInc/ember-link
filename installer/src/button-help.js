// One shared photo stays reachable by hover, keyboard, and touch.
const photo=document.getElementById('button-photo');
const triggers=[...document.querySelectorAll('[data-button-help]')];
let active=null,pinned=false,hideTimer;
function position(){
  if(!active||!photo.matches(':popover-open'))return;
  const rect=active.getBoundingClientRect(),width=photo.offsetWidth,height=photo.offsetHeight;
  const left=Math.max(16,Math.min(rect.left,innerWidth-width-16));
  const below=rect.bottom+8;
  const top=below+height<=innerHeight-16?below:Math.max(16,rect.top-height-8);
  photo.style.left=`${left}px`;photo.style.top=`${top}px`;
}
function close(){clearTimeout(hideTimer);if(photo.matches(':popover-open'))photo.hidePopover();}
function show(trigger){
  clearTimeout(hideTimer);active=trigger;
  for(const item of triggers)item.setAttribute('aria-expanded',String(item===active));
  if(!photo.matches(':popover-open'))photo.showPopover();
  position();
}
function leave(){
  clearTimeout(hideTimer);
  hideTimer=setTimeout(()=>{
    if(!pinned&&!photo.matches(':hover')&&!active?.matches(':hover')&&
       !photo.contains(document.activeElement)&&document.activeElement!==active)close();
  },180);
}
for(const trigger of triggers){
  trigger.addEventListener('pointerenter',event=>{if(event.pointerType==='mouse'&&!pinned)show(trigger);});
  trigger.addEventListener('pointerleave',leave);
  trigger.addEventListener('focus',()=>{if(!pinned)show(trigger);});
  trigger.addEventListener('blur',leave);
  trigger.addEventListener('click',()=>{
    if(pinned&&active===trigger){close();return;}
    pinned=true;show(trigger);
  });
}
photo.addEventListener('pointerenter',()=>clearTimeout(hideTimer));
photo.addEventListener('pointerleave',leave);
photo.addEventListener('focusout',leave);
photo.querySelector('.photo-close').addEventListener('click',close);
photo.addEventListener('toggle',event=>{
  if(event.newState==='closed'){
    pinned=false;active=null;for(const item of triggers)item.setAttribute('aria-expanded','false');
  }
});
photo.querySelector('img').addEventListener('load',position);
window.addEventListener('resize',position);
window.addEventListener('scroll',position,true);
