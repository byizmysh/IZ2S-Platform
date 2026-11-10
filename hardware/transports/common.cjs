const fs = require('node:fs/promises'),
  path = require('node:path'),
  { spawn } = require('node:child_process');
function invoke(
  file,
  args,
  { input, env = process.env, timeout = 30000, onEvent } = {},
) {
  return new Promise((resolve, reject) => {
    const child = spawn(file, args, { env, stdio: ['pipe', 'pipe', 'pipe'] });
    let output = '',
      errors = '',
      tail = '',
      done = false;
    const timer = setTimeout(() => {
      child.kill('SIGTERM');
      finish(Error('Input service timed out; commands were not retried'));
    }, timeout);
    function finish(error, value) {
      if (done) return;
      done = true;
      clearTimeout(timer);
      error ? reject(error) : resolve(value);
    }
    child.stdout.on('data', (part) => {
      output += part;
      tail += part;
      let at;
      while ((at = tail.indexOf('\n')) >= 0) {
        const line = tail.slice(0, at);
        tail = tail.slice(at + 1);
        if (onEvent && line)
          try {
            onEvent(JSON.parse(line));
          } catch (e) {
            child.kill('SIGTERM');
            finish(e);
          }
      }
    });
    child.stderr.on('data', (b) => (errors = (errors + b).slice(-4000)));
    child.on('error', finish);
    child.on('close', (code) =>
      finish(
        code ? Error(errors.trim() || 'Input service stopped') : null,
        output,
      ),
    );
    child.stdin.on('error', () => {});
    child.stdin.end(JSON.stringify(input));
  });
}
async function bundle(root) {
  return require('../agents.cjs').files(root);
}
async function plan(root, value) {
  if (
    !value ||
    !/^[a-f\d-]{36}$/.test(value.id || '') ||
    !Array.isArray(value.steps) ||
    value.steps.length > 4096
  )
    throw Error('Invalid input plan');
  const allowed = new Set([
    'enter',
    'escape',
    'f4',
    'f7',
    'f8',
    'home',
    'end',
    'left',
    'right',
    'up',
    'down',
    'backspace',
    'delete',
    'capslock',
    'tab',
  ]);
  const steps = value.steps.map((step) => {
    if (!step || !['line', 'text', 'key', 'sleep', 'gamepad'].includes(step.type))
      throw Error('Invalid input plan step');
    if (
      ['line', 'text'].includes(step.type) &&
      (typeof step.text !== 'string' ||
        step.text.length > 2048 ||
        /[^\x20-\x7e]/.test(step.text))
    )
      throw Error('A typed line must be printable ASCII');
    if (step.type === 'key' && !allowed.has(step.key))
      throw Error('Unsupported input key');
    if (step.when !== undefined && (!['first-line','not-last-line'].includes(step.when)||value.sourceMode!=='raw-lines'))
      throw Error('Invalid conditional input step');
    if (step.type === 'gamepad' && (!['a','b','x','y','l','r','zl','zr','minus','plus','l3','r3','home','capture'].includes(step.button)||
        !Number.isInteger(step.holdMs??50)||(step.holdMs??50)<20||(step.holdMs??50)>2000))
      throw Error('Unsupported gamepad button or hold time');
    const delay = step.type === 'sleep' ? step.ms : (step.delayMs ?? 500);
    if (!Number.isInteger(delay) || delay < 0 || delay > 120000)
      throw Error('Invalid input delay');
    return { ...step, [step.type === 'sleep' ? 'ms' : 'delayMs']: delay };
  });
  let source = null;
  if (value.sourceFile) {
    const absolute = await fs.realpath(path.resolve(root, value.sourceFile));
    if (!absolute.startsWith(path.resolve(root) + path.sep))
      throw Error('Input source must belong to this IZlauncher directory');
    source = (await fs.readFile(absolute)).toString('utf8');
    if (
      Buffer.byteLength(source) > 131072 ||
      /[^\x09\x0a\x0d\x20-\x7e]/.test(source)
    )
      throw Error('Bootstrap source must be bounded ASCII text');
  }
  const sourceAfter = value.sourceAfter ?? steps.length;
  if (
    !Number.isInteger(sourceAfter) ||
    sourceAfter < 0 ||
    sourceAfter > steps.length
  )
    throw Error('Invalid source insertion point');
  const selection = {};
  if (value.sourceMode !== undefined) {
    if (value.sourceMode !== 'raw-lines' || source === null || /[^\x0a\x0d\x20-\x7e]/.test(source))
      throw Error('Invalid raw source mode');
    const lines = source.trimEnd().split(/\r?\n/);
    if (!Number.isInteger(value.sourceLine) || value.sourceLine < 0 || value.sourceLine >= lines.length || lines.some(line=>!line.length||line.length>253))
      throw Error('Invalid raw source line selection');
    selection.sourceMode = 'raw-lines';
    selection.sourceLine = value.sourceLine;
    if(value.sourceThrough!==undefined){
      if(!Number.isInteger(value.sourceThrough)||value.sourceThrough<value.sourceLine||value.sourceThrough>=lines.length)throw Error('Invalid raw source range');
      selection.sourceThrough=value.sourceThrough;
    }
  }
  return { requestId: value.id, action: 'plan', steps, source, sourceAfter, ...selection };
}
module.exports = { invoke, bundle, plan };
