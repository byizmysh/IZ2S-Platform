const crypto = require('node:crypto');

async function qualify(native) {
  const a = native.addresses;
  const check = (ok, message) => {if (!ok) throw Error(message);};
  const proof = {started: new Date().toISOString(), platform: a.platform};
  const code = await native.query(a.sdk);
  check(code.address === BigInt(a.sdk) && code.size === 0x5e1000n && code.type === 3 &&
    code.permissions === 5 && code.attributes === 0, 'The SDK mapping differs');
  const crc = await native.call(0x71310, 0, a.sdk, 0x5e1000);
  check(crc.result === 3925528160n, 'SmileBASIC SDK checksum differs');
  const slot = a.scratch + 0x10000;
  const saved = await native.read(slot, 64);
  const marker = crypto.randomBytes(64);
  let written = false;
  try {
    await native.write(slot, marker);
    written = true;
    check((await native.read(slot, marker.length)).equals(marker), 'Fresh memory challenge failed');
    proof.memoryChallenge = marker.toString('hex');
  } finally {
    if (written && !native.connection.error) {
      await native.write(slot, saved);
      check((await native.read(slot, saved.length)).equals(saved), 'Scratch restoration failed');
      proof.scratchRestored = true;
    }
  }
  check((await native.read(a.got, 8)).readBigUInt64LE() === BigInt(a.memmove),
    'The native call did not restore its import');
  proof.importRestored = true;
  const offset = 0x1b18a0;
  check((await native.read(a.sdk + offset, 12)).toString('hex') === '0020009121088052e6401014',
    'Activity-report instructions differ');
  const controller = Number((await native.call(0x1aba00)).result);
  check(Number.isSafeInteger(controller) && controller > 0 && controller < 2 ** 39 && controller % 8 === 0,
    'Invalid application controller');
  const table = Number((await native.read(controller, 8)).readBigUInt64LE());
  check(table === a.sdk + 0xccca90 &&
    (await native.read(table + 0x120, 8)).readBigUInt64LE() === BigInt(a.sdk + offset),
    'Activity-report interface differs');
  check((await native.call(offset, controller)).result === 0n, 'Activity report failed');
  proof.activity = {offset, controller};
  proof.verified = true;
  proof.completed = new Date().toISOString();
  return proof;
}

module.exports = {qualify};
