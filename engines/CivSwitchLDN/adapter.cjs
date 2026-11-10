'use strict';
module.exports = {
  createSession: options => require('../CivSwitch/runtime/session.cjs').wrap(require('./session.cjs').create(options),options,'civswitch-ldn'),
  async plan() { throw Error('Select CIV7 LAN and follow its setup steps'); },
};
