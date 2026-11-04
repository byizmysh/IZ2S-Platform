const net = require('node:net');
const readline = require('node:readline');

async function listing(host, port) {
  if (!net.isIPv4(host) || !Number.isInteger(port) || port < 1024 || port > 65535) throw Error('Invalid FTP address');
  const socket = net.createConnection({host, port});
  const replies = [], waiting = [];
  let failure, multiline, dataSocket;
  const fail = error => {failure = error; while (waiting.length) waiting.shift().reject(error);};
  socket.on('error', fail);
  socket.setTimeout(15000, () => socket.destroy(Error('FTP response timed out')));
  socket.on('end', () => {if (waiting.length) fail(Error('FTP connection ended before its reply'));});
  const lines = readline.createInterface({input: socket});
  lines.on('line', line => {
    if (multiline && !line.startsWith(multiline + ' ')) return;
    const match = line.match(/^(\d{3})([- ])(.*)$/);
    if (!match) return;
    if (match[2] === '-') {multiline = match[1]; return;}
    multiline = null;
    const reply = {code: Number(match[1]), message: match[3]};
    if (waiting.length) waiting.shift().resolve(reply); else replies.push(reply);
  });
  const next = () => failure ? Promise.reject(failure) : replies.length ? Promise.resolve(replies.shift()) : new Promise((resolve, reject) => waiting.push({resolve, reject}));
  async function command(text, expected) {
    socket.write(text + '\r\n');
    const reply = await next();
    if (!expected.includes(reply.code)) throw Error('FTP ' + reply.code + ': ' + reply.message);
    return reply;
  }
  try {
    const welcome = await next();
    if (welcome.code !== 220) throw Error('Unexpected FTP greeting');
    const login = await command('USER anonymous', [230, 331]);
    if (login.code === 331) await command('PASS anonymous@', [230]);
    const passive = await command('PASV', [227]);
    const address = passive.message.match(/\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)/);
    if (!address || address.slice(1).some(n => Number(n) > 255)) throw Error('Invalid passive FTP response');
    const dataPort = Number(address[5]) * 256 + Number(address[6]);
    if (!dataPort) throw Error('Invalid passive FTP port');
    // Ignore the advertised PASV host; data always uses the selected console.
    dataSocket = net.createConnection({host, port: dataPort});
    const parts = []; let size = 0;
    const complete = new Promise((resolve, reject) => {
      dataSocket.on('error', reject);
      dataSocket.setTimeout(15000, () => dataSocket.destroy(Error('FTP data timed out')));
      dataSocket.on('data', bytes => {size += bytes.length; if (size > 4 * 1024 * 1024) dataSocket.destroy(Error('FTP listing too large')); else parts.push(bytes);});
      dataSocket.once('end', resolve);
    });
    complete.catch(() => {});
    await new Promise((resolve, reject) => {dataSocket.once('connect', resolve); dataSocket.once('error', reject);});
    await command('LIST /', [125, 150]);
    await complete;
    const done = await next();
    if (done.code !== 226) throw Error('FTP listing did not complete');
    await command('QUIT', [221]);
    return Buffer.concat(parts).toString('utf8').split(/\r?\n/).filter(Boolean);
  } finally {dataSocket?.destroy(); lines.close(); socket.destroy();}
}
module.exports = {listing};
