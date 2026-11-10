'use strict';
// Device preparation belongs to Connection settings, independently of console sessions.
globalThis.IZ2S_ADV_SETUP = (() => {
  function usbDescription(state, wifi, usb) {
    if (!wifi?.ready) return 'USB status unavailable until ADV reconnects.';
    if (usb?.usb?.localHost === 'this-pc') return 'ADV USB is connected to this PC. Move its USB data cable to Switch 2.';
    if (usb?.usb?.suspended) return 'USB inactive. Wake the console or check the cable.';
    if (usb?.ready) return 'USB connected · keyboard ready.';
    return usb?.detail || 'Connect the ADV USB data cable to Switch 2.';
  }
  function usbPresentation(wifi,usb,confirmed=false) {
    const manual=usb?.usb?.localHost!=='this-pc'&&(confirmed||!!wifi?.ready&&(usb?.usb?.confirmed===true||usb?.usb?.confirmation>0));
    return {manual,label:manual?'Connected (manual)':null,
      detail:manual?'USB connection confirmed manually.':null};
  }
  function create({document,changed,confirmUsb=async()=>({ok:true})}) {
    const $=id=>document.getElementById(id);
    let phase=1,boot=null,usbIdentity=null,usbConfirmed=false,context=null;
    let previousUsb=null,confirming=false;
    function select(value){phase=value;changed();}
    for(const tab of document.querySelectorAll('[data-adv-phase]'))tab.onclick=()=>select(Number(tab.dataset.advPhase));
    $('adv-steps').addEventListener('keydown',event=>{
      if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
      const tabs=[...document.querySelectorAll('[data-adv-phase]')],index=tabs.indexOf(document.activeElement);
      if(index<0)return;
      event.preventDefault();
      const next=event.key==='Home'?0:event.key==='End'?tabs.length-1:(index+(event.key==='ArrowRight'?1:-1)+tabs.length)%tabs.length;
      tabs[next].click();tabs[next].focus();
    });
    $('adv-firmware-next').onclick=()=>select(2);
    $('adv-usb-next').onclick=async()=>{
      if(!context||context.locked||context.blocked||confirming)return;
      // This records the user's cable choice immediately. Device synchronization
      // and measured keyboard readiness are separate from a manual acknowledgement.
      usbConfirmed=true;confirming=true;changed();
      try {if(context.wifi)await confirmUsb();}
      finally {confirming=false;changed();}
    };
    function render({state,busy=false,serverUnavailable=false,manual=false,preparation=null}) {
      const enabled=!manual&&state?.platform?.configuration?.inputAdapter==='cardputer-wifi';
      $('cardputer-setup').hidden=!enabled;
      $('adv-usb-next').hidden=!enabled;
      if(!enabled)return;
      const wifi=state?.checks?.items?.find(row=>row.id==='arm'),usb=state?.checks?.items?.find(row=>row.id==='usb');
      const telemetry=usb?.usb;
      const locked=busy||serverUnavailable||!!state?.platform?.locked||!!state?.preview;
      context={wifi:!!wifi?.ready,usb:!!usb?.ready,locked,blocked:telemetry?.localHost==='this-pc'};
      if(context.wifi){
        const identity=wifi.bootId||'detected';
        if(boot!==identity){if(boot!==null)usbConfirmed=false;boot=identity;previousUsb=null;usbIdentity=null;$('screen-ready').checked=false;}
        const generation=telemetry?.generation;
        if(Number.isSafeInteger(generation)){
          const next=identity+':'+generation;
          if(usbIdentity!==null&&usbIdentity!==next)$('screen-ready').checked=false;
          usbIdentity=next;
        }
        const disconnected=telemetry?.connected===false&&!telemetry.recovering&&previousUsb?.connected&&!previousUsb.recovering;
        if(disconnected)$('screen-ready').checked=false;
        if(context.blocked){usbConfirmed=false;$('screen-ready').checked=false;}
        // A recorded Enter press remains a manual choice for this boot, including
        // delayed software detach events that clear the firmware's transient flag.
        if(!context.blocked&&(telemetry?.confirmed||telemetry?.confirmation>0))usbConfirmed=true;
        previousUsb=telemetry;
      }
      for(const tab of document.querySelectorAll('[data-adv-phase]')){
        const selected=Number(tab.dataset.advPhase)===phase;
        tab.classList.toggle('selected',selected);tab.setAttribute('aria-selected',String(selected));tab.tabIndex=selected?0:-1;
      }
      for(const panel of document.querySelectorAll('[data-adv-panel]'))panel.hidden=Number(panel.dataset.advPanel)!==phase;
      // Navigation stays available while disconnected or busy; device writes remain locked.
      for(const button of $('cardputer-setup').querySelectorAll('.adv-stage button'))button.disabled=locked;
      const cancellable=state?.platform?.preparationCancelAvailable===true;
      $('cardputer-install').disabled=locked||!cancellable;
      $('cardputer-file').disabled=locked||!cancellable;
      if(!cancellable&&!preparation)$('cardputer-setup-result').textContent='Restart the panel to enable preparation cancellation.';
      $('cardputer-cancel').hidden=!preparation;
      $('cardputer-cancel').disabled=!preparation||preparation.stopping||serverUnavailable||!cancellable;
      $('cardputer-cancel').textContent=preparation?.stopping?'Cancelling…':'Cancel';
      $('adv-firmware-next').disabled=false;
      $('adv-usb-next').disabled=locked||confirming||context.blocked;
      $('adv-usb-next').textContent=usbConfirmed?'Confirm again':'Confirm connection';
      const presentation=usbPresentation(wifi,usb,usbConfirmed),card=$('device-usb');
      if(card){
        card.classList.toggle('manual-confirmed',presentation.manual);
        if(presentation.manual){
          card.querySelector('.state').textContent=presentation.label;
          card.querySelector('.detail-text').textContent=presentation.detail;
          card.querySelector('.detail-help').hidden=true;
        }
      }
      $('adv-usb-pending').hidden=!usbConfirmed||context.usb;
      $('adv-device-status').textContent=context.wifi?'Firmware '+(wifi.firmwareVersion||'· ADV connected'):'ADV offline';
      $('adv-wifi-status').textContent=context.wifi?'ADV found on Wi-Fi.':'Waiting for ADV on Wi-Fi.';
      const description=usbDescription(state,wifi,usb);
      $('cardputer-usb-prompt').textContent=description;
      $('adv-usb-status').textContent=context.usb?'USB connected · keyboard ready.':usbConfirmed
        ?'Connection confirmed. Waiting for the USB host before typing.'
        :context.wifi&&!context.blocked
          ?telemetry?.confirmationSupported
            ?'Press Enter on ADV or Confirm connection in the device card.'
            :'Use Confirm connection in the device card. Update ADV firmware to confirm with Enter on the device.'
          :description;
    }
    return {render,select,get phase(){return phase;},get confirmed(){return usbConfirmed;}};
  }
  return {create,usbDescription,usbPresentation};
})();
