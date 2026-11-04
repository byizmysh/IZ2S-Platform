const REQUEST_BYTES = 0x8080;
const OUTPUT_BYTES = 0x8000;
const RESPONSE_BYTES = 16 + OUTPUT_BYTES;
const INPUT_OFFSET = 0x40000;
const DATA_OFFSET = 0x80;
const OUTPUT_OFFSET = 0x4040;
const SDK_BYTES = 0x5e1000;

function callRecord(sdk, offset, args) {
  if (
    !Number.isSafeInteger(sdk) ||
    sdk < 0x1000 ||
    sdk >= 2 ** 39 ||
    sdk % 4 ||
    !Number.isInteger(offset) ||
    offset < 0 ||
    offset >= SDK_BYTES ||
    offset % 4 ||
    args.length !== 4
  )
    throw Error("Invalid SDK function or argument count");
  // JSON carries BigInt arguments as decimal strings.
  if (
    args.some(
      (value) =>
        typeof value !== "bigint" &&
        !Number.isSafeInteger(value) &&
        !(typeof value === "string" && /^(0|[1-9][0-9]{0,19})$/.test(value)),
    )
  )
    throw Error(
      "Native arguments must be exact integers; use BigInt for values above 2^53",
    );
  const words = [BigInt(sdk + offset), ...args.map(BigInt)];
  if (words.some((value) => value < 0n || value > 0xffffffffffffffffn))
    throw Error("Native arguments must fit uint64");
  const bytes = Buffer.alloc(40);
  words.forEach((value, index) => bytes.writeBigUInt64LE(value, index * 8));
  return bytes;
}

class NativeClient {
  constructor(connection, addresses) {
    this.connection = connection;
    this.addresses = addresses;
    this.output = addresses.scratch + OUTPUT_OFFSET;
    this.queue = Promise.resolve();
    this.calls = 0;
    this.functions = new Set();
  }

  exchange(calls, data = Buffer.alloc(0)) {
    if (calls.length < 1 || calls.length > 2 || data.length > 0x8000)
      return Promise.reject(Error("Invalid native request"));
    const packet = Buffer.alloc(REQUEST_BYTES);
    const operations = [...calls];
    if (operations.length === 1)
      operations.push({
        offset: 0x5872f0,
        args: [this.output, this.output, 0, 0],
      });
    operations.forEach(({ offset, address, args }, index) => {
      if (address !== undefined && !this.functions.has(address))
        throw Error("Native function has not been verified");
      callRecord(
        address ?? this.addresses.sdk,
        address === undefined ? offset : 0,
        args,
      ).copy(packet, index * 40);
    });
    data.copy(packet, DATA_OFFSET);
    const task = this.queue.then(async () => {
      await this.connection.write(packet);
      const bytes = await this.connection.read(RESPONSE_BYTES);
      this.calls += calls.length;
      return {
        results: [bytes.readBigUInt64LE(8), bytes.readBigUInt64LE()],
        result: bytes.readBigUInt64LE(8),
        output: bytes.subarray(16),
      };
    });
    this.queue = task.catch(() => {});
    return task;
  }

  call(offset, ...args) {
    if (args.length > 4)
      return Promise.reject(
        Error("At most four native arguments are supported"),
      );
    while (args.length < 4) args.push(0);
    return this.exchange([{ offset, args }]);
  }

  async verifyFunction(address, expected) {
    if (
      !Number.isSafeInteger(address) ||
      address < 0x1000 ||
      address % 4 ||
      !Buffer.isBuffer(expected) ||
      expected.length < 4 ||
      expected.length > 256 ||
      expected.length % 4
    )
      throw Error("Invalid native function signature");
    const region = await this.query(address);
    if (
      region.permissions !== 5 ||
      region.attributes ||
      BigInt(address + expected.length) > region.address + region.size ||
      !(await this.read(address, expected.length)).equals(expected)
    )
      throw Error("Native function mapping or instructions differ");
    this.functions.add(address);
  }

  callAddress(address, ...args) {
    if (args.length > 4)
      return Promise.reject(
        Error("At most four native arguments are supported"),
      );
    while (args.length < 4) args.push(0);
    return this.exchange([{ address, args }]);
  }

  async query(address) {
    const { result, output: bytes } = await this.call(
      0x158314,
      this.output,
      this.output + 0x40,
      address,
    );
    if (result) throw Error("QueryMemory failed: 0x" + result.toString(16));
    return {
      address: bytes.readBigUInt64LE(),
      size: bytes.readBigUInt64LE(8),
      type: bytes.readUInt32LE(16),
      attributes: bytes.readUInt32LE(20),
      permissions: bytes.readUInt32LE(24),
      ipcReferences: bytes.readUInt32LE(28),
      deviceReferences: bytes.readUInt32LE(32),
    };
  }

