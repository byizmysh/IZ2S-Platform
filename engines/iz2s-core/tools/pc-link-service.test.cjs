'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {test} = require('node:test');
const link = require('./pc-link-service.cjs');
const {pcTool} = require('../panel.cjs');

const ownerRoot = path.resolve(__dirname, '../../..');
const current = {service:{root:ownerRoot}, online:false, files:false, consoleAddress:'192.0.2.1'};
const foreign = {...current, service:{root:path.join(ownerRoot, 'other')}, consoleAddress:'192.0.2.2'};

async function fixture(t, status, config) {
  const out = await fs.mkdtemp(path.join(os.tmpdir(), 'iz2s-pc-link-test-'));
  t.after(() => fs.rm(out, {recursive:true, force:true}));
  if (config) {
    const folder = link.folder(out);
    await fs.mkdir(folder, {recursive:true});
    await fs.writeFile(path.join(folder, 'connection.json'), JSON.stringify(config));
  }
  t.mock.method(globalThis, 'fetch', async url => {
    assert.equal(url, 'http://127.0.0.1:39525/status');
    return Response.json(status);
  });
  return out;
}

test('unconfigured installation ignores another PC Link service', async t => {
  const out = await fixture(t, foreign);
  assert.equal(await link.snapshot(out), null);
  assert.deepEqual(await link.reset(out), {ok:true, offline:true});
  assert.deepEqual(await pcTool('files', fetch, {out, root:ownerRoot, open:true}), {available:false});
});

test('configured installation detects a conflict while FTP keeps its local address', async t => {
  const out = await fixture(t, foreign, {consoleAddresses:['192.0.2.3']});
  await assert.rejects(link.snapshot(out), /running in another IZ2S installation/);
  assert.deepEqual((await link.consoleAddresses(out)).filter(Boolean), ['192.0.2.3']);
});

test('an owned service remains available', async t => {
  const out = await fixture(t, current);
  assert.deepEqual(await link.snapshot(out), {...current, apps:[]});
  assert.deepEqual((await link.consoleAddresses(out)).filter(Boolean), ['192.0.2.1']);
});

test('setup verifies the local route and exports settings for that adapter', async t => {
  const out = await fixture(t, current);
  const routeTo = async host => {
    assert.equal(host, '192.168.42.20');
    return {address:'192.168.42.10', netmask:'255.255.255.0'};
  };
  const prepared = await link.configure(out,'192.168.42.20',{routeTo});
  assert.deepEqual(prepared,{pcAddress:'192.168.42.10',config:'192.168.42.10\n39524\n'});
  const config = JSON.parse(await fs.readFile(path.join(link.folder(out),'connection.json'),'utf8'));
  assert.deepEqual(config.pcAddresses,['192.168.42.10']);
  assert.deepEqual(config.consoleAddresses,['192.168.42.20']);
});

test('setup rejects public addresses and routes outside the console subnet', async t => {
  const out = await fixture(t, current);
  await assert.rejects(link.configure(out,'198.51.100.20',{routeTo:()=>assert.fail('must not route to a public address')}),
    /Enter the local IPv4 address/);
  await assert.rejects(link.configure(out,'192.168.42.20',{routeTo:async()=>({address:'192.168.43.10',netmask:'255.255.255.0'})}),
    /same local network/);
  await assert.rejects(fs.stat(path.join(link.folder(out),'connection.json')), {code:'ENOENT'});
});

test('setup preserves an active service from another installation', async t => {
  const out = await fixture(t,{...foreign,service:{...foreign.service,pid:2,name:'iz2s-pc-link'},enabled:true,online:true});
  await assert.rejects(link.configure(out,'192.168.42.20',{routeTo:async()=>({address:'192.168.42.10',netmask:'255.255.255.0'})}),
    /Pause PC Link in the other IZ2S panel/);
  await assert.rejects(fs.stat(path.join(link.folder(out),'connection.json')), {code:'ENOENT'});
});

test('only the verified paused legacy worker is released during setup', {skip:process.platform!=='linux'}, async t => {
  const {spawn} = require('node:child_process');
  const {once} = require('node:events');
  const oldRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'iz2s-old-pc-link-test-'));
  t.after(() => fs.rm(oldRoot,{recursive:true,force:true}));
  const script = path.join(oldRoot,'engines/iz2s-core/tools/pc-link.cjs');
  await fs.mkdir(path.dirname(script),{recursive:true});
  await fs.writeFile(path.join(oldRoot,'package.json'),' {"name":"iz2s-platform"}');
  await fs.writeFile(script,"console.log('ready');setInterval(()=>{},1000);");
  const worker = spawn(process.execPath,[script],{stdio:['ignore','pipe','ignore']});
  const exited = once(worker,'exit');
  t.after(async()=>{if(worker.exitCode===null&&worker.signalCode===null)worker.kill('SIGTERM');await exited;});
  await once(worker.stdout,'data');
  const status = {...foreign,service:{name:'iz2s-pc-link',root:oldRoot,pid:worker.pid},enabled:false};
  let checks = 0;
  t.mock.method(globalThis,'fetch',async url=>{
    assert.equal(url,'http://127.0.0.1:39525/status');
    return Response.json(++checks<=2?status:current);
  });
  const out=path.join(oldRoot,'local-config');
  const routeTo=async()=>({address:'192.168.42.10',netmask:'255.255.255.0'});
  assert.equal((await link.configure(out,'192.168.42.20',{routeTo})).pcAddress,'192.168.42.10');
  assert.deepEqual(await exited,[null,'SIGTERM']);
});
