const http = require("node:http");
const path = require("node:path");
const { port, token, addresses } = require("../connection.json");

function request(body) {
  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        host: "127.0.0.1",
        port,
        path: "/",
        method: "POST",
        // A native request must never use a pooled socket that the server may be closing.
        // A fresh loopback connection avoids the idle-close race without replaying commands.
        agent: false,
        headers: {
          authorization: "Bearer " + token,
          "content-type": "application/json",
        },
      },
      (res) => {
        const parts = [];
        res.on("data", (part) => parts.push(part));
        res.on("error", reject);
        res.on("end", () => {
          try {
            const value = JSON.parse(Buffer.concat(parts));
            if (res.statusCode !== 200)
              throw Error(value.error || "Native bridge failed");
            resolve(value);
          } catch (error) {
            reject(error);
          }
        });
      },
    );
    req.setTimeout(addresses.protocol === 2 ? 250000 : 125000, () =>
      req.destroy(Error("Native request timed out; it was not replayed")),
    );
    req.on("error", error => {
      const failure = new Error("Local native RPC connection failed during " + body.command +
        ": " + error.message + "; request was not replayed", {cause: error});
      failure.code = error.code;
      failure.rpcCommand = body.command;
      failure.requestReplayed = false;
      reject(failure);
    });
    req.end(
      JSON.stringify(body, (_, value) =>
        typeof value === "bigint" ? value.toString() : value,
      ),
    );
  });
}

async function call(offset, args) {
  const reply = await request({ command: "call", offset, args });
  return {
    result: BigInt(reply.result),
    output: Buffer.from(reply.output, "base64"),
  };
}
async function pair(calls) {
  const reply = await request({ command: "pair", calls });
  return {
    results: reply.results.map(BigInt),
    output: Buffer.from(reply.output, "base64"),
  };
}
async function callAddress(address, args, options={}) {
  const reply = await request({ command: "call-address", address, args,...(options.priority==='audio'?{priority:'audio'}:{}) });
  return {
    result: BigInt(reply.result),
    output: Buffer.from(reply.output, "base64"),
  };
}
async function read(address, length) {
  if (
    !Number.isSafeInteger(address) ||
    !Number.isInteger(length) ||
    length < 0 ||
    length > 0x4000000
  )
    throw Error("Invalid memory transfer");
  const parts = [];
  for (let offset = 0; offset < length; offset += 0x80000) {
    const size = Math.min(0x80000, length - offset);
    const bytes = Buffer.from(
      (
        await request({
          command: "read",
          address: address + offset,
          length: size,
        })
      ).data,
      "base64",
    );
    if (bytes.length !== size) throw Error("Memory read was truncated");
    parts.push(bytes);
  }
  return Buffer.concat(parts);
}
async function write(address, bytes) {
  if (
    !Number.isSafeInteger(address) ||
    !Buffer.isBuffer(bytes) ||
    bytes.length > 0x4000000
  )
    throw Error("Invalid memory transfer");
  for (let offset = 0; offset < bytes.length; offset += 0x80000)
    await request({
      command: "write",
      address: address + offset,
      data: bytes.subarray(offset, offset + 0x80000).toString("base64"),
    });
}
async function query(address) {
  const result = await request({ command: "query", address });
  return {
    ...result,
    result: 0,
    address: Number(result.address),
    size: Number(result.size),
  };
}
async function writeCall(address, bytes, functionAddress, args, options={}) {
  if (
    !Number.isSafeInteger(address) ||
    !Buffer.isBuffer(bytes) ||
    !bytes.length ||
    bytes.length > 0x8000
  )
    throw Error("Invalid combined memory transfer");
  const reply = await request({
    command: "write-call",
    ...(options.priority==='audio'?{priority:'audio'}:{}),
    address,
    data: bytes.toString("base64"),
    functionAddress,
    args,
  });
  return { result: BigInt(reply.result) };
}
async function readMany(spans) {
  const reply = await request({ command: "read-many", spans });
  if (!Array.isArray(reply.parts) || reply.parts.length !== spans.length)
    throw Error("Memory batch was truncated");
  return reply.parts.map((s, i) => {
    const b = Buffer.from(s, "base64");
    if (b.length !== spans[i].length) throw Error("Memory read was truncated");
    return b;
  });
}

module.exports = {
  request,
  call,
  callAddress,
  pair,
  read,
  readMany,
  write,
  writeCall,
  query,
  a: addresses,
  output: addresses.scratch + 0x4040,
  directory: path.resolve(__dirname, "..", "switch" + addresses.platform),
};
