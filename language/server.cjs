'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const languages = Object.freeze(['en', 'ru', 'es', 'zh']);
// Public local assets only. IDs never become arbitrary filesystem paths.
async function serve(req, res) {
  const url = new URL(req.url, 'http://127.0.0.1');
  if (url.pathname === '/language/preference') {
    const preference = path.resolve(__dirname, '../data/state/language.json');
    res.setHeader('cache-control', 'no-store');
    const reply = (status, value) => {res.writeHead(status, {'content-type': 'application/json; charset=utf-8'}); res.end(JSON.stringify(value));};
    if (req.method === 'GET') {reply(200, {language: (await require('./node.cjs').current()).id}); return true;}
    if (req.method !== 'POST' || req.headers.origin !== 'http://' + req.headers.host) {reply(403, {error: 'Use the local application address'}); return true;}
    let body = '';
    for await (const chunk of req) {body += chunk; if (Buffer.byteLength(body) > 128) {reply(413, {error: 'Request is too large'}); return true;}}
    let data;
    try {data = JSON.parse(body);} catch {reply(400, {error: 'Invalid language preference'}); return true;}
    if (!languages.includes(data?.language)) {reply(400, {error: 'Unsupported interface language'}); return true;}
    await require('../core/session/ledger.cjs').saveJson(preference, {language: data.language});
    reply(200, {language: data.language}); return true;
  }
  if (req.method !== 'GET') return false;
  let file, type;
  if (url.pathname === '/language/style.css') {
    file = path.join(__dirname, 'style.css');
    type = 'text/css; charset=utf-8';
  } else if (url.pathname === '/language/runtime.js') {
    file = path.join(__dirname, 'runtime.js');
    type = 'text/javascript; charset=utf-8';
  } else {
    const match = /^\/language\/(en|ru|es|zh)\/ui\.json$/.exec(url.pathname);
    if (!match) return false;
    file = path.join(__dirname, match[1], 'ui.json');
    type = 'application/json; charset=utf-8';
  }
  const body = await fs.readFile(file);
  res.writeHead(200, {'content-type': type, 'cache-control': 'no-store', 'x-content-type-options': 'nosniff'});
  res.end(body);
  return true;
}
module.exports = {serve, languages};
