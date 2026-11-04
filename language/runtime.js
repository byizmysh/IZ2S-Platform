/* IZ2S interface localization. Source strings and technical identifiers stay intact. */
'use strict';
(() => {
  const languages = [
    {id: 'en', name: 'English', locale: 'en-GB'},
    {id: 'ru', name: 'Русский', locale: 'ru-RU'},
    {id: 'es', name: 'Español', locale: 'es-ES'},
    {id: 'zh', name: '简体中文', locale: 'zh-CN'},
  ];
  const supported = id => languages.some(language => language.id === id);
  let selected;
  try { selected = /(?:^|;\s*)run21-language=(en|ru|es|zh)(?:;|$)/.exec(document.cookie)?.[1] || localStorage.getItem('run21.language'); } catch {}
  const remembered = supported(selected);
  if (!supported(selected)) selected = navigator.language?.toLowerCase().split('-')[0];
  if (!supported(selected)) selected = 'en';
  let catalog = {}, matcher = null;
  const originals = new WeakMap(), attributes = new WeakMap();
  const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  function t(value) {
    const source = String(value ?? '');
    if (Object.hasOwn(catalog, source)) return catalog[source];
    const trimmed = source.trim();
    if (Object.hasOwn(catalog, trimmed)) return source.replace(trimmed, catalog[trimmed]);
    if (!matcher) return source;
    // Longest phrases first, one pass: translations never feed back into matching.
    // Boundaries protect file paths, program names and protocol identifiers.
    const rendered = source.replace(matcher, phrase => catalog[phrase]);
    // A two-character unit fragment must not rename the controller's B button.
    return Object.hasOwn(catalog, ' B')
      ? rendered.replace(/\b(\d+(?:[.,]\d+)?) B\b/g, (_, count) => count + catalog[' B'])
      : rendered;
  }
  function ignored(node) {
    return node.parentElement?.closest('script,style,code,pre,[contenteditable="true"],[translate="no"],.language-picker');
  }
  function text(node) {
    if (ignored(node)) return;
    const current = node.nodeValue, old = originals.get(node);
    if (old && current === old.rendered) return;
    const rendered = t(current);
    originals.set(node, {source: current, rendered});
    if (rendered !== current) node.nodeValue = rendered;
  }
  function attribute(node, name) {
    if (node.closest('script,style,code,pre,[contenteditable="true"],[translate="no"],.language-picker')) return;
    const current = node.getAttribute(name);
    if (current === null) return;
    let saved = attributes.get(node);
    if (!saved) attributes.set(node, saved = new Map());
    const old = saved.get(name);
    if (old && current === old.rendered) return;
    const rendered = t(current);
    saved.set(name, {source: current, rendered});
    if (rendered !== current) node.setAttribute(name, rendered);
  }
  function visit(node) {
    if (node.nodeType === Node.TEXT_NODE) { text(node); return; }
    if (node.nodeType !== Node.ELEMENT_NODE) return;
    if (node.matches('script,style,code,pre,[contenteditable="true"],[translate="no"],.language-picker')) return;
    for (const name of ['title', 'aria-label', 'placeholder', 'alt']) attribute(node, name);
    if (node.matches('input[type="button"],input[type="submit"],input[type="reset"]')) attribute(node, 'value');
    for (const child of node.childNodes) visit(child);
  }
  let languageGeneration = 0;
  function installCatalog(id, messages) {
    selected = id;
    catalog = messages;
    const keys = Object.keys(catalog).filter(key => key.length >= 3).sort((a, b) => b.length - a.length);
    matcher = keys.length ? new RegExp(keys.map(key => (/^[A-Za-z0-9_]/.test(key) ? '(?<![A-Za-z0-9_./-])' : '') + escape(key) + (/[A-Za-z0-9_]$/.test(key) ? '(?![A-Za-z0-9_./-])' : '')).join('|'), 'g') : null;
    document.documentElement.lang = selected === 'zh' ? 'zh-Hans' : selected;
    document.documentElement.dataset.language = selected;
  }
  async function readCatalog(id) {
    const response = await fetch('/language/' + id + '/ui.json');
    if (!response.ok) throw Error('Language catalog unavailable: ' + id);
    return response.json();
  }
  function restore(node) {
    if (node.nodeType === Node.TEXT_NODE) {
      const old = originals.get(node);
      if (old && node.nodeValue === old.rendered) node.nodeValue = old.source;
      originals.delete(node);
    } else if (node.nodeType === Node.ELEMENT_NODE) {
      if (node.matches('script,style,code,pre,[contenteditable="true"],[translate="no"],.language-picker')) return;
      const saved = attributes.get(node);
      if (saved) for (const [name, old] of saved) if (node.getAttribute(name) === old.rendered) node.setAttribute(name, old.source);
      attributes.delete(node);
      for (const child of node.childNodes) restore(child);
    }
  }
  async function setLanguage(id, {persist = true} = {}) {
    if (!supported(id)) return;
    const generation = ++languageGeneration;
    try {
      const messages = await readCatalog(id);
      if (generation !== languageGeneration) return;
      restore(document.documentElement);
      installCatalog(id, messages);
      const picker = document.querySelector('.language-picker');
      const label = {en: 'Language', ru: 'Язык', es: 'Idioma', zh: '语言'}[id];
      if (picker) {picker.querySelector('span').textContent = label; picker.querySelector('select').setAttribute('aria-label', label); picker.querySelector('select').value = id;}
      visit(document.documentElement);
      window.dispatchEvent(new CustomEvent('run21:languagechange', {detail: {language: id}}));
      if(persist)try {
        localStorage.setItem('run21.language', id);
        document.cookie = 'run21-language=' + id + '; Path=/; Max-Age=31536000; SameSite=Strict';
      } catch {}
      if(persist)try {await fetch('/language/preference', {method: 'POST', headers: {'content-type': 'application/json'}, body: JSON.stringify({language: id})});} catch {}
    } catch (error) {console.error(error); const select = document.querySelector('.language-picker select'); if (select) select.value = selected;}
  }
  const api = window.RUN21_LANGUAGE = {t, languages, setLanguage, get id() {return selected;}, get locale() {return languages.find(item => item.id === selected).locale;}};
  // A framed module follows the containing IZ2S window. The handshake carries
  // only a language id; module authentication remains at its own origin.
  window.addEventListener('message',async event=>{
    if(window.parent===window||window.name!=='iz2s-workspace'||event.source!==window.parent||event.data?.type!=='iz2s:workspace-language'||!supported(event.data.language))return;
    let parent;try{parent=new URL(event.origin);}catch{return;}
    if(parent.protocol!=='http:'||parent.hostname!=='127.0.0.1')return;
    await api.ready;document.documentElement.dataset.iz2sEmbedded='true';
    if(selected!==event.data.language)await setLanguage(event.data.language,{persist:false});
  });
  api.ready = (async () => {
    // The native shell and a fresh browser profile share the saved preference.
    if (!remembered) try {
      const response = await fetch('/language/preference', {signal: AbortSignal.timeout(3000)});
      if (response.ok) {
        const preference = await response.json();
        if (supported(preference.language)) selected = preference.language;
      }
    } catch {}
    installCatalog(selected, await readCatalog(selected));
    if (!document.body) await new Promise(resolve => document.addEventListener('DOMContentLoaded', resolve, {once: true}));
    for (const method of ['alert', 'confirm', 'prompt']) {const original = window[method].bind(window); window[method] = (message, ...args) => original(t(message), ...args);}
    const validity = HTMLInputElement.prototype.setCustomValidity;
    HTMLInputElement.prototype.setCustomValidity = function(message) {return validity.call(this, t(message));};
    document.documentElement.lang = selected === 'zh' ? 'zh-Hans' : selected;
    document.documentElement.dataset.language = selected;
    const picker = document.createElement('label');
    picker.className = 'language-picker'; picker.setAttribute('translate', 'no');
    const label = document.createElement('span'); label.textContent = {en: 'Language', ru: 'Язык', es: 'Idioma', zh: '语言'}[selected];
    const select = document.createElement('select'); select.setAttribute('aria-label', label.textContent);
    for (const language of languages) {const option = document.createElement('option'); option.value = language.id; option.textContent = language.name; select.append(option);}
    select.value = selected; select.onchange = () => setLanguage(select.value);
    picker.append(label, select);
    const mount = document.getElementById('language-control');
    if (mount) mount.append(picker); else {picker.classList.add('language-floating'); document.body.prepend(picker);}
    // All independent module pages use the same local stylesheet.
    const link = document.createElement('link'); link.rel = 'stylesheet'; link.href = '/language/style.css'; document.head.append(link);
    visit(document.documentElement);
    new MutationObserver(records => {
      for (const record of records) {
        if (record.type === 'characterData') text(record.target);
        else if (record.type === 'attributes') {
          if (record.attributeName === 'value') visit(record.target);
          else attribute(record.target, record.attributeName);
        }
        else for (const node of record.addedNodes) visit(node);
      }
    }).observe(document.documentElement, {subtree: true, childList: true, characterData: true, attributes: true, attributeFilter: ['title', 'aria-label', 'placeholder', 'alt', 'value']});
    window.addEventListener('focus', () => {const shared = /(?:^|;\s*)run21-language=(en|ru|es|zh)(?:;|$)/.exec(document.cookie)?.[1]; if (shared && shared !== selected) setLanguage(shared);});
    if(window.parent!==window&&window.name==='iz2s-workspace')window.parent.postMessage({type:'iz2s:workspace-ready'},'*');
    return api;
  })().catch(error => {
    // Keep the original English UI usable if a catalog cannot be read.
    console.error(error);
    return api;
  });
})();
