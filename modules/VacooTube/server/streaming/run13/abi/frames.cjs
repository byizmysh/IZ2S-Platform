const PAGE = 4096;
const STEP = 0x140;
const RESULT = 0x800;
const SDK_SIZE = 0x5e1000;
const SENTINEL = 0xfedcba9876543210n;
const align = value => Math.ceil(value / PAGE) * PAGE;

// Each call uses a table entry and a saved register frame. Dynamic arguments
// are loaded by preceding memcpy calls, using the same checked SDK functions.
// Additional targets must be verified against live executable mappings by the
// caller before being included in verifiedTargets (used by the Vulkan worker).
function build(base, sdk, calls, code, fixed = false, verifiedTargets = new Set()) {
  if (base % PAGE || sdk % PAGE || calls.length < 1 || calls.length > (fixed ? 96 : 32768))
    throw Error('Invalid worker address or call count');
  const expanded = [], logicalResultIndices = [], patches = [];
  for (const call of calls) {
    if (call.args.length !== 4) throw Error('Workers require exactly four arguments');
    const loads = [];
    call.args.forEach((argument, index) => {
      if (typeof argument !== 'object') return;
      if (fixed || !argument || !['u32','u64'].includes(Object.keys(argument).join()) ||
          !Number.isSafeInteger(argument.u32??argument.u64) || (argument.u32??argument.u64) <= 0 || (argument.u32??argument.u64) % (argument.u64!==undefined?8:4))
        throw Error('Dynamic arguments require an aligned u32/u64 address');
      loads.push({load: expanded.length, argument: index});
      expanded.push({function: sdk + 0x586fb0, args: [0, argument.u32??argument.u64, argument.u64!==undefined?8:4, 0]});
    });
    const target = expanded.length;
    patches.push(...loads.map(load => ({...load, target})));
    logicalResultIndices.push(target);
    expanded.push({...call, args: call.args.map(value => typeof value === 'object' ? 0 : value)});
  }
  const count = expanded.length;
  const tables = fixed ? 0x1000 : align(RESULT + count * 8);
  const stack = fixed ? 0x28000 : align(tables + count * 0x100) + 0x10000;
  const size = fixed ? 0x30000 : align(stack + count * STEP + 0x4000);
  if (size > 0x2000000 || RESULT + count * 8 > tables || tables + count * 0x100 > stack || stack + count * STEP > size)
    throw Error('Worker regions overlap or exceed the allocation');
  for (const {load, target, argument} of patches) {
    const slot = argument === 0 ? (target === 0 ? 0x30 : stack + (target - 1) * STEP + 0xe8)
      : stack + target * STEP + [0, 0x10, 0x20, 0x28][argument];
    expanded[load].args[0] = base + slot;
  }
  const frame = Buffer.alloc(size);
  function put(offset, value) {
    const word = BigInt(value);
    if (word < 0n || word > 0xffffffffffffffffn) throw Error('Worker word is outside uint64');
    frame.writeBigUInt64LE(word, offset);
  }
  put(0x10, base + tables); // x21: first dispatch table
  put(0x30, expanded[0].args[0]); // x25: first call's x0
  put(0x58, sdk + code.loadTable.offset);
  put(0x68, base + stack);
  expanded.forEach((call, index) => {
    if (!Number.isSafeInteger(call.function) || call.function % 4 ||
        ((call.function < sdk || call.function >= sdk + SDK_SIZE) && !verifiedTargets.has(call.function)))
      throw Error('Worker function is outside the qualified SDK');
    const table = tables + index * 0x100, sp = stack + index * STEP;
    put(table, base + table + 0x10);
    put(table + 0x78, call.function);
    put(RESULT + index * 8, SENTINEL);
    put(sp + 8, sdk + code.call4.offset);
    [0x10, 0x20, 0x28].forEach((offset, argument) => put(sp + offset, call.args[argument + 1]));
    put(sp + 0xd8, sdk + code.saveResult64.offset);
    put(sp + 0x118, base + RESULT + index * 8);
    if (index + 1 < count) {
      put(sp + 0x108, base + tables + (index + 1) * 0x100);
      put(sp + 0xe8, expanded[index + 1].args[0]);
      put(sp + 0x128, sdk + code.loadTable.offset);
    } else put(sp + 0x128, sdk + code.exit.offset);
  });
  return {frame, proof: {bytes: size, logicalCalls: calls.length, nativeCalls: count,
    resultOffsets: expanded.map((_, index) => RESULT + index * 8), logicalResultIndices,
    resultOffset: RESULT + (count - 1) * 8, stackOffset: stack, tablesOffset: tables}};
}

module.exports = {build, SENTINEL};
