// The parent runner owns the lifetime of an opt-in held FS endpoint.
function waitForRelease(channel = process) {
  if (typeof channel.send !== 'function')
    throw Error('Held FS mode requires the runner IPC channel');
  return new Promise(resolve => {
    let settled = false;
    const finish = reason => {
      if (settled) return;
      settled = true;
      channel.removeListener('message', message);
      channel.removeListener('disconnect', disconnect);
      resolve(reason);
    };
    const message = value => {if (value?.type === 'release') finish('release');};
    const disconnect = () => finish('parent-disconnected');
    channel.on('message', message);
    channel.once('disconnect', disconnect);
    if (channel.connected === false) finish('parent-disconnected');
  });
}

module.exports = {waitForRelease};
