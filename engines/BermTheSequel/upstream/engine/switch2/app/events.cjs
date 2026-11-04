// Manual-reset light events stored entirely in our allocation.
const {live, check} = require("./session.cjs");
const code = require("./event-code.json").functions;

async function verify(s) {
  for (const [name, expected] of Object.entries(code)) {
    await s.verifyCode(expected, 'Event ' + name);
  }
}

function address(s, pointer) {
  check(pointer % 8 === 0 && pointer >= s.base && pointer + 8 <= s.base + s.bytes,
    'Event is outside the owned allocation');
}

async function initialize(s, pointer) {
  address(s, pointer);
  await s.rpc.call3(live.sdk + code.initialize.offset, pointer, 0, 0);
  const bytes = await s.read(pointer, 8);
  check(bytes[0] === 0 && bytes[1] === 1 && bytes.readUInt32LE(4) === 0,
    'Manual-reset event did not initialize unsignaled');
}

async function signal(s, pointer) {
  address(s, pointer);
  await s.rpc.call3(live.sdk + code.signal.offset, pointer, 0, 0);
}

async function clear(s, pointer) {
  address(s, pointer);
  await s.rpc.call3(live.sdk + code.clear.offset, pointer, 0, 0);
}

async function finalize(s, pointer) {
  address(s, pointer);
  check(!s.threads.size, 'Cannot finalize events while workers remain');
  await s.rpc.call3(live.sdk + code.finalize.offset, pointer, 0, 0);
}

module.exports = {verify, initialize, signal, clear, finalize, code};
