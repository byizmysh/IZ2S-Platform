// A TCP connection checks a local UI listener without sending HTTP/native commands.
const net = require('node:net');
function listening(value, {timeoutMs = 400, connect = net.createConnection} = {}) {
  const url = new URL(value);
  if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || url.username || url.password) throw Error('Expected a local module interface');
  return new Promise(resolve => {
    const socket = connect({host:'127.0.0.1',port:Number(url.port || 80)});
    let finished = false;
    const done = state => {if (finished) return; finished=true; socket.destroy(); resolve(state);};
    socket.once('connect',()=>done(true));
    socket.once('error',error=>done(['ECONNREFUSED','ENOENT','ENOTFOUND'].includes(error.code)?false:null));
    socket.setTimeout(timeoutMs,()=>done(null));
  });
}
module.exports = {listening};
