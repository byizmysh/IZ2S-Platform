'use strict';
// Civ7 Stability-1: keep optional IZPS2 VU disk persistence disabled.
// This policy is shared by the complete SD kit and individual app exports.
const FLAG = 'vu-split-persist-off.flag';
const CONTENT = 'RUN32 exit isolation: temporarily disable optional VU disk persistence.\n';
function files(id) {
  return id === 'izps2' ? [{name:FLAG, bytes:Buffer.from(CONTENT)}] : [];
}
module.exports = {files};
