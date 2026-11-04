const path = require('node:path');
const apiVersion = 1;
function openSession(session) {
  if (!session?.directory) throw Error('Qualified native session required');
  return require(path.join(session.directory, 'shared/rpc.cjs'));
}
function dataDirectory(root, id) {
  if (!/^[a-z0-9][a-z0-9-]{0,47}$/.test(id)) throw Error('Invalid module id');
  return path.join(require('../core/module-paths.cjs').home(root,id), 'data');
}
// A module owns its listener and requests an OS-assigned port by default.
async function listenHttp(server, { port = 0, panelRoot = process.env.IZ2S_ROOT || require('../core/root.cjs') } = {}) {
  if (!Number.isInteger(port) || port < 0 || port > 65535)
    throw Error('Invalid module port');
  require('../core/panel-embedding.cjs').allow(server, await require('../core/panel-embedding.cjs').origin(panelRoot));
  await new Promise((resolve, reject) => {
    const error = (e) => {
      server.removeListener('listening', ready);
      reject(e);
    };
    const ready = () => {
      server.removeListener('error', error);
      resolve();
    };
    server.once('error', error);
    server.once('listening', ready);
    server.listen(port, '127.0.0.1');
  });
  const actualPort = server.address().port;
  const origin='http://127.0.0.1:'+actualPort;
  await require('../core/session-workspace.cjs').current()?.listen(origin);
  return { port: actualPort, origin };
}
function openPanel(url) {
  return require('../core/services/open-panel.cjs').openPanel(url, {
    env: { ...process.env, HB_OPEN_PANEL: '1' },
  });
}
function forModule(root,id,{kind='module'}={}) {
  return {...module.exports,
    sdStorage: require('../core/module-storage.cjs').forModule(id),
    async listenHttp(server, options) {
      return listenHttp(server,{...options,panelRoot:root});
    },
    async openPanel(url, {title=id}={}) {
      const session=require('../core/session-workspace.cjs').current();if(session)return session.open(url);
      return require('../core/workspace.cjs').register(root,{id:(kind==='settings'?'settings-':'module-')+id,title,url,moduleId:id,kind,ownerPid:process.pid});
    },
  };
}
module.exports = {
  apiVersion,
  moduleDirectories: true,
  streamMenuExtensions: true,
  moduleSettings: true,
  moduleWorkspace: true,
  moduleSdStorage: true,
  sdStorage: require('../core/module-storage.cjs').forModule,
  openSession,
  dataDirectory,
  listenHttp,
  openPanel,
  forModule,
};
