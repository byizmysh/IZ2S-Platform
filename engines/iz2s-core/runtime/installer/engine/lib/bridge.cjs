const http = require('node:http');

// Stage programs share one native connection through this local, serialized
// bridge. Its address exists only in the current launch directory.
async function serve(native) {
  let queue = Promise.resolve();
  const enqueue = task => {
    const result = queue.then(task);
    queue = result.catch(() => {});
    return result;
  };
  async function handle(request) {
    const {command} = request;
    if (command === 'status') return {ready: !native.connection.error, calls: native.calls};
    if (command === 'call') {
      const reply = await native.call(request.offset, ...request.args);
      return {result: reply.result.toString(), output: reply.output.toString('base64')};
    }
    if (command === 'verify-function') {
      if (typeof request.hex !== 'string' || !/^(?:[0-9a-f]{2}){4,256}$/i.test(request.hex))
        throw Error('Expected original native instruction bytes');
      await native.verifyFunction(request.address, Buffer.from(request.hex, 'hex'));
      return {verified: true};
    }
    if (command === 'call-address') {
      const reply = await native.callAddress(request.address, ...request.args);
      return {result: reply.result.toString(), output: reply.output.toString('base64')};
    }
    if (command === 'pair') {
      const reply = await native.exchange(request.calls);
      return {results: reply.results.map(String), output: reply.output.toString('base64')};
    }
    if (command === 'read') return {data: (await native.read(request.address, request.length)).toString('base64')};
    if (command === 'write') {
      await native.write(request.address, Buffer.from(request.data, 'base64'));
      return {written: true};
    }
    if (command === 'query') return native.query(request.address);
    throw Error('Unknown bridge command');
  }
  const server = http.createServer(async (req, res) => {
    if (req.method !== 'POST' || req.url !== '/') {
      res.writeHead(403).end(); return;
    }
    try {
      const parts = [];
      let length = 0;
      for await (const part of req) {
        length += part.length;
        if (length > 2 * 1024 * 1024) throw Error('Bridge request is too large');
        parts.push(part);
      }
      const request = JSON.parse(Buffer.concat(parts));
      const result = await enqueue(() => handle(request));
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify(result, (_, value) => typeof value === 'bigint' ? value.toString() : value));
    } catch (error) {
      res.writeHead(400, {'Content-Type': 'application/json'}).end(JSON.stringify({error: error.message}));
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  let activity;
  let lastActivity = 0;
  const heartbeat = setInterval(() => {
    enqueue(async () => {
      if (native.connection.error) return;
      if (activity && Date.now() - lastActivity > 30000) {
        const reply = await native.call(activity.offset, activity.controller);
        if (reply.result) throw Error('Console activity report failed');
        lastActivity = Date.now();
      } else await native.call(0x5872f0, native.output, native.output, 0);
    }).catch(error => native.connection.fail(error));
  }, 5000);
  heartbeat.unref();
  return {
    port: server.address().port,
    keepAwake(value) {activity = value;},
    async close() {
      clearInterval(heartbeat);
      server.closeAllConnections();
      await new Promise(resolve => server.close(resolve));
      await queue;
    },
  };
}

module.exports = {serve};
