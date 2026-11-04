const path = require('node:path');
// A IZ2S page attaches only to this panel's live session, never a nearby RUN20.
async function discover({root = path.resolve(__dirname, '../..'),
  findSession = require('../dolphin-fs-rpc/library.cjs').findSession} = {}) {
  const session = await findSession(root, undefined, {standalone: false});
  return {run: session.run, host: session.host, id: session.id, nativeSession:session.session, source: 'panel'};
}
module.exports = {discover};
