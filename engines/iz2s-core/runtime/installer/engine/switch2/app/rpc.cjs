const transport = require('../../shared/rpc.cjs');
const live = transport.a;
const word = value => BigInt.asUintN(64, BigInt(value));
function args4(args) {
  if (!Array.isArray(args) || args.length > 4) throw Error('At most four native arguments are supported');
  return [...args, ...Array(4 - args.length).fill(0)].map(word);
}
const result32 = value => Number(BigInt.asUintN(32, value));

async function client() {
  const rpc = {calls: 0, bridgeReady: (await transport.request({command: 'status'})).ready};
  async function call(fn, args) {
    if (!Number.isSafeInteger(fn) || fn % 4) throw Error('Invalid native function');
    const reply = fn >= live.sdk && fn < live.sdk + 0x5e1000
      ? await transport.call(fn - live.sdk, args4(args))
      : await transport.callAddress(fn, args4(args));
    rpc.calls++;
    return result32(reply.result);
  }
  rpc.call3 = (fn, ...args) => call(fn, args);
  rpc.call4 = (fn, ...args) => call(fn, args);
  rpc.verifyFunction = (address, bytes) => transport.request({command: 'verify-function', address,
    hex: Buffer.isBuffer(bytes) ? bytes.toString('hex') : bytes});
  rpc.read = transport.read;
  rpc.write = transport.write;
  rpc.query = transport.query;
  rpc.store64 = (address, value) => {
    const bytes = Buffer.alloc(8);
    bytes.writeBigUInt64LE(word(value));
    return transport.write(address, bytes);
  };
  rpc.batch = async calls => {
    if (!Array.isArray(calls) || calls.length < 1 || calls.length > 32)
      throw Error('Native batch must contain 1..32 calls');
    const replies = [];
    for (let i = 0; i < calls.length;) {
      const item = calls[i];
      if (item.read !== undefined) {
        replies.push({bytes: await transport.read(item.read, item.size)});
        i++; continue;
      }
      if (item.output !== undefined) throw Error('Use an explicit memory read in this transport');
      const group = calls.slice(i, i + 2);
      if (group.length === 2 && group.every(c => c.read === undefined && c.output === undefined &&
          c.fn >= live.sdk && c.fn < live.sdk + 0x5e1000)) {
        const reply = await transport.pair(group.map(c => ({offset: c.fn - live.sdk, args: args4(c.args)})));
        replies.push(...reply.results.map(result => ({result: result32(result)})));
        rpc.calls += 2; i += 2;
      } else {replies.push({result: await call(item.fn, item.args)}); i++;}
    }
    return replies;
  };
  return rpc;
}

module.exports = {client, live};
