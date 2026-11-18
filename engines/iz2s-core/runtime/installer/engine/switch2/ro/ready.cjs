// Confirm that the launched RO service accepts its standard Initialize call.
const {Session, check} = require('../app/session.cjs');
const {argumentsWithPid} = require('./nro.cjs');

async function main() {
  const session = new Session('ro-ready');
  await session.initialize();
  try {
    const ro = await session.openService('ldr:ro');
    const reply = await session.ipc('RO Initialize', ro, 4, argumentsWithPid(),
      {pid: true, copies: [0xffff8001]});
    check(!reply.result, 'RO Initialize failed: 0x' + reply.result.toString(16));
    return {ready: true};
  } finally {
    await session.cleanup();
  }
}
module.exports = {main};
