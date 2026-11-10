'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const ids = ['en', 'ru', 'es', 'zh'];
let consoleMessages;
async function forConsole() {
  // Panel preferences never alter console pixels, SD tiles or native labels.
  consoleMessages ||= fs.readFile(path.join(__dirname, 'en/ui.json'), 'utf8').then(JSON.parse).catch(error => {consoleMessages = null; throw error;});
  const messages = await consoleMessages;
  return {id: 'en', messages, font: process.env.HB_FONT || path.join(__dirname, 'fonts/DejaVuSans.ttf'), t: text => messages[text] ?? text};
}
async function current() {
  let id = 'en';
  try {const saved = JSON.parse(await fs.readFile(path.join(root, 'data/state/language.json'), 'utf8')); if (ids.includes(saved.language)) id = saved.language;} catch (error) {if (error.code !== 'ENOENT') throw error;}
  const messages = JSON.parse(await fs.readFile(path.join(__dirname, id, 'ui.json'), 'utf8'));
  let font = process.env.HB_FONT || path.join(__dirname, 'fonts/DejaVuSans.ttf');
  if(id==='zh')for(const candidate of [process.env.HB_CJK_FONT,path.join(__dirname,'fonts/NotoSansCJK-Regular.ttc'),'/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc']){
    if(candidate&&await fs.access(candidate).then(()=>true,()=>false)){font=candidate;break;}
  }
  return {id, messages, font, t: text => messages[text] ?? text};
}
module.exports = {current, forConsole, ids};
