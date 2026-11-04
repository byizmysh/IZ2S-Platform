const transfer = require('../../shared/rpc.cjs');

function checkSpan(session, address, length) {
  if (!session.base || !Number.isSafeInteger(address) || !Number.isInteger(length) || length < 1 ||
      address < session.base || address + length > session.base + session.bytes)
    throw Error('Transfer is outside the owned allocation');
}

async function write(session, address, bytes) {
  checkSpan(session, address, bytes.length);
  await transfer.write(address, bytes);
}
async function read(session, address, length) {
  checkSpan(session, address, length);
  return transfer.read(address, length);
}
async function close() {}
module.exports = {write, read, close};
