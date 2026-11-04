'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const ids = ['en', 'ru', 'es', 'zh'];
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
module.exports = {current, ids};
