'use strict';
// Copied only into an explicitly configured fresh run. No session discovery,
// ordinary SDK fallback, process acquisition, ownership or console auto-start.
const fs = require('node:fs'), path = require('node:path'), http = require('node:http');
const {AsyncLocalStorage} = require('node:async_hooks');
const crypto = require('node:crypto');
const ID = /^[A-Za-z0-9_-]{1,128}$/, HEX = /^[a-f0-9]{64}$/;
const plain = x => x !== null && typeof x === 'object' && !Array.isArray(x);
function check(ok) { if (!ok) throw Error('Invalid private FS admission configuration'); }
function privateConfiguration(file) {
  check(typeof file === 'string' && path.isAbsolute(file));
  const parent = fs.lstatSync(path.dirname(file));
  check(parent.isDirectory() && !parent.isSymbolicLink() && (parent.mode & 0o777) === 0o700);
  if (typeof process.getuid === 'function') check(parent.uid === process.getuid());
  const fd = fs.openSync(file, fs.constants.O_RDONLY | fs.constants.O_NOFOLLOW);
  try {
    const s = fs.fstatSync(fd); check(s.isFile() && s.nlink === 1 && s.size > 0 && s.size <= 65536 && (s.mode & 0o777) === 0o600);
    if (typeof process.getuid === 'function') check(s.uid === process.getuid());
    const value = JSON.parse(fs.readFileSync(fd, 'utf8'));
    check(plain(value) && plain(value.connection) && plain(value.channel));
    return value;
  } catch { throw Error('Invalid private FS admission configuration'); }
  finally { fs.closeSync(fd); }
}
function open({configurationFile, clientModule = path.join(__dirname, 'admission-client.cjs'),
  installIpc = true, sendIpc = value => process.send?.(value)} = {}) {
  const config = privateConfiguration(configurationFile);
  const {connection, channel} = config, timeoutMs = config.timeoutMs ?? 125000;
  const prefix = config.participantIdPrefix ?? 'fs-dispatch'; check(ID.test(prefix) && prefix.length <= 64);
  const client = require(clientModule).create({connection, channel, timeoutMs});
  const a = Object.freeze({...connection.addresses}), store = new AsyncLocalStorage();
  const authorityId = channel.authorityId, session = a.session;
  let sequence = 0, retained = false, retainPromise, cleanup = null, cleanupPending = null, ready = null, ipcHandler;
  function control(action) {
    const body = JSON.stringify({action});
    return new Promise((resolve, reject) => {
      let settled = false;
      const done = (error, value) => { if (settled) return; settled = true; clearTimeout(timer); error ? reject(error) : resolve(value); };
      const req = http.request({host: '127.0.0.1', port: connection.port, path: '/admission-control', method: 'POST', agent: false,
        headers: {authorization: 'Bearer ' + channel.token, 'content-type': 'application/json'}}, res => {
        let text = '';
        res.on('data', b => { text += b; if (text.length > 65536) req.destroy(Error('Admission control response exceeded bound')); });
        res.on('error', error => done(error));
        res.on('end', () => { try {
          const value = JSON.parse(text);
          if (res.statusCode !== 200 || value.authorityId !== authorityId) throw Error('Admission control rejected');
          done(null, value);
        } catch { done(Error('Admission control response unverified')); } });
      });
      const timer = setTimeout(() => req.destroy(Error('Admission control outcome unknown')), Math.min(timeoutMs, 5000));
      req.on('error', error => done(error)); req.end(body);
    });
  }
  function retainAuthority() {
    retained = true;
    // Independent one-shot control route is deliberately available even after
    // the strict SDK client freezes on a malformed reply. Never repeat it.
    if (!retainPromise) retainPromise = control('retain').then(v => {
      if (v.phase !== 'retained') throw Error('Admission retention unverified');
    }).catch(() => {});
    return retainPromise;
  }
  function fenced() { return Object.assign(Error('FS admission dispatch fenced before execution'), {admissionFenced: true}); }
  function failed() { return Error('FS admission request retained; no retry'); }
  async function track(ctx, operation) {
    if (retained || !ctx.accepting) { await retainAuthority(); throw failed(); }
    const task = Promise.resolve().then(() => operation(ctx.scope.rpc)); ctx.pending.add(task);
    try { return await task; }
    catch { await retainAuthority(); throw failed(); }
    finally { ctx.pending.delete(task); }
  }
  async function runDispatch(task, {claim, command, aborted = () => false} = {}) {
    if (retained) throw failed();
    if (store.getStore()) { await retainAuthority(); throw failed(); }
    let scope;
    if (cleanup) {
      if (!['retire', 'native-admission-status'].includes(command)) throw fenced();
      scope = cleanup.scope;
    } else {
      // Atomic refusal/permit issuance in the actual original gate. No status
      // check -> acquire -> BEGIN race; known fenced replies do not poison an
      // unrelated active owner. Unknown/schema failures still retain.
      try {
        if (typeof client.tryBegin !== 'function') throw failed();
        scope = await client.tryBegin(prefix + '_' + process.pid + '_' + (++sequence), claim ? {claim} : undefined);
      }
      catch { await retainAuthority(); throw failed(); }
      if (scope.fenced === true) throw fenced();
    }
    const ctx = {scope, accepting: true, pending: new Set()};
    try {
      return await store.run(ctx, async () => {
        const result = await task(); ctx.accepting = false;
        const pending = await Promise.allSettled([...ctx.pending]);
        if (pending.some(x => x.status === 'rejected') || retained || aborted()) throw failed();
        if (!cleanup) await client.end(scope);
        return result;
      });
    } catch { ctx.accepting = false; await retainAuthority(); throw failed(); }
  }
  function using(operation) {
    const ctx = store.getStore();
    if (ctx) return track(ctx, operation);
    if (retained) return Promise.reject(failed());
    if (cleanup) return Promise.resolve().then(() => operation(cleanup.scope.rpc)).catch(async () => { await retainAuthority(); throw failed(); });
    return runDispatch(() => track(store.getStore(), operation));
  }
  const bytes = value => {
    if (typeof value !== 'string') throw failed();
    const b = Buffer.from(value, 'base64'); if (b.toString('base64') !== value) throw failed(); return b;
  };
  const u64 = value => {
    if (typeof value !== 'string' || !/^(0|[1-9][0-9]{0,19})$/.test(value)) throw failed();
    const n = BigInt(value); if (n > 0xffffffffffffffffn) throw failed(); return n;
  };
  const rpc = {
    a, output: a.scratch + 0x4040, directory: path.resolve(__dirname, '..', 'switch' + a.platform),
    request: (body, validate) => using(p => p.request(body, validate)),
    call: (offset, args) => using(p => p.call(offset, args)),
    callAddress: (address, args, options = {}) => using(p => p.request({command: 'call-address', address, args,
      ...(options.priority === 'audio' ? {priority: 'audio'} : {})}, r => ({result: u64(r.result), output: bytes(r.output)}))),
    pair: calls => using(p => p.pair(calls)),
    read: (address, length) => using(p => p.read(address, length)),
    write: (address, data) => using(p => p.write(address, data)),
    query: address => using(p => p.query(address)),
    readMany: spans => using(p => p.request({command: 'read-many', spans}, r => {
      if (!Array.isArray(r.parts) || r.parts.length !== spans.length) throw failed();
      return r.parts.map((v, i) => { const b = bytes(v); if (b.length !== spans[i].length) throw failed(); return b; });
    })),
    writeCall: (address, data, functionAddress, args, options = {}) => using(p => {
      if (!Buffer.isBuffer(data) || data.length < 1 || data.length > 0x8000) throw failed();
      return p.request({command: 'write-call', address, data: data.toString('base64'), functionAddress, args,
        ...(options.priority === 'audio' ? {priority: 'audio'} : {})}, r => {
        if (r.written !== true) throw failed(); return {result: u64(r.result)};
      });
    }),
  };
  function decodeRequest(text) {
    try {
      const parsed = JSON.parse(text); if (!plain(parsed)) throw failed();
      const request = {...parsed}, envelope = request.__nativeAdmission; delete request.__nativeAdmission;
      if (envelope === undefined) return {request};
      if (!plain(envelope) || Object.keys(envelope).join() !== 'claim' || !plain(envelope.claim)) throw failed();
      const claim = envelope.claim;
      if (Object.keys(claim).sort().join() !== 'authorityId,capability,generation,ownerId' ||
        claim.authorityId !== authorityId || !HEX.test(claim.capability) || !ID.test(claim.ownerId) ||
        !Number.isSafeInteger(claim.generation) || claim.generation < 1) throw failed();
      return {request, claim: Object.freeze({...claim})};
    } catch { throw failed(); } // Never expose invalid JSON snippets or tokens.
  }
  async function acceptCleanup(value) {
    if (retained || !plain(value) || value.authorityId !== authorityId || !HEX.test(value.permit) ||
      !Number.isSafeInteger(value.generation) || value.generation < 0) { await retainAuthority(); throw failed(); }
    if (cleanup) {
      if (cleanup.permit !== value.permit || cleanup.generation !== value.generation) { await retainAuthority(); throw failed(); }
    } else if (cleanupPending) {
      if (cleanupPending.permit !== value.permit || cleanupPending.generation !== value.generation) { await retainAuthority(); throw failed(); }
      await cleanupPending.promise;
    } else {
      if (typeof client.adoptCleanupPermit !== 'function') { await retainAuthority(); throw failed(); }
      try {
        cleanupPending = {permit: value.permit, generation: value.generation,
          promise: client.adoptCleanupPermit(value)};
        const scope = await cleanupPending.promise;
        if (retained) throw failed();
        cleanup = {permit: value.permit, authorityId, generation: value.generation, scope};
      }
      catch { await retainAuthority(); throw failed(); }
    }
    if (retained || !cleanup) throw failed();
    // One-shot arming is complete. Do not keep the pipeline process alive via
    // this IPC listener after its ordinary release handler has finished.
    if (ipcHandler) process.off('message', ipcHandler);
    return {cleanupMode: true, authorityId, generation: value.generation};
  }
  async function endpointReady({session: actualSession, socketPath}) {
    if (actualSession !== session || ready) { await retainAuthority(); throw failed(); }
    await runDispatch(async () => {
      const status = await rpc.request({command: 'status'});
      if (!status.ready || status.suspended || status.admission?.authorityId !== authorityId) throw failed();
    });
    ready = Object.freeze({authorityId, session, socketPath, outerDispatchFenced: true,
      nestedSdkScoped: true, runtimeConfirmed: true});
    return {...ready};
  }
  async function outer(text, socket, dispatch) {
    const {request, claim} = decodeRequest(text);
    if (request.command === 'native-admission-cleanup') return JSON.stringify(await acceptCleanup(request.admissionCleanup)) + '\n';
    return runDispatch(async () => JSON.stringify(request.command === 'native-admission-status' ?
      (() => { if (!ready) throw failed(); return {...ready}; })() : await dispatch(request)) + '\n',
      {claim, command: request.command, aborted: () => socket.destroyed});
  }
  async function outerFailure(error) {
    if (error?.admissionFenced === true && !retained) return false;
    await retainAuthority(); return true;
  }
  function park() {
    // Ref'ed timer preserves the original endpoint parent; no native cleanup,
    // child kill, retry, reconnect or signals are issued on uncertainty.
    return new Promise(() => { setInterval(() => {}, 1000); });
  }
  ipcHandler = async message => {
    if (message?.type !== 'native-admission-cleanup') return;
    try {
      const result = await acceptCleanup(message);
      sendIpc({type: 'native-admission-cleanup-ready', requestId: message.requestId, ...result});
    } catch { sendIpc({type: 'native-admission-cleanup-error', requestId: message.requestId, error: 'FS admission cleanup unverified'}); }
  };
  if (installIpc) process.on('message', ipcHandler);
  rpc.admission = Object.freeze({outer, outerFailure, endpointReady, acceptCleanup, retainAuthority, park,
    retained: () => retained, inspect: () => ({authorityId, session, retained, cleanupMode: !!cleanup, nativeLeaseGranted: false})});
  return Object.freeze(rpc);
}
module.exports = {open, privateConfiguration};
