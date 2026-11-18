'use strict';
// Device preparation belongs to Connection settings, independently of console sessions.
globalThis.IZ2S_ADV_SETUP = (() => {
  function usbDescription(wifi, usb) {
    if (!wifi?.ready) return 'USB status unavailable until ADV reconnects.';
    if (usb?.usb?.localHost === 'this-pc') return 'ADV USB is connected to this PC. Move its USB data cable to Switch 2.';
    if (usb?.usb?.suspended) return 'USB inactive. Wake the console or check the cable.';
    if (usb?.ready) return 'USB connected · keyboard ready.';
    return usb?.detail || 'Connect the ADV USB data cable to Switch 2.';
  }
  function usbPresentation(wifi,usb,confirmed=false) {
    const telemetry = usb?.usb;
    const acknowledged = confirmed || telemetry?.confirmed === true || telemetry?.confirmation > 0;
    const waiting = acknowledged && wifi?.ready && !usb?.ready && telemetry?.localHost !== 'this-pc'
      && !telemetry?.suspended && !telemetry?.recovering;
    return {
      label: waiting ? '○ WAITING' : null,
      detail: waiting ? 'Connection confirmed. Waiting for the USB host before typing.' : null,
    };
  }
  function create({document,changed,confirmUsb=async()=>({ok:true}),refreshUsb=async()=>{}}) {
    const $=id=>document.getElementById(id);
    const confirmationButton = $('adv-usb-next');
    let phase=1,boot=null,usbIdentity=null,usbConfirmed=false,context=null;
    let previousUsb=null,confirming=false;
    let switchConfirmed=false,switchProfile=null,switchWasReady=false;
    function select(value){phase=value;changed();}
    function renderUsbTile(state) {
      const card = $('device-usb');
      if (!card) return;
      const rows = state?.checks?.items || [];
      // Manual cable acknowledgement can only supply a waiting hint.
      const presentation = context && context.state === state
        ? usbPresentation(rows.find(row=>row.id==='arm'),rows.find(row=>row.id==='usb'),
          context.adapter === 'switch-wifi' ? switchConfirmed : usbConfirmed)
        : null;
      if (!presentation?.label) return;
      const t = text => globalThis.RUN21_LANGUAGE?.t(text) || text;
      card.querySelector('.state').textContent = t(presentation.label);
      card.querySelector('.detail-text').textContent = t(presentation.detail);
      card.querySelector('.detail-help').hidden = true;
    }
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
    confirmationButton.onclick=async()=>{
      if(!context||context.locked||context.blocked||confirming)return;
      // This records the user's cable choice immediately. Device synchronization
      // and measured keyboard readiness are separate from a manual acknowledgement.
      const {adapter,wifi}=context;
      if(adapter==='switch-wifi')switchConfirmed=true;
      else usbConfirmed=true;
      confirming=true;changed();
      try {if(wifi)await (adapter==='switch-wifi'?refreshUsb():confirmUsb());}
      finally {confirming=false;changed();}
    };
    function render({state,busy=false,serverUnavailable=false,manual=false,preparation=null}) {
      const adapter=state?.platform?.configuration?.inputAdapter;
      const showHardware=!manual&&state?.platform?.loaderId!=='civswitch-ldn';
      const adv=showHardware&&adapter==='cardputer-wifi';
      const switchKeyboard=showHardware&&adapter==='switch-wifi';
      $('cardputer-setup').hidden=!adv;
      confirmationButton.hidden=!adv&&!switchKeyboard;
      if(!switchKeyboard){switchConfirmed=false;switchProfile=null;switchWasReady=false;}
      if(!adv&&!switchKeyboard){
        context=null;
        renderUsbTile(state);
        return;
      }
      const wifi=state?.checks?.items?.find(row=>row.id==='arm'),usb=state?.checks?.items?.find(row=>row.id==='usb');
      const telemetry=usb?.usb;
      const locked=busy||serverUnavailable||!!state?.platform?.locked||!!state?.preview;
      context={state,adapter,wifi:!!wifi?.ready,usb:!!usb?.ready,locked,blocked:telemetry?.localHost==='this-pc'};
      if(switchKeyboard){
        const id=state.platform.configurationId;
        const identity=id+':'+(state.platform.profiles?.[id]?.switchAddress||'');
        const disconnected=context.wifi&&switchWasReady&&!context.usb;
        if(switchProfile!==identity||context.blocked||disconnected){
          switchConfirmed=false;
          if(switchProfile!==null)$('screen-ready').checked=false;
        }
        switchProfile=identity;
        // The Switch 1 app reports keyboard readiness without a cable generation.
        if(context.wifi)switchWasReady=context.usb;
      } else if(context.wifi){
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
      confirmationButton.disabled=locked||confirming||context.blocked;
      confirmationButton.textContent=(switchKeyboard?switchConfirmed:usbConfirmed)?'Confirm again':'Confirm connection';
      renderUsbTile(state);
      if(!adv)return;
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
      $('adv-usb-pending').hidden=!usbConfirmed||context.usb;
      $('adv-device-status').textContent=context.wifi?'Firmware '+(wifi.firmwareVersion||'· ADV connected'):wifi?.networkReachable?'Pairing required':'ADV offline';
      $('adv-wifi-status').textContent=context.wifi?'ADV found on Wi-Fi.':wifi?.networkReachable?'Device found on the network. Pairing key is missing or was not accepted.':'Waiting for ADV on Wi-Fi.';
      const description=usbDescription(wifi,usb);
      $('cardputer-usb-prompt').textContent=description;
      $('adv-usb-status').textContent=context.usb?'USB connected · keyboard ready.':usbConfirmed
        ?'Connection confirmed. Waiting for the USB host before typing.'
        :context.wifi&&!context.blocked
          ?telemetry?.confirmationSupported
            ?'Press Enter on ADV or Confirm connection in the device card.'
            :'Use Confirm connection in the device card. Update ADV firmware to confirm with Enter on the device.'
          :description;
    }
    return {render,renderUsbTile,select,get phase(){return phase;},get confirmed(){return usbConfirmed;},
      get switchConfirmed(){return switchConfirmed;}};
  }
  return {create,usbDescription,usbPresentation};
})();
