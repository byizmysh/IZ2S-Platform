'use strict';
module.exports = {
  createSession: options => require('./runtime/session.cjs').wrap(require('./session.cjs').create(options),options,'civswitch'),
  async plan() { throw Error('Select CivSwitch in Core only and use its guided controls'); },
};
