// BEGIN ERROR TEXT
/* Canonical English diagnostics; localized UI labels never become log sources. */
'use strict';
(function(root){
 if(root.IZ2S_ERRORS){if(typeof module==='object'&&module.exports)module.exports=root.IZ2S_ERRORS;return;}
 let dictionary={},matcher;
 const cyrillic=/[\u0400-\u04ff]/;
 function configure(messages={}){
  const candidates=new Map();
  for(const [english,translated] of Object.entries(messages)){
   if(!cyrillic.test(english)&&cyrillic.test(translated)&&translated.length>=3&&!candidates.has(translated))candidates.set(translated,english);
  }
  dictionary=Object.fromEntries(candidates);const escape=s=>s.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
  const keys=[...candidates.keys()].sort((a,b)=>b.length-a.length);
  matcher=keys.length?new RegExp(keys.map(escape).join('|'),'g'):null;
 }
 function canonical(value){
  const source=String(value?.message??value??'');
  if(!cyrillic.test(source))return source;
  // Filenames/paths remain data, even when they contain non-English characters.
  const protectedParts=[];
  let text=source.replace(/(?:[A-Za-z]:[\\/]|\/)[^\s"'<>]+|`[^`]*`/g,part=>{protectedParts.push(part);return '\u0001'+(protectedParts.length-1)+'\u0002';});
  if(matcher)text=text.replace(matcher,p=>dictionary[p]);
  const legacy={'\u041e\u0448\u0438\u0431\u043a\u0430':'Error','\u043e\u0448\u0438\u0431\u043a\u0430':'error','\u041f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u043a':'Restart','\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435':'Connection','\u043a\u043e\u043d\u0441\u043e\u043b\u044c':'console','\u041a\u043e\u043d\u0441\u043e\u043b\u044c':'Console','\u0424\u0430\u0439\u043b':'File','\u0444\u0430\u0439\u043b\u043e\u0432':'files','\u043e\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d\u043e':'stopped','\u0432\u044b\u0431\u0440\u0430\u043d\u043e':'selected','\u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u043e':'completed','\u0412\u0432\u043e\u0434 \u043e\u0442\u043c\u0435\u043d\u0451\u043d. \u041f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u0442\u0438\u0442\u0435 \u043a\u043e\u043d\u0441\u043e\u043b\u044c \u043f\u0435\u0440\u0435\u0434 \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0435\u0439 \u043f\u043e\u043f\u044b\u0442\u043a\u043e\u0439.':'Input was cancelled. Restart the console before another attempt.'};
  for(const [ru,en]of Object.entries(legacy).sort((a,b)=>b[0].length-a[0].length))text=text.split(ru).join(en);
  // Unknown legacy prose is kept in the original downloadable log, never
  // spliced into an English/Spanish/Chinese interface or a native stack trace.
  text=text.split('\n').map(line=>cyrillic.test(line)?'Original diagnostic is available in the full log.':line).join('\n');
  return text.replace(/\u0001(\d+)\u0002/g,(_,i)=>protectedParts[Number(i)]);
 }
 function describe(value){
  const detail=canonical(value),s=(String(value?.message??value??'')+' '+detail).toLowerCase();let title,action;
  if(/fs rpc memory acquisition failed|fs acquisition stopped|overwrite missed.*canary/.test(s)){title='Filesystem access was not acquired in this attempt.';action='Fully close and reopen the game, open a fresh Direct, then start a new session.';}
  else if(/resources retained|uncertain|did not (?:finish|terminate)|restart required|native execution/.test(s)){title='Native execution could not be confirmed.';action='Restart the game before starting a new session.';}
  else if(/9026|loader.*not (?:respond|found)|awaiting payload/.test(s)){title='The game loader is not responding.';action='Open Load Game on Switch, wait for Awaiting payload, then check the connection.';}
  else if(/package changed|selected package|version.*(?:differ|mismatch)/.test(s)){title='The selected package has changed.';action='Refresh App library and select the current version.';}
  else if(/failed to fetch|socket hang up|disconnected|connection.*(?:lost|refused|ended)|server.*unavailable/.test(s)){title='Connection was interrupted.';action='Check the network and restore the connection when the console is ready.';}
  else if(/timeout|timed out|aborted due/.test(s)){title='The operation timed out.';action='Check the device and connection before trying again.';}
  else if(/cancelled|canceled/.test(s)){title='The operation was cancelled.';action='Review the current console screen before continuing.';}
  else {title=detail.split('\n').find(l=>l.trim())||'The operation failed.';if(title.length>220)title='The operation failed.';action='Open technical details or download the full log.';}
  return {title,action,detail};
 }
 const api={canonical,describe,configure};root.IZ2S_ERRORS=api;
 if(typeof module==='object'&&module.exports){module.exports=api;configure(require('./ru/ui.json'));}
})(globalThis);
// END ERROR TEXT
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
  if (!supported(selected)) selected = 'en';
  let catalog = {}, matcher = null, sourceAliases = {}, sourceMatcher = null, english = {};
  const originals = new WeakMap(), attributes = new WeakMap();
  const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  function t(value) {
    const raw = String(value ?? '');
    const source = Object.hasOwn(english,raw)&&!/[\u0400-\u04ff]/.test(raw)?raw:canonicalSource(raw);
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
  function canonicalSource(value) {
    const raw=String(value??'');
    if(Object.hasOwn(sourceAliases,raw))return sourceAliases[raw];
    if(!sourceMatcher)return raw;
    // Paths are data. Reverse legacy UI translations, never filenames.
    const protectedParts=[];
    let text=raw.replace(/(?:[A-Za-z]:[\\/]|\/)[^\s"'<>]+|`[^`]*`/g,part=>{protectedParts.push(part);return '\u0001'+(protectedParts.length-1)+'\u0002';});
    text=text.replace(sourceMatcher,part=>sourceAliases[part]);
    return text.replace(/\u0001(\d+)\u0002/g,(_,i)=>protectedParts[Number(i)]);
  }
  function source(value) {return canonicalSource(value);}
  function configureSources(catalogs) {
    english=catalogs.en;sourceAliases={};
    for(const [key,value]of Object.entries(english))if(/[\u0400-\u04ff]/.test(key)&&!/[\u0400-\u04ff]/.test(value))sourceAliases[key]=value;
    // Prefer sentence case when several English labels share a translation.
    const sourceRank=key=>(key.match(/[A-Z]/g)||[]).length+(/^[A-Z0-9 ·:/_-]+$/.test(key)?100:0);
    for(const id of ['ru','es','zh'])for(const [key,value]of Object.entries(catalogs[id]).sort((a,b)=>sourceRank(a[0])-sourceRank(b[0]))){
      const canonical=english[key]||key;
      if(value!==canonical&&value.length>=3&&!/[\u0400-\u04ff]/.test(canonical)&&!Object.hasOwn(sourceAliases,value))sourceAliases[value]=canonical;
    }
    const keys=Object.keys(sourceAliases).sort((a,b)=>b.length-a.length);
    sourceMatcher=keys.length?new RegExp('(?<![\\p{L}\\p{N}_./-])(?:'+keys.map(escape).join('|')+')(?![\\p{L}\\p{N}_./-])','gu'):null;
  }
  function installSynchronousText() {
    const descriptor=Object.getOwnPropertyDescriptor(Node.prototype,'textContent');
    Object.defineProperty(Node.prototype,'textContent',{...descriptor,set(value){
      const blocked=this.nodeType===Node.ELEMENT_NODE?this.closest('script,style,code,pre,[contenteditable="true"],[translate="no"],.app-name,.language-picker'):ignored(this);
      const original=String(value??''),canonical=blocked?original:canonicalSource(original),rendered=blocked?original:t(canonical);
      if(descriptor.get.call(this)===rendered)return;
      descriptor.set.call(this,rendered);
      if(!blocked){const node=this.nodeType===Node.TEXT_NODE?this:this.childNodes.length===1&&this.firstChild?.nodeType===Node.TEXT_NODE?this.firstChild:null;if(node)originals.set(node,{source:canonical,rendered});}
    }});
  }
  function ignored(node) {
    return node.parentElement?.closest('script,style,code,pre,[contenteditable="true"],[translate="no"],.app-name,.language-picker');
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
    if (node.closest('script,style,code,pre,[contenteditable="true"],[translate="no"],.app-name,.language-picker')) return;
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
    if (node.matches('script,style,code,pre,[contenteditable="true"],[translate="no"],.app-name,.language-picker')) return;
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
      if (node.matches('script,style,code,pre,[contenteditable="true"],[translate="no"],.app-name,.language-picker')) return;
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
  const api = window.RUN21_LANGUAGE = {t, source, languages, setLanguage, get id() {return selected;}, get locale() {return languages.find(item => item.id === selected).locale;}};
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
    const catalogs=Object.fromEntries(await Promise.all(languages.map(async item=>[item.id,await readCatalog(item.id)])));
    configureSources(catalogs);globalThis.IZ2S_ERRORS?.configure(catalogs.ru);
    installCatalog(selected,catalogs[selected]);installSynchronousText();
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
