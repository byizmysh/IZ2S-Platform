// A single owned page used to qualify RO's mapping authority on the real console.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const check = (ok, message) => { if (!ok) throw Error(message); };
const hash = data => crypto.createHash('sha256').update(data).digest('hex');

async function dispatch({s, live, save}, request, state) {
  check(!s.threads.size, 'Finish the native IPC worker before accessing the test page');
  const source = s.base + 0x2200000, output = source + 0x1000;
  check(!(source % 0x1000) && output + 0x1000 <= s.base + s.bytes, 'Test page exceeds our owned allocation');
  const command = request.command.slice('root-native-process-test-'.length);
  if (command === 'prepare') {
    check(!state.processTest || state.processTest.finished, 'A prior code-mapping test needs cleanup');
    const region = await s.rpc.query(source);
    check(!region.result && region.type === 5 && region.permissions === 3 && !region.attributes,
      'The source must be ordinary writable heap');
    const code = await fs.readFile(path.join(__dirname, '../process/test-function.bin'));
    check(code.length > 0 && code.length <= 0x1000, 'Unexpected test-function size');
    const page = Buffer.alloc(0x1000); code.copy(page);
    await s.write(source, page);
    check((await s.readOwned(source, page.length)).equals(page), 'Test code readback differs');
    state.processTest = {session: live.session, source, output, size: 0x1000,
      codeBytes: code.length, codeSha256: hash(code), pageSha256: hash(page), before: region};
    await save(); return state.processTest;
  }
  const test = state.processTest;
  check(test && !test.finished && test.source === source, 'No current owned test page');
  const destination = Number(request.destination);
  check(Number.isSafeInteger(destination) && destination > 0 && !(destination % 0x1000), 'Invalid alias address');
  if (command === 'execute') {
    const region = await s.rpc.query(destination);
    check(!region.result && region.permissions === 5 && !region.attributes &&
      Number(BigInt(region.address) + BigInt(region.size)) >= destination + test.size,
      'RO did not provide a readable executable alias');
    check(hash(await s.read(destination, test.size)) === test.pageSha256, 'Executable alias bytes differ');
    const nonce = Number(request.nonce);
    check(Number.isInteger(nonce) && nonce > 0 && nonce <= 0x3fffffff, 'Invalid test nonce');
    await s.write(output, Buffer.alloc(64, 0xa5));
    test.destination = destination; test.nonce = nonce; test.pending = 'execute';
    await save();
    // The boot-time direct-call list predates this alias. The existing call4
    // bridge reaches it only after the live RX and complete-page hash checks.
    const code = await fs.readFile(path.join(__dirname, '../process/test-function.bin'));
    check(hash(code) === test.codeSha256, 'Test instructions changed after preparation');
    await s.rpc.verifyFunction(destination, code);
    test.result = await s.rpc.call4(destination, output, nonce, 0, 0);
    test.outputHex = (await s.readOwned(output, 48)).toString('hex');
    test.mapping = region; delete test.pending;
    await save(); return test;
  }
  if (command === 'cleanup') {
    const sourceInfo = await s.rpc.query(source), aliasInfo = await s.rpc.query(destination);
    check(!sourceInfo.result && sourceInfo.type === 5 && sourceInfo.permissions === 3 && !sourceInfo.attributes,
      'The source page is still locked or changed');
    check(!aliasInfo.result && aliasInfo.type === 0 && aliasInfo.permissions === 0, 'The executable alias remains mapped');
    await s.write(source, Buffer.alloc(0x2000));
    test.cleanup = {source: sourceInfo, alias: aliasInfo}; test.finished = true;
    delete test.pending;
    await save(); return test;
  }
  throw Error('Unknown owned process test operation');
}
module.exports = {dispatch};
