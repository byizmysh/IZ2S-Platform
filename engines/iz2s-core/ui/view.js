'use strict';

// Shared by the server (whole journal) and older API responses (visible tail).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.IZ2S_CORE_DIAGNOSTICS = factory();
})(globalThis, () => {
  const localSocketDenied = /\blisten\s+EACCES\b[^\r\n]*\.sock\b/i;
  const keyboardNotReady = /^(?:Error:\s*)?(?:(?:Switch 1|Cardputer(?: ADV)?|ARM PC) )?USB[- ](?:keyboard (?:is )?not ready|клавиатура не готова|input cable is not configured|keyboard is not configured by a host(?:: [^\r\n]*)?)\.?$/iu;

  function errorKey(message = '', fallback = 'requestError') {
    if (localSocketDenied.test(message)) return 'localSocketDenied';
    if (/FS acquisition/i.test(message)) return 'fsError';
    if (/No console connected before the pairing deadline/i.test(message)) return 'consoleConnectTimeout';
    if (/path.*Received null|Неожиданный каталог|до создания каталога/i.test(message)) return 'pathError';
    if (/port.*in use|порт .+занят/i.test(message)) return 'portBusy';
    if (/No active.*address|нет активного адреса/i.test(message)) return 'noHost';
    if (/\bpair(?:ing)?\b|сопряжени/i.test(message)) return 'pairingError';
    if (/ECONNREFUSED|EHOSTUNREACH|ENETUNREACH|ENOTFOUND|fetch failed|did not answer on Wi-Fi/i.test(message)) return 'inputNetworkError';
    if (/input device|устройство ввода/i.test(message)) return 'chooseInput';
    if (/Выберите.*Cardputer|select Cardputer/i.test(message)) return 'chooseCardputer';
    if (keyboardNotReady.test(String(message).trim())) return 'usbKeyboardNotReady';
    if (/^[Вв]вод:|input job/i.test(message)) return 'inputError';
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
        if (/^(?:Error:|Ввод:|Details:|\s+at )/.test(line) && detailBytes < 16000) {
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
      || errors.find(line => /^(?:Error:|Ввод:)/.test(line)) || attempt?.error || '';
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
  const { t } = globalThis.IZ2S_CORE_MESSAGES;

  const { errorKey, describe } = globalThis.IZ2S_CORE_DIAGNOSTICS;
  const usbKeyboardAdapters = new Set(['switch-wifi', 'cardputer-wifi', 'ssh-gadget']);
  const setupProgress = {
    typing: 1, review: 2, checking: 2, values: 2, checked: 2,
    saving: 3, save_confirmation: 3, saved: 4,
  };

  function launchConfirmed(attempt, diagnostic) {
    return attempt?.kind === 'launch' && attempt.state === 'ready' && diagnostic.confirmed;
  }

  function date(value) {
    return value ? new Date(value).toLocaleString(globalThis.RUN21_LANGUAGE?.locale || 'en-GB', {
      hour12: false,
    }) : '';
  }

  function create({ api, changed, updated = () => {}, openApps = () => {}, openFiles = () => {} }) {
    const el = id => document.getElementById(id);
    const section = el('iz2s-core');
    const originalPosition = document.createComment('IZ2S workflow position');
    section.before(originalPosition);
    let sessionSettings = false, compactAttempt = null;
    const confirmationIds = ['core-fresh', 'core-setup-ready', 'core-reviewed', 'core-lines', 'core-saved-ready'];
    const textNodes = [...section.querySelectorAll('[data-core-text]')];
    const labelNodes = [...section.querySelectorAll('[data-core-label]')];
    let selected = true;
    let mode = localStorage.getItem('iz2s-run25-core-mode') === 'setup' ? 'setup' : 'launch';
    let enabled = false;
    let locked = false;
    let preview = false;
    let waitingForUsbKeyboard = false;
    let networkHost = '';
    let last = null;
    let reading = null;
    let actionPending = false;
    let downloadPending = false;
    let refreshError = null;
    let actionError = null;
    let attemptId = null;
    let newLaunch = false;
    let renderedLog = null;
    let renderedAttempt = null;
    let renderedTimings = null;
    let recovery = null;
    let generation = 0;
    let refreshedAt = null;

    function localize() {
      for (const node of textNodes) node.textContent = t(node.dataset.coreText, { n: node.dataset.n });
      for (const node of labelNodes) node.setAttribute('aria-label', t(node.dataset.coreLabel));
      for (let i = 0; i < 6; i++) {
        el('core-metric-' + i).setAttribute('aria-label', t('metric', {
          n: Math.floor(i / 2) + 1,
          field: t(i % 2 ? 'checksum' : 'chars'),
        }));
      }
    }

    function paintJournal(state, attempt, keyboardRefused) {
      el('core-journal-details').hidden = el('core-log-section').hidden = !attempt && !refreshError;
      el('core-refresh').setAttribute('aria-busy', String(!!reading));
      el('core-refresh').disabled = !!reading || actionPending;
      el('core-log-section').dataset.stale = String(!!refreshError);
      el('core-download').disabled = downloadPending || !state.logBytes;
      el('core-log-meta').textContent = attempt ? t('attempt', { id: attempt.id.slice(0, 8) }) : '';
      const emptyLog = t(keyboardRefused ? 'inputNotStarted' : 'noEvents');
      const stale = state.logUpdatedAt ? Math.floor((Date.now() - Date.parse(state.logUpdatedAt)) / 1000) : 0;
      el('core-log-updated').textContent = state.logUpdatedAt ? [
        t('lastLine', { date: date(state.logUpdatedAt) }),
        state.busy && stale >= 10 ? t('quiet', { seconds: stale }) : '',
      ].filter(Boolean).join(' · ') : emptyLog;
      const log = el('core-log');
      const nextLog = state.log || emptyLog;
      const timings = state.logTimings || [];
      const locale = globalThis.RUN21_LANGUAGE?.locale || 'en-GB';
      const timingStamp = locale + '/' + (timings.at(-1)?.join('/') || '');
      if (nextLog === renderedLog && attempt?.id === renderedAttempt && timingStamp === renderedTimings) return;
      const follow = renderedAttempt !== attempt?.id || log.scrollHeight - log.scrollTop - log.clientHeight < 32;
      const top = log.scrollTop;
      const lines = nextLog.split('\n');
      if (lines.at(-1) === '') lines.pop();
      const rows = document.createDocumentFragment();
      const encoder = new TextEncoder();
      const clock = new Intl.DateTimeFormat(locale, {
        hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3,
      });
      const startedAt = Date.parse(attempt?.startedAt);
      let end = 0, stamp = 0;
      for (const [index, line] of lines.entries()) {
        end += encoder.encode(line).length + (index < lines.length - 1 || nextLog.endsWith('\n') ? 1 : 0);
        while (timings[stamp]?.[0] < end) stamp++;
        const at = timings[stamp]?.[1];
        const row = document.createElement('div');
        const error = /^(?:Error:|Ввод:|\s+at\s)/.test(line);
        row.className = 'log-row ' + (error ? 'error' : /^\d\/4\s/.test(line) ? 'process' : 'detail');
        const time = document.createElement('time');
        time.className = 'core-log-time';
        time.textContent = Number.isFinite(at) ? clock.format(at) : '—';
        if (Number.isFinite(at)) {
          time.dateTime = new Date(at).toISOString();
          time.title = at >= startedAt ? '+' + (at - startedAt) + ' ms' : time.dateTime;
        } else time.title = t('noTimestamp');
        const text = document.createElement('span');
        text.className = 'core-log-line';
        text.setAttribute('translate', 'no');
        text.textContent = line.replace(/\r$/, '') || '\u00a0';
        row.append(time, text);
        rows.append(row);
      }
      log.replaceChildren(rows);
      log.scrollTop = follow ? log.scrollHeight : top;
      renderedLog = nextLog;
      renderedAttempt = attempt?.id;
      renderedTimings = timingStamp;
    }

    function paint() {
      const state = last || {};
      const attempt = state.attempt;
      const setup = attempt?.kind === 'setup';
      const diagnostic = state.diagnostic || describe(state);
      if ((attempt?.id ?? null) !== attemptId) {
        attemptId = attempt?.id ?? null;
        newLaunch = false;
        for (const id of confirmationIds) el(id).checked = false;
        for (let i = 0; i < 6; i++) el('core-metric-' + i).value = '';
        if (actionError?.attemptId !== attemptId) actionError = null;
      }
      const disabled = !last || actionPending || locked || preview || state.busy || !!refreshError;
      const confirmed = launchConfirmed(attempt, diagnostic);
      const pastSuccess = confirmed && newLaunch;
      const session = confirmed && !pastSuccess;
      const compact = enabled && session;
      if (compact && compactAttempt !== attempt.id) {
        compactAttempt = attempt.id;
        sessionSettings = false;
        el('core-journal-details').open = false;
      }
      if (!session) { compactAttempt = null; el('core-journal-details').open = true; }
      el('setup').dataset.coreSession = String(compact);
      el('setup').dataset.coreSettings = String(sessionSettings);
      if (originalPosition.nextSibling !== section) originalPosition.after(section);
      el('core-settings-toggle').setAttribute('aria-expanded', String(sessionSettings));
      el('core-settings-toggle').textContent = t(sessionSettings ? 'hideSettings' : 'showSettings');
      const selectedMode = setup && state.owned ? 'setup' : mode;
      const step = attempt?.state;
      const phase = diagnostic.phase;
      const inFlight = state.busy && !setup;
      section.dataset.state = refreshError ? 'unavailable' : !last ? 'loading' : pastSuccess ? 'idle' : step || 'idle';
      section.setAttribute('aria-busy', String(!last && !!reading));
      el('core-workflow').hidden = inFlight && !session;
      el('core-mode-heading').hidden = el('core-preparation').hidden = session || inFlight;
      el('core-session').hidden = true;
      const readiness = el('core-launch-readiness');
      if (el('core-fresh-confirmation').parentElement !== readiness) readiness.append(el('core-fresh-confirmation'));
      el('core-setup-panel').hidden = selectedMode !== 'setup';
      el('core-launch-panel').hidden = selectedMode !== 'launch';
      const setupStage = setup && !state.historicalAttempt ? setupProgress[step] ?? 0 : 0;
      [...el('core-setup-steps').children].forEach((node, index) => {
        node.dataset.state = index < setupStage ? 'done' : '';
        if (index === setupStage) node.setAttribute('aria-current', 'step');
        else node.removeAttribute('aria-current');
      });
      for (const name of ['setup', 'launch']) {
        const button = el('core-mode-' + name);
        const active = selectedMode === name;
        button.classList.toggle('selected', active);
        button.setAttribute('aria-selected', String(active));
        button.tabIndex = active ? 0 : -1;
        button.disabled = actionPending || !!state.owned;
      }
      el('core-start').disabled = disabled || state.owned || !networkHost || !el('core-fresh').checked;
      el('core-network').textContent = networkHost ? t('host', { host: networkHost }) : t('noHost');
      el('core-enter-source').disabled = disabled || state.owned || waitingForUsbKeyboard || !el('core-setup-ready').checked;
      el('core-usb-keyboard-note').hidden = !waitingForUsbKeyboard || state.owned || preview;
      el('core-slot').disabled = disabled || (setup && state.owned);
      el('core-source-progress').hidden = !setup || !state.busy;
      el('core-source-progress').value = attempt?.progress?.line || 0;
      el('core-source-review').hidden = !setup || !['review', 'values', 'checked'].includes(step);
      el('core-verify').disabled = el('core-save').disabled = disabled || !el('core-reviewed').checked;
      el('core-check-values').hidden = !setup || !['values', 'checked'].includes(step);
      el('core-comparison').textContent = attempt?.comparison ? t(attempt.comparison.passed ? 'match' : 'mismatch') : '';
      el('core-compare').disabled = disabled || !el('core-lines').checked || step !== 'values';
      el('core-save-confirmation').hidden = !setup || step !== 'save_confirmation';
      el('core-confirm-saved').disabled = disabled || !el('core-saved-ready').checked;
      el('core-stop').disabled = actionPending || preview || step === 'stopping';
      el('core-stop').textContent = t(step === 'stopping' ? 'stopping' : 'stop');

      const failure = refreshError || actionError;
      const keyboardRefused = !!actionError && errorKey(actionError.message, actionError.key) === 'usbKeyboardNotReady';
      const inputNeverStarted = keyboardRefused && !state.busy && !state.logBytes && !state.log?.trim();
      const showStatus = !session;
      const showAction = !!failure;
      el('core-attempt-card').hidden = !showStatus && !showAction;
      el('core-attempt-card').dataset.failed = String(step === 'failed' || showAction);
      el('core-current-status').hidden = !showStatus;
      el('core-action-result').hidden = !showAction;
      el('core-action-error').textContent = failure ? t(refreshError ? 'pcError' : errorKey(failure.message, failure.key)) : '';
      el('core-stale-note').hidden = !refreshError;
      el('core-stale-note').textContent = refreshError ? t(refreshedAt ? 'staleSnapshot' : 'noSnapshot', { date: date(refreshedAt) }) : '';
      el('core-action-details').hidden = !failure?.message || (keyboardRefused && !refreshError);
      el('core-action-raw').textContent = failure?.message || '';
      el('core-status-retry').hidden = !refreshError;
      el('core-status-retry').disabled = !!reading || actionPending;
      el('core-restart-row').hidden = selectedMode !== 'setup' || session
        || !(['failed', 'interrupted', 'stopped'].includes(step) || actionError);
      el('core-restart-setup').disabled = disabled || !!state.owned;
      const status = inputNeverStarted ? t('inputNotStarted') : step === 'connected' && phase
        ? t('phaseStatus', { n: phase, name: t('phase' + phase) })
        : t(!last ? refreshError ? 'unavailable' : 'loading' : pastSuccess ? 'lastConfirmed' : step === 'ready' && !confirmed ? 'unconfirmed' : step === 'failed' && attempt.finishedAt ? 'lastFailed' : step || 'idle', {device:attempt?.inputName||'Cardputer ADV'});
      if (el('core-status').textContent !== status) el('core-status').textContent = status;
      el('core-detail').textContent = step === 'failed'
        ? [t(diagnostic.errorKey), attempt.inputSent === false ? t('noInput') : ''].filter(Boolean).join(' ')
        : step === 'connected' ? t('keepOpen') : step === 'waiting' && attempt.host ? t('waitingForConsole', { host: attempt.host }) : '';
      el('core-detail').hidden = !el('core-detail').textContent;
      el('core-stop-row').hidden = !state.owned || session;
      el('core-reboot-note').hidden = !attempt?.needsReboot || session ||
        (selectedMode === 'launch' && el('core-fresh').checked);
      const stages = el('core-stages');
      stages.hidden = setup || !phase;
      [...stages.children].forEach((node, index) => {
        const current = index + 1 === phase;
        node.dataset.state = current && step === 'failed' ? 'failed' : index + 1 < phase || diagnostic.confirmed ? 'done' : '';
        if (current) node.setAttribute('aria-current', 'step');
        else node.removeAttribute('aria-current');
      });

      el('core-session-title').textContent = t(state.historicalAttempt ? 'lastConfirmed' : 'sessionReady');
      el('core-session-detail').textContent = session ? t('pid', { pid: attempt.core.pid }) : '';
      el('core-new-launch').disabled = disabled || !!state.owned;
      const menu=state.pcLink?.menu;
      const menuBox=el('core-menu-update'),menuButton=el('core-menu-apply'),menuVersion=el('core-menu-version'),menuStatus=el('core-menu-status');
      const showMenu=!!(session&&menu?.supported);
      if(menuBox.hidden===showMenu)menuBox.hidden=!showMenu;
      if(showMenu){
        const version=t('menuVersion',{version:menu.active||'—'});
        const processing=['waiting','transferring','verifying','staged','applying'].includes(menu.state);
        const status=t('menuUpdate_'+menu.state,{version:menu.available||'—',stage:menu.interruptedAt||'—',error:menu.error?'0x'+menu.error.toString(16):'—'});
        const label=t(menu.canRetry?'menuRetry':'menuUpdate');
        if(menuVersion.textContent!==version)menuVersion.textContent=version;
        if(menuButton.textContent!==label)menuButton.textContent=label;
        if(menuButton.hidden===!!(menu.canUpdate||menu.canRetry||processing||actionPending))menuButton.hidden=!(menu.canUpdate||menu.canRetry||processing||actionPending);
        menuButton.disabled=preview||actionPending||processing||!(menu.canUpdate||menu.canRetry);
        if(menuStatus.textContent!==status)menuStatus.textContent=status;
        menuStatus.hidden=!status;
      }
      el('core-reset-session').textContent = t('resetSession');
      el('core-reset-session').hidden = !session;
      el('core-reset-session').disabled = preview || actionPending || !!state.busy;
      el('core-close-note').textContent = t(state.historicalAttempt ? 'recordedSession' : state.busy ? 'handoffKeep' : 'handoffDone');
      el('core-restore').disabled = preview || actionPending || state.busy || !!state.pcLink?.online;
      el('core-restore').textContent = t(state.pcLink?.online ? 'restored' : 'restore');
      el('core-restore-note').textContent = '';
      el('core-session-tools').hidden = session;
      el('core-add-app').disabled = preview;
      el('core-files').disabled = preview;
      paintJournal(state, attempt, inputNeverStarted);
    }

    function refresh(force = false) {
      if (!enabled || (actionPending && !force)) return;
      if (reading) return reading;
      const requestedGeneration = generation;
      reading = Promise.resolve().then(() => api('/api/iz2s-core', undefined, { timeoutMs: 5000 }))
        .then(async value => {
          if (requestedGeneration !== generation) return;
          if (!value || typeof value.busy !== 'boolean' || typeof value.owned !== 'boolean' ||
              !Object.hasOwn(value, 'attempt') || typeof value.log !== 'string' ||
              value.attempt && (typeof value.attempt.id !== 'string' || typeof value.attempt.state !== 'string') ||
              value.logAttemptId != null && value.logAttemptId !== value.attempt?.id) {
            throw Error('Invalid IZ2S snapshot: status and journal do not identify the same attempt');
          }
          last = value;
          refreshedAt = new Date().toISOString();
          refreshError = null;
        })
        .catch(error => {
          if (requestedGeneration === generation) refreshError = { key: 'pcError', message: error.message };
        })
        .finally(() => {
          reading = null;
          paint();
          updated();
        });
      paint();
      return reading;
    }

    async function action(route, body) {
      if (actionPending) return;
      actionPending = true;
      generation++;
      actionError = null;
      paint();
      try {
        const result=await api(route.startsWith('/') ? route : '/api/iz2s-core/' + route, body);
        if(result?.warning)actionError={key:'requestError',message:result.warning,attemptId};
        if (route === 'start') el('core-fresh').checked = false;
        if (body.action === 'begin') {
          for (const id of confirmationIds.slice(1)) el(id).checked = false;
          for (let i = 0; i < 6; i++) el('core-metric-' + i).value = '';
        }
        return result;
      } catch (error) {
        actionError = { key: 'requestError', message: error.message, attemptId };
      } finally {
        await reading;
        await refresh(true);
        actionPending = false;
        paint();
      }
    }

    function setup(actionName, fields = {}) {
      return action('setup', { action: actionName, ...fields });
    }
    function chooseMode(value) {
      mode = value;
      try { localStorage.setItem('iz2s-run25-core-mode', mode); } catch {}
      paint();
    }
    el('core-start').onclick = () => action('start', { freshDirect: el('core-fresh').checked });
    el('core-stop').onclick = () => action('stop', {});
    el('core-refresh').onclick = () => refresh();
    el('core-reset-session').onclick = () => {
      if(window.confirm(t('resetPrompt')))void action('/api/iz2s-core/pc/reset',{});
    };
    el('core-menu-apply').onclick = () => action('/api/iz2s-core/pc/menu-update',{retry:!!last?.pcLink?.menu?.canRetry});
    el('core-status-retry').onclick = () => refresh();
    el('core-restart-setup').onclick = async () => {
      if (el('core-restart-row').hidden || el('core-restart-setup').disabled) return;
      const result = await action('/api/iz2s-core/pc/reset', {});
      if (!result?.ok) return;
      for (const id of confirmationIds) el(id).checked = false;
      for (let i = 0; i < 6; i++) el('core-metric-' + i).value = '';
      chooseMode('setup');
      el('core-setup-panel').scrollIntoView({ block: 'nearest' });
      el('core-setup-ready').focus({ preventScroll: true });
    };
    el('core-add-app').onclick = openApps;
    el('core-files').onclick = openFiles;
    el('core-restore').onclick = () => refresh(true);
    el('core-enter-source').onclick = () => setup('begin', { slot: Number(el('core-slot').value), screenReady: el('core-setup-ready').checked });
    el('core-verify').onclick = () => setup('verify', { reviewed: el('core-reviewed').checked });
    el('core-save').onclick = () => setup('save', { reviewed: el('core-reviewed').checked });
    el('core-compare').onclick = () => setup('compare', {
      linesConfirmed: el('core-lines').checked,
      values: Array.from({ length: 6 }, (_, i) => el('core-metric-' + i).value),
    });
    el('core-confirm-saved').onclick = () => setup('saved', { savedConfirmed: el('core-saved-ready').checked });
    for (const id of confirmationIds) el(id).onchange = paint;
    for (const name of ['setup', 'launch']) {
      const button = el('core-mode-' + name);
      button.onclick = () => chooseMode(name);
      button.onkeydown = event => {
        if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
        event.preventDefault();
        const next = event.key === 'Home' ? 'setup' : event.key === 'End' ? 'launch' : name === 'setup' ? 'launch' : 'setup';
        chooseMode(next);
        el('core-mode-' + next).focus();
      };
    }
    el('core-settings-toggle').onclick = () => { sessionSettings = !sessionSettings; paint(); };
    el('core-new-launch').onclick = () => {
      newLaunch = true;
      chooseMode('launch');
    };
    el('core-download').onclick = async () => {
      const id = attemptId;
      if (!id || downloadPending) return;
      downloadPending = true;
      actionError = null;
      paint();
      try {
        const blob = await api('/api/iz2s-core/log?id=' + encodeURIComponent(id), undefined, { responseType: 'blob' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = 'iz2s-core-' + id + '.log';
        link.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
      } catch (error) {
        if (id === attemptId) actionError = { key: 'downloadError', message: error.message, attemptId: id };
      } finally {
        downloadPending = false;
        paint();
      }
    };
    localize();
    window.addEventListener('run21:languagechange', () => {
      localize();
      paint();
      if (enabled) changed();
    });
    const timer = setInterval(refresh, 1500);
    window.addEventListener('pagehide', event => { if (!event.persisted) clearInterval(timer); });

    return {
      refresh,
      current: () => enabled ? { ...last, session:!newLaunch&&launchConfirmed(last?.attempt,last?.diagnostic||describe(last||{})), loading: !last, refreshError } : null,
      selected: () => selected,
      select(value) {
        selected = value;
        localStorage.setItem('iz2s-run25-connection-core', value ? '1' : '0');
        changed();
      },
      render(value, busy, panel) {
        const was = enabled;
        enabled = value;
        locked = busy;
        preview = !!panel?.preview;
        waitingForUsbKeyboard = usbKeyboardAdapters.has(panel?.platform?.configuration?.inputAdapter)
          && panel.checks?.items?.find(row => row.id === 'usb')?.ready !== true;
        recovery = globalThis.IZ2S_PRESENTATION?.connectionMode(panel);
        networkHost = panel?.settings?.consoleHost || panel?.connections?.devices?.server?.address || '';
        el('setup').dataset.core = String(value);
        section.hidden = !value;
        paint();
        if (value && !was) void refresh();
      },
    };
  }

  function rpcAvailable(state) {
    if (!state?.session || state.refreshError) return false;
    if (state.fsRpc) return state.fsRpc.ready === true;
    // Older running panel processes can serve the new UI without dropping SB4.
    // This means retained, not verified: /api/rpc/open verifies before opening.
    return !!state.busy && !!state.attempt?.core?.complete && !state.attempt.finishedAt;
  }
  function deviceRow(row, state) {
    const attempt = state?.attempt;
    if(state?.session&&['server','lan'].includes(row.id)){
      const online=!!state.pcLink?.online, rpc=rpcAvailable(state);
      return {...row,ready:row.id==='server'||online||rpc,pending:row.id==='lan'&&!online&&!rpc,
        coreLabel:row.id==='server'?'IZ2S':online?t('connected'):rpc?t(state.fsRpc?.ready?'rpcReady':'rpcHeld'):t('waitingLauncher'),
        detail:row.id==='server'?t('host',{host:state.pcLink?.pcAddress||attempt.host}):
          online?['IZ2S',state.pcLink.consoleAddress,'Super File Bros.'].filter(Boolean).join(' · '):rpc?t('rpcKeep'):t('sessionReady')};
    }
    if (state?.refreshError || !state?.busy || attempt?.kind !== 'launch') return row;
    let label;
    let detail;
    if (row.id === 'server' && attempt.run) {
      label = 'IZ2S';
      detail = t('deviceServer', { host: attempt.host });
    } else if (row.id === 'usb' && attempt.inputSent) {
      label = detail = t(attempt.inputComplete ? 'deviceTyped' : 'typing', {device:attempt.inputName||'Cardputer ADV'});
    } else if (row.id === 'lan' && ['connected', 'ready'].includes(attempt.state)) {
      const confirmed = !state.historicalAttempt && launchConfirmed(attempt, state.diagnostic || describe(state));
      label = t(confirmed ? 'confirmed' : 'connected');
      detail = t(confirmed ? 'closeSmile' : 'keepOpen');
    } else {
      return row;
    }
    return { ...row, ready: true, pending: false, coreLabel: label, detail };
  }

  function connectionState(state) {
    if (state.pcLink?.online) return ['connected', t('restored')];
    if (state.refreshError) return ['disconnected', t('pcError')];
    if (state.loading) return ['checking', t('loading')];
    if (launchConfirmed(state.attempt, state.diagnostic || describe(state))) {
      if (state.historicalAttempt) return ['idle', t('lastConfirmed')];
      return ['connected', t('confirmed')];
    }
    if (state.attempt?.state === 'connected') return ['connected', t('connected')];
    if (state.attempt?.state === 'stopping') return ['checking', t('stopping')];
    if (state.busy) return ['connecting', t('starting')];
    if (['failed', 'interrupted', 'stopped'].includes(state.attempt?.state)) return ['disconnected', t(state.attempt.state)];
    return ['idle', t('idle')];
  }

  return { create, errorKey, deviceRow, connectionState, rpcAvailable, t };
})();
