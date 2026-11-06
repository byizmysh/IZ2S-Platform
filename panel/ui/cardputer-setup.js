'use strict';
// Only the ADV branch owns these preparatory screens. ARM uses its original wizard.
globalThis.IZ2S_ADV_SETUP = (() => {
  function usbDescription(state, wifi, usb) {
    if (!wifi?.ready) return 'USB status unavailable until ADV reconnects.';
    if (usb?.usb?.localHost === 'this-pc') return 'ADV USB is connected to this PC. Move its USB data cable to Switch 2.';
    if (usb?.usb?.suspended) return 'USB inactive. Wake the console or check the cable.';
    if (usb?.ready) return 'USB connected · keyboard ready.';
    return usb?.detail || 'Connect the ADV USB data cable to Switch 2.';
  }
  function create({document,changed,openSetup}) {
    const $=id=>document.getElementById(id);
    let phase=1,boot=null,usbIdentity=null,usbConfirmed=false,context=null;
    function select(value){phase=value;if(value<4)usbConfirmed=false;$('screen-ready').checked=false;changed();}
    $('adv-firmware-next').onclick=()=>select(2);
    $('adv-usb-next').onclick=()=>{if(context?.wifi&&context?.usb){usbConfirmed=true;phase=4;changed();}};
    $('adv-setup-back').onclick=()=>select(context?.wifi?3:2);
    $('adv-open-setup').onclick=()=>{phase=context?.wifi?3:1;openSetup();};
    for(const button of document.querySelectorAll('[data-adv-back]'))button.onclick=()=>select(Number(button.dataset.advBack));
    function render({state,step,busy=false,serverUnavailable=false,manual=false}) {
      const enabled=!manual&&state?.platform?.configuration?.inputAdapter==='cardputer-wifi';
      const setup=state?.active?.kind==='bootstrap'?state.active.setup:null;
      const shown=enabled&&(step==='bootstrap'||!!setup);
      const wifi=state?.checks?.items?.find(row=>row.id==='arm'),usb=state?.checks?.items?.find(row=>row.id==='usb');
      context={wifi:!!wifi?.ready,usb:!!usb?.ready};
      if(enabled&&wifi?.ready){
        const identity=wifi.bootId||'detected';
        if(boot!==identity){boot=identity;usbConfirmed=false;$('screen-ready').checked=false;phase=3;}
        const generation=usb?.usb?.generation;
        if(Number.isSafeInteger(generation)){
          const next=identity+':'+generation;
          if(usbIdentity!==null&&usbIdentity!==next){usbConfirmed=false;$('screen-ready').checked=false;}
          usbIdentity=next;
        }
        if(usb?.usb?.connected===false||usb?.usb?.localHost==='this-pc'){
          usbConfirmed=false;$('screen-ready').checked=false;
        }
      }
      if(setup&&enabled)phase=setup.stage==='reboot'?5:4;
      else if(enabled&&state?.active?.kind==='launch')phase=6;
      else if(enabled&&!state?.active&&phase>=5)phase=usbConfirmed&&context.wifi&&context.usb?4:context.wifi?3:1;
      else if(enabled&&phase===4&&!usbConfirmed)phase=context.wifi?3:2;
      $('cardputer-setup').hidden=!shown;
      $('adv-launch-setup').hidden=!enabled||shown||!!state?.active;
      document.getElementById('iz-flow').classList.toggle('adv-preparation',shown&&phase<4);
      if(!enabled)return;
      if(shown&&phase<4)$('installation-options').hidden=true;
      for(const item of document.querySelectorAll('[data-adv-phase]')){
        const index=Number(item.dataset.advPhase);
        item.classList.toggle('current',index===phase);item.classList.toggle('done',index<phase);
        if(index===phase)item.setAttribute('aria-current','step');else item.removeAttribute('aria-current');
      }
      for(const panel of document.querySelectorAll('[data-adv-panel]'))panel.hidden=Number(panel.dataset.advPanel)!==phase;
      $('adv-setup-back').hidden=phase!==4||!!setup;
      const locked=busy||serverUnavailable||!!state.platform.locked||!!state.preview;
      for(const button of $('cardputer-setup').querySelectorAll('button'))button.disabled=locked;
      // USB readiness comes from the firmware, independently of a console network session.
      $('adv-usb-next').disabled=locked||!context.wifi||!context.usb;
      $('adv-wifi-status').textContent=context.wifi?'ADV found on Wi-Fi.':'Waiting for ADV on Wi-Fi.';
      const description=usbDescription(state,wifi,usb);
      $('cardputer-usb-prompt').textContent=description;
      $('adv-usb-status').textContent=context.usb
        ?'Detected automatically over Wi-Fi. Open SmileBASIC Direct, then continue.'
        :description;
    }
    return {render,select,get phase(){return phase;}};
  }
  return {create,usbDescription};
})();
