// Historical receipt-protocol listener. The active ordinary bootstrap does not use it.
const net=require('node:net');
const {Connection,HELLO_BYTES,pairingCode,parseHello}=require('../server/lib/connection.cjs');
async function listen({host, port, token, onWaiting, onRejected, onResumeState, timeout = 10 * 60 * 1000}) {
  if (!net.isIPv4(host)) throw Error('Use the computer\'s LAN IPv4 address');
  if (!Number.isInteger(port) || port < 1024 || port > 65535) throw Error('Port must be 1024..65535');
  const pending = new Set();
  const server = net.createServer();
  let timer, finished = false, resumeSession, resumeAddresses;
  return new Promise((resolve, reject) => {
    const stop = () => {finished = true; clearTimeout(timer); server.close();};
    const fail = error => {
      if (finished) return;
      stop();
      for (const connection of pending) connection.close();
      reject(error);
    };
    server.on('error', fail);
    server.on('connection', socket => {
      if (finished && !resumeSession || pending.size >= 4) {socket.destroy(); return;}
      socket.setKeepAlive(true,5000);
      const connection = new Connection(socket, 10000);
      pending.add(connection);
      connection.read(HELLO_BYTES).then(async bytes => {
        const addresses = parseHello(bytes, token, true);
        if(addresses.protocol===2){
          const {SessionConnection,receipt}={...require('./session-resume.cjs'),...require('./recovery-connection.cjs')};
          const proof=receipt(await connection.read(32));
          if(resumeSession){
            if(JSON.stringify(addresses)!==JSON.stringify(resumeAddresses))throw Error('Reconnect belongs to a different console process');
            await resumeSession.attach(connection,proof);pending.delete(connection);return;
          }
          if(finished||proof.sequence!==0)throw Error('Cannot resume a previous launch');
          const session=new SessionConnection({onState:onResumeState});
          resumeSession=session;resumeAddresses=addresses;finished=true;clearTimeout(timer);pending.delete(connection);
          for(const other of pending)other.close();
          session.once('close',()=>{resumeSession=null;server.close();for(const other of pending)other.close();});
          await session.attach(connection,proof);
          resolve({connection:session,addresses});return;
        }
        if (finished) throw Error('Pairing has ended');
        pending.delete(connection);
        stop();
        for (const other of pending) other.close();
        connection.timeout = 30000;
        resolve({connection, addresses});
      }).catch(error => {
        pending.delete(connection);
        connection.close();
        if (!finished || resumeSession) onRejected?.(error.message);
      });
    });
    timer = setTimeout(() => fail(Error('No console connected before the pairing deadline')), timeout);
    server.listen(port, host, () => {
      try {onWaiting?.();}
      catch (error) {fail(error);}
    });
  });
}

module.exports={listen,pairingCode};
