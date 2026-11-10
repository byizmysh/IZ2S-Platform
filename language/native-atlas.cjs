'use strict';
// The packaged console atlases already contain English. Panel localization
// never runs on these binary assets and requires no Python subprocess here.
async function localize(bytes, {kind, shell, language = 'en'} = {}) {
  if (language !== 'en') throw Error('Console interfaces use English');
  if (!Buffer.isBuffer(bytes) || !['focus', 'legacy'].includes(kind) || !['izlauncher', 'izpslauncher'].includes(shell)) throw Error('Invalid interface atlas');
  return Buffer.from(bytes);
}
module.exports = {localize};
