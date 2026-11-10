'use strict';
// Wait only for an actually observed process close; a deadline is not proof of
// native cleanup. The caller retains its ownership policy when time runs out.
module.exports = function waitForClose(child, graceMs) {
  return new Promise(resolve => {
    const finish = () => { clearTimeout(timer); child.removeListener('close', finish); resolve(); };
    const timer = setTimeout(finish, graceMs);
    child.once('close', finish);
  });
};
