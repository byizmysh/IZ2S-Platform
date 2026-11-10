'use strict';
const assert = require('node:assert/strict');

// The requested frame count bounds diagnostic storage. Only the explicit,
// qualified resident capability changes the lifetime of a selected game.
function validate({interactive, resident, continuous, frames, input, displayOption}) {
  assert.ok(Number.isInteger(frames) && frames > 0 && frames <= (interactive ? 100000 : 3600));
  if (resident) assert.ok(interactive && displayOption === 'av' && !input,
    'Resident playback requires live controls and display/audio');
  if (continuous) assert.ok(resident && interactive && !input && displayOption === 'av',
    'Continuous playback is available only in the resident SD picker');
  return {continuous, traceFrames: frames, frameLimit: continuous ? null : frames};
}

function qualifyImage(image, continuous) {
  if (!continuous) return;
  const feature = image.runtimeFeatures?.continuousResidentPlay;
  assert.deepEqual(feature, {
    flag: 16, pacingCounterBits: 64, boundedTrace: true, boundedPerformance: true,
  }, 'Continuous playback needs a newly qualified native image with bounded capture and 64-bit pacing');
}

module.exports = {validate, qualifyImage};
