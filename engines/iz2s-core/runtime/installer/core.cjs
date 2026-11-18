#!/usr/bin/env node
// Acquire SmileBASIC RPC, then start RO and IZ2S from the supplied package.
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const readline = require('node:readline');

const runScript = path.join(__dirname, 'run.cjs');

function usage(exitCode) {
  console.log('Usage: node core.cjs --dir PACKAGE_DIRECTORY [PC-LAN-IP]');
  console.log('Start with SmileBASIC open at its Direct-mode prompt.');
  process.exit(exitCode);
}

const args = process.argv.slice(2);
if (args.includes('--help')) usage(0);
const dirOption = args.indexOf('--dir');
if (dirOption < 0 || dirOption + 1 >= args.length) usage(1);
const ship = path.resolve(args[dirOption + 1]);
args.splice(dirOption, 2);
if (args.length > 1 || args.some(arg => arg.startsWith('-'))) usage(1);
const host = args[0];

for (const file of ['ro/main', 'core/main', 'game/main', 'bootstrap.bin', 'iz2s/izlauncher.iza', 'iz2s/izlauncher.romfs']) {
  if (!fs.statSync(path.join(ship, file), {throwIfNoEntry: false})?.isFile()) {
    throw Error(`Missing package file: ${path.join(ship, file)}`);
  }
}
if (!fs.statSync(runScript, {throwIfNoEntry: false})?.isFile()) {
  throw Error(`Missing ns2jb run.cjs: ${runScript}`);
}

let acquisition;

function observe(child) {
  const seen = [];
  let exitCode;
  const lines = readline.createInterface({input: child.stdout});
  child.once('exit', code => { exitCode = code; });
  lines.on('line', line => {
    seen.push(line);
  });
  return {
    seen,
    waitFor(fragment, timeoutMs) {
      if (seen.some(line => line.includes(fragment))) return Promise.resolve();
      if (exitCode !== undefined) {
        return Promise.reject(Error(`Process exited before ${fragment}: ${exitCode}`));
      }
      return new Promise((resolve, reject) => {
        let timer;
        const finish = error => {
          if (timer) clearTimeout(timer);
          lines.off('line', onLine);
          child.off('exit', onExit);
          child.off('error', onError);
          error ? reject(error) : resolve();
        };
        const onLine = line => {
          if (line.includes(fragment)) finish();
        };
        const onExit = code => finish(Error(`Process exited before ${fragment}: ${code}`));
        const onError = error => finish(error);
        lines.on('line', onLine);
        child.once('exit', onExit);
        child.once('error', onError);
        if (timeoutMs) timer = setTimeout(() => finish(Error(`Timed out waiting for ${fragment}`)), timeoutMs);
      });
    },
  };
}

function stopChildren() {
  acquisition?.kill();
}
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, stopChildren);

async function main() {
  const env = {...process.env,
    NS2_BASIC_ARITY: process.env.NS2_BASIC_ARITY || '9',
    NS2_RO_IMAGE: path.join(ship, 'ro/main')};
  acquisition = spawn(process.execPath, [runScript, '--rpc', ...(host ? [host] : [])],
    {stdio: ['inherit', 'pipe', 'inherit'], env});
  // Forward bytes as they arrive: run.cjs's address prompt has no newline.
  acquisition.stdout.pipe(process.stdout);
  const rpcOutput = observe(acquisition);
  await rpcOutput.waitFor('RO frontend verified');
  const folder = rpcOutput.seen.filter(line => line.startsWith('Run folder: ')).at(-1);
  if (!folder) throw Error('ns2jb did not print its run folder');
  const run = path.resolve(folder.slice('Run folder: '.length));
  const result = JSON.parse(fs.readFileSync(path.join(run, 'result.json')));
  if (!result.verified || result.rpc?.state !== 'listening') {
    throw Error('The held ns2jb RPC is not ready');
  }

  process.env.NS2_RUN_DIR = run;
  const programs = fs.readdirSync(path.join(ship, 'programs'), {withFileTypes: true})
    .filter(entry => entry.isFile() && !entry.name.startsWith('.') && !entry.name.startsWith('hbmenu-'))
    .map(entry => ({source: path.join(ship, 'programs', entry.name),
      target: `/switch/${entry.name}`}));
  programs.unshift(...['izlauncher.romfs','izlauncher.iza'].map(name => ({source: path.join(ship,'iz2s',name),target: `/iz2s/${name}`})));
  const pcService=require('../../tools/pc-link-service.cjs');
  const pairing=await pcService.prepare(process.env.IZ2S_CORE_OUT,host||rpcOutput.seen.map(line=>/^Waiting for Switch 2 at ([0-9.]+):/.exec(line)?.[1]).find(Boolean),
    process.env.IZ2S_CONSOLE_IPV4||undefined,process.env.IZ2S_CONSOLE_WIFI_IPV4,JSON.parse(process.env.IZ2S_PC_LINK_HOSTS||'null'));
  programs.push({source:pairing.config,target:'/iz2s/pc-link.cfg'});
  const foldrift = '/iz2s/modules/foldrift/launcher.iza';
  // Publish the signature first. An existing executable owns its signature.
  programs.push(...[foldrift + '.sig', foldrift].map(target => ({
    source: path.join(ship, target.slice(1)), target, skipIfPresent: foldrift,
  })));
  const {main: startCore} = require(path.join(run, 'switch2/ro/start-core.cjs'));
  try {
    const started = await startCore(path.join(ship, 'core/main'),
      path.join(ship, 'bootstrap.bin'), path.join(ship, 'game/main'), programs);
    console.log(`IZ2S PID ${started.pid}; startup record: ${started.file}`);
    await pcService.ensure(process.env.IZ2S_CORE_OUT).catch(error=>console.error('Add app: '+error.message));
  } catch (error) {
    console.error(`IZ2S startup failed; SmileBASIC RPC remains live in ${run}`);
    console.error(error.stack);
    process.exitCode = 1;
    return;
  }

  console.log('IZ2S launch completed! Please quit SmileBASIC and launch a Switch 2 game while holding the R button; repeat once if needed. The built-in IZ2S runtime will open /iz2s/izlauncher.iza automatically.');
  console.log('Keep this process running until SmileBASIC closes.');
  if (acquisition.exitCode === null) {
    await new Promise(resolve => acquisition.once('exit', resolve));
  }
}

main().catch(error => {
  console.error(error.stack);
  stopChildren();
  process.exitCode = 1;
});
