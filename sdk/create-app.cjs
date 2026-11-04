const path = require('node:path'),
  { make } = require('./create-gpu-module.cjs');
if (require.main === module) {
  const id = process.argv[2];
  make(id, path.join(require('../core/root.cjs'), 'modules', id || ''))
    .then(console.log)
    .catch((e) => {
      console.error(e.message);
      process.exitCode = 1;
    });
}
module.exports = { make };
