'use strict';
const fs = require('node:fs/promises'), path = require('node:path'), crypto = require('node:crypto');
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
async function payload(folder) {
  const manifest = JSON.parse(await fs.readFile(path.join(folder, 'input-manifest.json')));
  const source = await fs.readFile(path.join(folder, 'core.js'));
  const packed = await fs.readFile(path.join(folder, 'PicoSwitch/payload.txt'));
  if (digest(source) !== manifest.coreSha256 || digest(packed) !== manifest.payloadSha256)
    throw Error('CivSwitch payload changed; rebuild and verify the input package first');
  const lines = packed.toString('ascii').trimEnd().split('\n');
  if (lines.length !== manifest.lines || lines.some(line => !line.length || line.length > 253 || /[^\x20-\x7e]/.test(line)))
    throw Error('Invalid CivSwitch chat input');
  return {lines, manifest};
}
function linePlan(text, id = crypto.randomUUID()) {
  if (typeof text !== 'string' || !text.length || text.length > 253 || /[^\x20-\x7e]/.test(text))
    throw Error('Invalid CivSwitch chat line');
  // The user opens/focuses the chat with controller A. Do not emit SmileBASIC's
  // Escape-before-Enter sequence here: Escape would cancel the chat text.
  return {id, steps: [
    {type: 'sleep', ms: 1200},
    {type: 'text', text, delayMs: 200},
    {type: 'key', key: 'enter', delayMs: 200},
    {type: 'key', key: 'enter', delayMs: 1200},
  ]};
}
function sourcePlan(folder, index = 0, id = crypto.randomUUID()) {
  return {id, sourceFile:path.join(folder,'PicoSwitch/payload.txt'), sourceMode:'raw-lines', sourceLine:index, sourceAfter:1,
    steps:[{type:'sleep',ms:1200},{type:'key',key:'enter',delayMs:200},{type:'key',key:'enter',delayMs:1200}]};
}
function automaticPlan(folder,index=0,id=crypto.randomUUID()) {
  const a=(delayMs,when)=>({type:'gamepad',button:'a',holdMs:50,delayMs,...(when?{when}:{})});
  // Same controller/keyboard order as upstream payload_to_macro.py. The first
  // five A presses register the controller; no separator follows the last line.
  const before=[...Array.from({length:5},()=>a(800,'first-line')),
    {type:'key',key:'escape',delayMs:800},a(1200)];
  return {id,sourceFile:path.join(folder,'PicoSwitch/payload.txt'),sourceMode:'raw-lines',sourceLine:index,sourceAfter:before.length,
    steps:[...before,{type:'key',key:'enter',delayMs:200},{type:'key',key:'enter',delayMs:0},
      {type:'sleep',ms:800,when:'not-last-line'},a(1200,'not-last-line')]};
}
module.exports = {payload, linePlan, sourcePlan, automaticPlan, digest};
