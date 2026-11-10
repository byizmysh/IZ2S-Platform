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
