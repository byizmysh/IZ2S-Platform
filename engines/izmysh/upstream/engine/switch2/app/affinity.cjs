// Set only owned parked workers to a CPU allowed for this process.
const {live, check, u64} = require("./session.cjs");
const reference = require("./thread-scheduling-reference.json").functions;
const compat = require("./compat-affinity-reference.json");
const availableName = 'nn::os::GetThreadAvailableCoreMask()';
const currentName = 'nn::os::GetCurrentCoreNumber()';

async function verify(s) {
  if (s.affinityCodeVerified) return;
  for (const name of ['pthread_getaffinity_np', 'pthread_setaffinity_np', availableName, currentName,
    'nn::os::detail::ThreadManagerImplByHorizon::GetThreadAvailableCoreMask() const',
    'nn::os::detail::ThreadManagerImplByHorizon::GetCurrentCoreNumber() const']) {
    const expected = reference[name];
    await s.verifyCode(expected, 'Thread affinity ' + name);
  }
  await s.verifyCode(compat.import, 'SDK affinity import');
  const target = Number((await s.read(live.sdk + compat.gotOffset, 8)).readBigUInt64LE());
  check(Number.isSafeInteger(target) && target > 0 &&
    (await s.read(target, compat.target.size)).toString('hex') === compat.target.bytes,
    'The compatibility affinity wrapper differs');
  check((await s.read(target + compat.symbolOffsetFromTarget, compat.symbol.length + 1)).toString() === compat.symbol + '\0',
    'The compatibility wrapper resolves a different function');
  s.affinityCodeVerified = true;
}

async function available(s) {
  await verify(s);
  if (!s.affinityMasks) {
    const processMask = await s.rpc.call3(live.sdk + reference[availableName].offset, 0, 0, 0);
    const sdkMask = await s.rpc.call3(live.sdk + compat.import.offset, 0, 0, 0);
    check([processMask, sdkMask].every(mask => Number.isSafeInteger(mask) && mask > 0 && mask < 2 ** 32) &&
      (BigInt(sdkMask) & BigInt(processMask)) === BigInt(sdkMask), 'Unexpected available CPU masks');
    s.affinityMasks = {processMask: String(processMask), sdkMask: String(sdkMask)};
    await s.record({operation: 'availableCoreMasks', ...s.affinityMasks});
  }
  return BigInt(s.affinityMasks.sdkMask);
}

function currentCoreCall() {
  return {kind: 'core', function: live.sdk + reference[currentName].offset, args: [0, 0, 0, 0]};
}

async function get(s, worker) {
  check(s.threads.has(worker.thread), 'Affinity query requires an owned worker');
  await verify(s);
  const output = s.base + 0x2800;
  await s.write(output, Buffer.alloc(8));
  check(!await s.rpc.call3(live.sdk + reference.pthread_getaffinity_np.offset,
    worker.thread, 8, output), 'Worker affinity query failed');
  const mask = (await s.readOwned(output, 8)).readBigUInt64LE();
  check(mask > 0n, 'Worker has no allowed CPU cores');
  return mask;
}

async function pin(s, worker, mask) {
  const before = await get(s, worker);
  const allowed = await available(s);
  check(mask > 0n && (mask & (mask - 1n)) === 0n && (mask & allowed) === mask,
    'Choose one CPU allowed for this process');
  const input = s.base + 0x2800;
  await s.write(input, u64(mask));
  const result = await s.rpc.call3(live.sdk + reference.pthread_setaffinity_np.offset, worker.thread, 8, input);
  await s.record({operation: 'workerAffinitySet', label: worker.label, thread: worker.thread,
    before: before.toString(), requested: mask.toString(), result});
  check(!result, 'Worker affinity update failed: ' + result);
  check(await get(s, worker) === mask, 'Worker affinity readback differs');
  await s.record({operation: 'workerAffinity', label: worker.label, thread: worker.thread,
    before: before.toString(), after: mask.toString()});
  return {before: before.toString(), after: mask.toString()};
}

async function pinAll(s, workers, masks) {
  check(workers.length === masks.length && workers.length <= 32, 'Invalid worker affinity list');
  for (let i = 0; i < workers.length; i++) await pin(s, workers[i], masks[i]);
}

module.exports = {verify, available, currentCoreCall, get, pin, pinAll};