  async guard(address, length, permission, regions = []) {
    if (
      !Number.isSafeInteger(address) ||
      address < 0x1000 ||
      !Number.isInteger(length) ||
      length < 1 ||
      address + length >= 2 ** 39
    )
      throw Error("Invalid application memory span");
    for (let at = address; at < address + length; ) {
      // A batch may reuse a mapping queried within that same serialized task.
      // Nothing is cached across tasks, allocations, app transitions or HOME.
      const region =
        regions.find(
          (r) => r.address <= BigInt(at) && r.address + r.size > BigInt(at),
        ) || (await this.query(at));
      const end = Number(region.address + region.size);
      if (
        Number(region.address) > at ||
        end <= at ||
        (region.permissions & permission) !== permission ||
        region.ipcReferences ||
        [0, 1, 16].includes(region.type)
      )
        throw Error("Application memory is inaccessible or reserved by IPC");
      if (!regions.includes(region)) regions.push(region);
      at = end;
    }
  }

  async read(address, length) {
    if (length === 0) return Buffer.alloc(0);
    await this.guard(address, length, 1);
    const parts = [];
    for (let at = 0; at < length; at += OUTPUT_BYTES) {
      const count = Math.min(OUTPUT_BYTES, length - at);
      const reply = await this.call(0x586fb0, this.output, address + at, count);
      if (reply.result !== BigInt(this.output))
        throw Error("Native memory read failed");
      parts.push(reply.output.subarray(0, count));
    }
    return Buffer.concat(parts);
  }

  async readMany(spans) {
    if (
      !Array.isArray(spans) ||
      spans.length < 1 ||
      spans.length > 8 ||
      spans.some(
        (s) =>
          !s ||
          !Number.isSafeInteger(s.address) ||
          s.address < 0x1000 ||
          !Number.isInteger(s.length) ||
          s.length < 1 ||
          s.address + s.length >= 2 ** 39 ||
          (s.address < this.output + OUTPUT_BYTES &&
            s.address + s.length > this.output),
      ) ||
      spans.reduce((n, s) => n + s.length, 0) > OUTPUT_BYTES
    )
      throw Error("Invalid bounded memory read batch");
    const regions = [];
    for (const { address, length } of spans)
      await this.guard(address, length, 1, regions);
    const parts = [];
    for (let i = 0; i < spans.length; i += 2) {
      const pair = spans.slice(i, i + 2);
      let offset = 0;
      const calls = pair.map((s) => {
        const call = {
          offset: 0x586fb0,
          args: [this.output + offset, s.address, s.length, 0],
        };
        offset += s.length;
        return call;
      });
      const reply = await this.exchange(calls);
      offset = 0;
      pair.forEach((s, j) => {
        if (reply.results[j] !== BigInt(this.output + offset))
          throw Error("Native batch read failed");
        parts.push(
          Buffer.from(reply.output.subarray(offset, offset + s.length)),
        );
        offset += s.length;
      });
    }
    return parts;
  }

  async write(address, bytes) {
    if (!Buffer.isBuffer(bytes)) throw Error("Expected a byte buffer");
    if (bytes.length === 0) return;
    await this.guard(address, bytes.length, 2);
    for (let at = 0; at < bytes.length; at += 0x8000) {
      const part = bytes.subarray(at, at + 0x8000);
      const args = [
        address + at,
        this.addresses.scratch + INPUT_OFFSET + DATA_OFFSET,
        part.length,
        0,
      ];
      const reply = await this.exchange([{ offset: 0x5872f0, args }], part);
      if (reply.result !== BigInt(address + at))
        throw Error("Native memory write failed");
    }
  }

  // The existing bootstrap executes its two records in order. Keep the memory
  // guard, then copy PCM and publish its descriptor in one serialized exchange.
  // No new console code or packet layout is involved; never replay on failure.
  async writeCall(address, bytes, functionAddress, args) {
    if (
      !Buffer.isBuffer(bytes) ||
      bytes.length < 1 ||
      bytes.length > OUTPUT_BYTES ||
      !Array.isArray(args) ||
      args.length > 4 ||
      !this.functions.has(functionAddress)
    )
      throw Error("Invalid qualified write/call");
    const padded = [...args, ...Array(4 - args.length).fill(0)];
    callRecord(functionAddress, 0, padded); // Validate everything before writing.
    await this.guard(address, bytes.length, 2);
    const reply = await this.exchange(
      [
        {
          offset: 0x5872f0,
          args: [
            address,
            this.addresses.scratch + INPUT_OFFSET + DATA_OFFSET,
            bytes.length,
            0,
          ],
        },
        { address: functionAddress, args: padded },
      ],
      bytes,
    );
    if (reply.results[0] !== BigInt(address))
      throw Error("Native memory write failed; call was not replayed");
    return { result: reply.results[1] };
  }

  async stop() {
    try {
      await this.queue;
      if (!this.connection.error)
        await this.connection.write(Buffer.alloc(REQUEST_BYTES));
    } finally {
      this.connection.close();
    }
  }
}

module.exports = {
  NativeClient,
  callRecord,
  REQUEST_BYTES,
  RESPONSE_BYTES,
  OUTPUT_BYTES,
};
