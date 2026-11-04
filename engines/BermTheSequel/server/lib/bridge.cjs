// Serialized loopback RPC for the connected console.
const http = require("node:http");
const crypto = require("node:crypto");
const fs = require("node:fs/promises");
const path = require("node:path");

// Stage programs share one native connection through this local, serialized
// bridge. Its address and token exist only in the current launch directory.
async function serve(native, directory, options = {}) {
  const token = crypto.randomBytes(32).toString("hex");
  const queue = require('./request-queue.cjs').create();
  // Opt-in only in a future explicitly configured original host. No existing
  // host is activated or restarted by loading this source. This is dispatch
  // fencing, not native ownership/installation attestation.
  const admission = options.admission ? require('./admission-gate.cjs').create({
    drain: () => queue.idle(), verifyRelease: options.admission.verifyRelease,
  }) : null;
  const controlToken = admission ? crypto.randomBytes(32).toString('hex') : null;
  const statistics = {
    requests: 0,
    completed: 0,
    failed: 0,
    readBytes: 0,
    writtenBytes: 0,
    commands: {},
  };
  async function save(row) {
    if (!directory) return;
    if (row.status === "failed") {
      await fs.writeFile(
        path.join(directory, "bridge-failure.json"),
        JSON.stringify(row, null, 2),
      );
      try {
        await fs.writeFile(
          path.join(directory, "bridge-first-failure.json"),
          JSON.stringify(row, null, 2),
          { flag: "wx" },
        );
      } catch (error) {
        if (error.code !== "EEXIST") throw error;
      }
      await fs.appendFile(
        path.join(directory, "bridge-failures.jsonl"),
        JSON.stringify(row) + "\n",
      );
    }
    const file = path.join(directory, "bridge-progress.json");
    await fs.writeFile(
      file + ".tmp",
      JSON.stringify({ ...row, statistics }, null, 2),
    );
    await fs.rename(file + ".tmp", file);
  }
  const enqueue = (task, info = { command: "keepalive" }, headers) => {
    const ticket = admission?.admit(headers);
    const queuedAt=performance.now();
    return queue.enqueue(async () => {
      ticket?.beforeDispatch();
      const row = {
        ...info,
        queueWaitMs: performance.now()-queuedAt,
        started: new Date().toISOString(),
        status: "pending",
      };
      statistics.requests++;
      const count = (statistics.commands[row.command] ||= {
        count: 0,
        completed: 0,
        failed: 0,
        milliseconds: 0,
      });
      count.count++;
      const start = performance.now();
      await save(row);
      try {
        const result = await task();
        row.status = "returned";
        statistics.completed++;
        count.completed++;
        if (row.command === "read" || row.command === "read-many")
          statistics.readBytes += row.length;
        if (row.command === "write" || row.command === "write-call")
          statistics.writtenBytes += row.length;
        return result;
      } catch (error) {
        row.status = "failed";
        row.error = error.message;
        statistics.failed++;
        count.failed++;
        throw error;
      } finally {
        row.completed = new Date().toISOString();
        row.milliseconds = performance.now() - start;
        count.milliseconds += row.milliseconds;
        await save(row);
      }
    },{priority:info.priority||'normal'}).catch(error => {
      ticket?.failed(); throw error;
    }).finally(() => ticket?.finish());
  };
  async function handle(request) {
    const { command } = request;
    if (command === "status")
      return {
        ready: !native.connection.error && !native.connection.suspended,
        suspended: !!native.connection.suspended,
        calls: native.calls,
      };
    if (command === "call") {
      const reply = await native.call(request.offset, ...request.args);
      return {
        result: reply.result.toString(),
        output: reply.output.toString("base64"),
      };
    }
    if (command === "verify-function") {
      if (
        typeof request.hex !== "string" ||
        !/^(?:[0-9a-f]{2}){4,256}$/i.test(request.hex)
      )
        throw Error("Expected original native instruction bytes");
      await native.verifyFunction(
        request.address,
        Buffer.from(request.hex, "hex"),
      );
      return { verified: true };
    }
    if (command === "call-address") {
      const reply = await native.callAddress(request.address, ...request.args);
      return {
        result: reply.result.toString(),
        output: reply.output.toString("base64"),
      };
    }
    if (command === "pair") {
      const reply = await native.exchange(request.calls);
      return {
        results: reply.results.map(String),
        output: reply.output.toString("base64"),
      };
    }
    if (command === "read")
      return {
        data: (await native.read(request.address, request.length)).toString(
          "base64",
        ),
      };
    if (command === "read-many")
      return {
        parts: (await native.readMany(request.spans)).map((b) =>
          b.toString("base64"),
        ),
      };
    if (command === "write") {
      await native.write(request.address, Buffer.from(request.data, "base64"));
      return { written: true };
    }
    if (command === "write-call") {
      const reply = await native.writeCall(
        request.address,
        Buffer.from(request.data, "base64"),
        request.functionAddress,
        request.args,
      );
      return { result: reply.result.toString(), written: true };
    }
    if (command === "query") return native.query(request.address);
    throw Error("Unknown bridge command");
  }
  const server = http.createServer(async (req, res) => {
    const control = admission && req.url === '/admission-control';
    if (
      req.method !== "POST" ||
      (!control && req.url !== "/") ||
      req.headers.authorization !== "Bearer " + (control ? controlToken : token)
    ) {
      res.writeHead(403).end();
      return;
    }
    try {
      const parts = [];
      let length = 0;
      for await (const part of req) {
        length += part.length;
        if (length > 2 * 1024 * 1024)
          throw Error("Bridge request is too large");
        parts.push(part);
      }
      const request = JSON.parse(Buffer.concat(parts));
      if (control) {
        // Capabilities/control tokens are deliberately absent from progress,
        // error statistics, connection.json and ordinary status responses.
        const actions = new Set(['acquire', 'begin', 'try-begin', 'end', 'release', 'retain', 'status', 'verify-cleanup']);
        if (!request || !actions.has(request.action)) throw Error('Unknown admission control action');
        if (request.action !== 'status') res.once('close', () => {
          if (!res.writableFinished) admission.retain({});
        });
        const result = request.action === 'status' ? admission.inspect() :
          await admission[request.action === 'verify-cleanup' ? 'verifyCleanupPermit' : request.action === 'try-begin' ? 'tryBegin' : request.action](request);
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify(result)); return;
      }
      if (admission && request.command !== 'status') res.once('close', () => {
        if (!res.writableFinished && (admission.inspect().phase !== 'open' ||
          req.headers['x-iz-admission'] !== undefined || req.headers['x-iz-admission-permit'] !== undefined))
          admission.retain({});
      });
      const info = { command: request.command };
      if(request.priority!==undefined){
        if(request.priority!=='audio'||!['call-address','write-call'].includes(request.command))throw Error('Invalid native request priority');
        info.priority='audio';
      }
      for (const key of ["address", "offset", "length"])
        if (request[key] !== undefined) info[key] = request[key];
      if (
        ["write", "write-call"].includes(request.command) &&
        typeof request.data === "string"
      )
        info.length = Buffer.byteLength(request.data, "base64");
      if (request.command === "read-many" && Array.isArray(request.spans))
        info.length = request.spans.reduce(
          (n, s) => n + (Number.isInteger(s?.length) ? s.length : 0),
          0,
        );
      const result = await (request.command === "status"
        ? handle(request)
        : enqueue(() => handle(request), info, req.headers));
      if (admission && request.command === 'status') result.admission = admission.inspect();
      res.setHeader("Content-Type", "application/json");
      res.end(
        JSON.stringify(result, (_, value) =>
          typeof value === "bigint" ? value.toString() : value,
        ),
      );
    } catch (error) {
      res
        .writeHead(400, { "Content-Type": "application/json" })
        .end(JSON.stringify({ error: error.message }));
    }
  });
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  let activity;
  let lastActivity = 0;
  let heartbeatPending = false;
  const heartbeat = setInterval(() => {
    if (admission && admission.inspect().phase !== 'open') return;
    if (heartbeatPending) return;
    heartbeatPending = true;
    enqueue(async () => {
      if (native.connection.error) return;
      if (activity && Date.now() - lastActivity > 30000) {
        const reply = await native.call(activity.offset, activity.controller);
        if (reply.result) throw Error("Console activity report failed");
        lastActivity = Date.now();
      } else await native.call(0x5872f0, native.output, native.output, 0);
    })
      .catch((error) => native.connection.fail(error))
      .finally(() => {
        heartbeatPending = false;
      });
  }, 5000);
  heartbeat.unref();
  return {
    port: server.address().port,
    token,
    ...(admission ? {admission: Object.freeze({
      acquire: admission.acquire, begin: admission.begin, end: admission.end,
      release: admission.release, retain: admission.retain, inspect: admission.inspect,
      sealForShutdown: admission.sealForShutdown, verifyShutdownSeal: admission.verifyShutdownSeal,
      beginShutdownCleanup: admission.beginShutdownCleanup, endShutdownCleanup: admission.endShutdownCleanup,
      channel: Object.freeze({url: 'http://127.0.0.1:' + server.address().port + '/admission-control',
        token: controlToken, authorityId: admission.inspect().authorityId}),
    })} : {}),
    keepAwake(value) {
      activity = value;
    },
    async close() {
      clearInterval(heartbeat);
      admission?.close();
      server.closeAllConnections();
      await new Promise((resolve) => server.close(resolve));
      await queue.idle();
    },
  };
}

module.exports = { serve };
