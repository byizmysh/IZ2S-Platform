'use strict';
// This small guard runs before the deferred application scripts. A broken bundle
// must show a retry action even when application initialization never reaches poll().
(() => {
  let failed = false;
  function showFailure() {
    if (!failed) return;
    const startup = document.getElementById('panel-startup');
    if (!startup || startup.hidden) return;
    const message = document.getElementById('startup-message');
    const retry = document.getElementById('startup-retry');
    const t = text => globalThis.RUN21_LANGUAGE?.t(text) || text;
    message.textContent = t('The interface could not start. Reload the window.');
    retry.textContent = t('Reload window');
    retry.hidden = false;
    retry.disabled = false;
    retry.onclick = () => location.reload();
  }
  window.addEventListener('error', event => {
    if (event.error || event.target?.tagName === 'SCRIPT') {
      failed = true;
      showFailure();
    }
  }, true);
  window.addEventListener('unhandledrejection', () => {
    failed = true;
    showFailure();
  });
  document.addEventListener('DOMContentLoaded', showFailure);
})();
