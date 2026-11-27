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
