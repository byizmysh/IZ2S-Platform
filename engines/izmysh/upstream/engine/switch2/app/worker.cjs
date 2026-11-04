// Verify the original SDK entry points used by owned native worker frames.
const {check} = require('./session.cjs');
const code = require('./thread-code.json').functions;
const {SENTINEL} = require('../../shared/frames.cjs');

async function verifyWorkerCode(session) {
  for (const name of ['create', 'join', 'current', 'longjmp', 'loadTable',
    'call4', 'saveResult64', 'exit', 'tryWait', 'tryWaitImplementation', 'sleep']) {
    check(code[name], 'Missing worker signature: ' + name);
    await session.verifyCode(code[name], name);
  }
  await session.record({operation: 'workerCodeVerified'});
}
module.exports = {verifyWorkerCode, code, SENTINEL};
