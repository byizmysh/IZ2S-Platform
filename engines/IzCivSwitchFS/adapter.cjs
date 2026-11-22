'use strict';
module.exports = {
  createSession: options => require('./session.cjs').create(options),
  async plan() { throw Error('Use the IZ2SCiv core setup steps.'); },
};
