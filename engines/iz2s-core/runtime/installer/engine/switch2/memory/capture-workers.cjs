// Preserve client-side progress without sending another filesystem request.
async function captureWorkers(s, workers) {
  const reads = workers.map(worker => ({address: worker.base + worker.resultOffsets[0],
    bytes: worker.resultOffsets.length * 8}));
  const replies = workers.flatMap(worker => worker.calls.filter(call => call.kind === 'ipc')
    .map(call => ({worker: worker.label, name: call.name, address: call.args[0], bytes: 256})));
  const captured = await s.readOwnedMany([...reads, ...replies]);
  const sentinel = 0xfedcba9876543210n;
  return {at: new Date().toISOString(), workers: workers.map((worker, index) => ({
    label: worker.label, thread: worker.thread,
    calls: worker.calls.map((call, callIndex) => {
      const value = captured[index].readBigUInt64LE(worker.logicalResultIndices[callIndex] * 8);
      return {name: call.name || call.kind || 'call', returned: value !== sentinel,
        value: '0x' + value.toString(16)};
    }),
  })), replies: replies.map((reply, index) => ({worker: reply.worker, name: reply.name,
    address: reply.address, hex: captured[workers.length + index].toString('hex')}))};
}

module.exports = {captureWorkers};
