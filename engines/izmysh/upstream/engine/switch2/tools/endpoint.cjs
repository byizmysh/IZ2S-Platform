const net = require('node:net');
const fs = require('node:fs/promises');
const crypto = require('node:crypto');

function requestOnce(socketPath, command, timeoutMs = 300000) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(socketPath);
    let input = '';
    socket.setEncoding('utf8');
    socket.setTimeout(timeoutMs, () => socket.destroy(Error('Memory request timed out; it was not replayed')));
    socket.on('error', reject);
    socket.on('connect', () => socket.write(JSON.stringify(command) + '\n'));
    socket.on('data', part => {
      input += part;
      if (input.length > 16 * 1024 * 1024) socket.destroy(Error('Memory reply exceeded its limit'));
    });
    socket.on('end', () => {
      try {
        const result = JSON.parse(input);
        if (result.error) throw Error(result.error);
        resolve(result);
      } catch (error) {reject(error);}
    });
  });
}

async function request(socketPath, command, timeoutMs = 60000) {
  const deadline = Date.now() + 3000;
  for (;;) {
    try {return await requestOnce(socketPath, command, timeoutMs);}
    catch (error) {
      // The busy reply is sent before dispatch. Every other failure is final.
      if (error.message !== 'Another memory request is active' || Date.now() >= deadline) throw error;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
  }
}

async function read(socketPath, address, length) {
  const result = await request(socketPath, {command: 'read', address, length});
  const bytes = await fs.readFile(result.outputFile);
  if (result.address !== address || result.length !== length || bytes.length !== length ||
      crypto.createHash('sha256').update(bytes).digest('hex') !== result.sha256)
    throw Error('Saved memory read differs from its request or hash');
  return {result, bytes};
}

module.exports = {request, read};
