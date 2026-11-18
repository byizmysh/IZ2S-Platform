// Run a mapped NRO entry on an SDK-created thread, then join before unloading.
const {live, check} = require('../app/session.cjs');
const {code} = require('../app/worker.cjs');

async function execute(session, entry, record, save, config = {}) {
  for (const name of ['create', 'join', 'tryWait', 'tryWaitImplementation']) {
    await session.verifyCode(code[name], name);
  }
  const slots = session.base + 0x6000;
  const returnSlot = slots + 8;
  const statusSlot = slots + 16;
  await session.write(slots, Buffer.alloc(48));
  if (config.prepare) await config.prepare(session, statusSlot);
  record.pendingThread = 'create';
  await save();
  session.threads.add('creating');
  record.createResult = await session.rpc.call4(live.sdk + code.create.offset,
    slots, 0, entry, statusSlot);
  check(!record.createResult, 'pthread_create failed: ' + record.createResult);
  const thread = Number((await session.readOwned(slots, 8)).readBigUInt64LE());
  session.threads.delete('creating');
  session.threads.add(thread);
  record.thread = thread;
  record.pendingThread = 'wait';
  await save();

  const deadline = Date.now() + (config.waitMs ?? 15000);
  let finished = false;
  while (Date.now() < deadline) {
    const state = await session.rpc.call3(live.sdk + code.tryWait.offset, thread, 0, 0);
    check(state === 0 || state === 1, 'Unexpected SDK thread state: ' + state);
    if (state === 1) {
      finished = true;
      break;
    }
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  check(finished, 'Thread has not exited; retain its mapped image and backing');
  record.pendingThread = 'join';
  await save();
  record.joinResult = await session.rpc.call3(live.sdk + code.join.offset, thread, returnSlot, 0);
  check(!record.joinResult, 'pthread_join failed: ' + record.joinResult);
  session.threads.delete(thread);
  record.joined = true;
  const returned = Number((await session.readOwned(returnSlot, 8)).readBigUInt64LE());
  delete record.pendingThread;
  await save();
  if (config.verify) {
    await config.verify(session, statusSlot, record);
    await save();
  }
  return returned;
}

module.exports = {execute};
