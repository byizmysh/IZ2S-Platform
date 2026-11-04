// The legacy SDK reads the native counter but reports the Switch 1 frequency.
const fs = require('node:fs/promises');
const path = require('node:path');
const {live, check} = require("./session.cjs");
const code = require("./timing-code.json").functions;
const nativeFrequencyCode = {offset: 0x13d6f0, bytes: '00ca9a52803ba072c0035fd6'};

async function verify(s) {
  for (const [name, expected] of Object.entries(code))
    await s.verifyCode(expected, 'Timing ' + name);
  const map = JSON.parse(await fs.readFile(path.join(__dirname, 'memory-map.json'), 'utf8'));
  check(map.session === live.session && map.complete, 'Fresh memory map is missing');
  const candidates = map.regions.filter(region => region.permissions === 5 && BigInt(region.size) === 45989888n);
  check(candidates.length === 1, 'Native compatibility module is not uniquely identified');
  const base = Number(BigInt(candidates[0].address));
  const region = await s.rpc.query(base);
  check(!region.result && region.permissions === 5 && Number(BigInt(region.address)) === base &&
    BigInt(region.size) === 45989888n, 'Native module mapping changed');
  const address = base + nativeFrequencyCode.offset;
  check((await s.read(address, 12)).toString('hex') === nativeFrequencyCode.bytes,
    'Native frequency instructions differ from the captured module');
  await s.rpc.verifyFunction(address, nativeFrequencyCode.bytes);
  const frequency = await s.rpc.call3(address, 0, 0, 0);
  const legacyFrequency = await s.rpc.call3(live.sdk + code.frequency.offset, 0, 0, 0);
  check(frequency === 31250000 && legacyFrequency === 19200000, 'Unexpected counter frequencies');
  await s.record({operation: 'counterFrequency', nativeModule: base, address, frequency, legacyFrequency,
    instructionBytes: nativeFrequencyCode.bytes});
  return frequency;
}

module.exports = {verify, code, nativeFrequencyCode};
