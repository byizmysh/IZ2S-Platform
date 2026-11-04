'use strict';
// Only the ADV branch owns these preparatory screens. ARM uses its original wizard.
globalThis.IZ2S_ADV_SETUP = (() => {
  function create({document,changed,openSetup}) {
    const $=id=>document.getElementById(id);
    let phase=1,boot=null,usbConfirmed=false,context=null;
    function select(value){phase=value;if(value<4)usbConfirmed=false;$('adv-usb-ready').checked=false;$('screen-ready').checked=false;changed();}
    $('adv-firmware-next').onclick=()=>select(2);
    $('adv-usb-next').onclick=()=>{if(context?.wifi&&context?.usb&&$('adv-usb-ready').checked){usbConfirmed=true;phase=4;changed();}};
    $('adv-usb-ready').onchange=()=>changed();
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
        if(boot!==identity){boot=identity;usbConfirmed=false;$('adv-usb-ready').checked=false;$('screen-ready').checked=false;phase=3;}
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
      // The network/USB acknowledgements describe the cable, not a USB-host identity claim.
      $('adv-usb-ready').disabled=locked||!context.wifi;
      $('adv-usb-next').disabled=locked||!context.wifi||!context.usb||!$('adv-usb-ready').checked;
      $('adv-wifi-status').textContent=context.wifi?'ADV found on Wi-Fi.':'Waiting for ADV on Wi-Fi.';
      $('cardputer-usb-prompt').textContent=context.wifi
        ?'ADV found on Wi-Fi. Move its USB data cable from this PC to Switch 2.'
        :'Reconnect ADV to local Wi-Fi, then connect its USB cable to Switch 2.';
      $('adv-usb-status').textContent=!context.wifi?'Waiting for ADV on Wi-Fi.':context.usb?'USB host detected. Confirm the cable is connected to Switch 2.':'Waiting for the USB connection to Switch 2.';
    }
    return {render,select,get phase(){return phase;}};
  }
  return {create};
})();
