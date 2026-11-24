'use strict';

// Shared by the server (whole journal) and older API responses (visible tail).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.IZ2S_CORE_DIAGNOSTICS = factory();
})(globalThis, () => {
  const localSocketDenied = /\blisten\s+EACCES\b[^\r\n]*\.sock\b/i;
  const keyboardNotReady = /^(?:Error:\s*)?(?:(?:Switch 1|Cardputer(?: ADV)?|ARM PC) )?USB[- ](?:keyboard (?:is )?not ready|клавиатура не готова|input cable is not configured|keyboard is not configured by a host(?:: [^\r\n]*)?)\.?$/iu;
  const keyboardBusy = /USB[- ](?:keyboard[^\r\n]*already typing|клавиатура[^\r\n]*уже вводит команды)/iu;

  function errorKey(message = '', fallback = 'requestError') {
    if (localSocketDenied.test(message)) return 'localSocketDenied';
    if (/FS acquisition/i.test(message)) return 'fsError';
    if (/No console connected before the pairing deadline/i.test(message)) return 'consoleConnectTimeout';
    if (/path.*Received null|unexpected installer directory|before creating the attempt directory|Неожиданный каталог|до создания каталога/i.test(message)) return 'pathError';
    if (/port.*in use|порт .+занят/i.test(message)) return 'portBusy';
    if (/No active.*address|нет активного адреса/i.test(message)) return 'noHost';
    if (keyboardBusy.test(message)) return 'usbKeyboardBusy';
    if (keyboardNotReady.test(String(message).trim())) return 'usbKeyboardNotReady';
    if (/choose the address shown on your input device/i.test(message)) return 'chooseInputAddress';
    if (/\bpair(?:ing)?\b|сопряжени/i.test(message)) return 'pairingError';
    if (/ECONNREFUSED|EHOSTUNREACH|ENETUNREACH|ENOTFOUND|fetch failed|did not answer on Wi-Fi/i.test(message)) return 'inputNetworkError';
    if (/input device|устройство ввода/i.test(message)) return 'chooseInput';
    if (/Выберите.*Cardputer|select Cardputer/i.test(message)) return 'chooseCardputer';
    if (/^(?:Ввод|Input):|input job/i.test(message)) return 'inputError';
    if (/Cardputer|keyboard|USB/i.test(message)) return 'deviceError';
    if (/панели|panel session/i.test(message) && /сеанс|session/i.test(message)) return 'panelBusy';
    if (/уже|завершения текущего ввода|already active/i.test(message)) return 'busyError';
    if (/подтвердите|проверьте 229/i.test(message)) return 'confirmScreen';
    if (/First Setup|slot|Bootstrap/i.test(message)) return 'setupError';
    if (/предварительного просмотр|предварительном просмотр|preview/i.test(message)) return 'previewError';
    if (/core-startup|до подтверждения IZ2S|before RO frontend/i.test(message)) return 'receiptError';
    return fallback;
  }

  function scanner() {
    const result = { phase: 0, connected: false, inputUpdates: 0, errors: [] };
    let detailBytes = 0;
    return {
      line(line) {
        if (line.startsWith('[input]')) result.inputUpdates++;
        if (line.startsWith('Connected.')) result.connected = true;
        const phase = /^([1-4])\/4\s/.exec(line);
        if (phase) result.phase = Number(phase[1]);
        if (/^(?:Error:|Input:|Ввод:|Details:|\s+at )/.test(line) && detailBytes < 16000) {
          const detail = line.slice(0, 16000 - detailBytes);
          result.errors.push(detail);
          detailBytes += detail.length + 1;
        }
      },
      result,
    };
  }

  function describe(state) {
    const attempt = state.attempt;
    let evidence = state.logEvidence;
    if (!evidence) {
      const scan = scanner();
      for (const line of (state.log || '').split(/\r?\n/)) scan.line(line);
      evidence = scan.result;
    }
    const errors = [...(evidence.errors || [])];
    if (attempt?.error && !errors.includes(attempt.error)) errors.push(attempt.error);
    const primary = errors.find(line => localSocketDenied.test(line))
      || errors.find(line => /^(?:Error:|Input:|Ввод:)/.test(line)) || attempt?.error || '';
    const phase = Number(attempt?.phase) || evidence.phase || 0;
    return {
      ...evidence,
      phase: phase >= 1 && phase <= 4 ? phase : 0,
      confirmed: attempt?.core?.complete === true && (attempt.core.result == null || attempt.core.result === 0) && !!attempt.core.pid,
      errorKey: errorKey(primary, 'attemptError'),
      technicalReason: errors.join('\n'),
    };
  }

  return { scanner, describe, errorKey };
});

if (!(typeof module === 'object' && module.exports)) globalThis.IZ2S_CORE = (() => {
 const {t}=globalThis.IZ2S_CORE_MESSAGES;
 function create({api,updated=()=>{}}){
  let enabled=false,preview=false,last=null,refreshError=null,request=null;
  async function refresh(){
   if(!enabled||preview)return;
   if(request)return request;
   request=api('/api/iz2s-core',undefined,{timeoutMs:10000}).then(value=>{last=value;refreshError=null;},error=>{refreshError=error;}).finally(()=>{request=null;updated();});
   return request;
  }
  const timer=setInterval(refresh,1500);
  window.addEventListener('pagehide',event=>{if(!event.persisted)clearInterval(timer);});
  return {refresh,current:()=>enabled?{...last,session:!!last?.pcLink?.online&&!refreshError,loading:!last,refreshError}:null,
   selected:()=>false,render(value,busy,panel){const was=enabled;enabled=value;preview=!!panel?.preview;if(enabled&&!was)void refresh();}};
 }
 return {create,t,rpcAvailable:()=>false,deviceRow:row=>row};
})();
