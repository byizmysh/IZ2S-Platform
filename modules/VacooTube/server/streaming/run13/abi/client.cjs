// Qualified calls only. Buffers are session-owned and retained until SmileBASIC exits.
async function create(rpc, definitions, bytes = 65536) {
 if (!Number.isInteger(bytes) || bytes < 4096 || bytes > 1048576 || bytes % 4096) throw Error('Invalid native allocation size');
 const functions = {};
 for (const [name, f] of Object.entries(definitions)) {
  await rpc.request({command:'verify-function', address:rpc.a.sdk + f.offset, hex:f.hex});
  functions[name] = rpc.a.sdk + f.offset;
 }
 const allocation = await rpc.call(5732112, [rpc.output, 4096, bytes]);
 if (allocation.result !== 0n) throw Error('SDK native allocation failed');
 const base = Number(allocation.output.readBigUInt64LE());
 if (!Number.isSafeInteger(base) || base <= 0 || base % 4096) throw Error('Invalid SDK allocation');
 await rpc.write(base, Buffer.alloc(bytes));
 function bounds(offset, length) {
  if (!Number.isInteger(offset) || !Number.isInteger(length) || offset < 0 || length < 0 || offset + length > bytes) throw Error('Native buffer bounds');
 }
 return {rpc, base, bytes, functions,
  async call(name, args = []) {
   if (!Object.hasOwn(functions, name)) throw Error('Unknown native SDK function: ' + name);
   if (!Array.isArray(args) || args.length > 4) throw Error('Native call accepts at most four arguments');
   return (await rpc.callAddress(functions[name], args)).result;
  },
  async result(name, args = []) { return Number(BigInt.asUintN(32, await this.call(name, args))); },
  async write(offset, data) { bounds(offset, data.length); await rpc.write(base + offset, data); },
  async read(offset, length) { bounds(offset, length); return rpc.read(base + offset, length); }
 };
}
module.exports = {create};
