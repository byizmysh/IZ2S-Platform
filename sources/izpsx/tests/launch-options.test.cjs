'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {validate, qualifyImage} = require('../launch-options.cjs');
const live = {interactive: true, resident: true, continuous: true, frames: 100000, input: null, displayOption: 'av'};
const image = {runtimeFeatures: {continuousResidentPlay: {flag: 16, pacingCounterBits: 64, boundedTrace: true, boundedPerformance: true}}};
test('continuous resident uses the requested count only as capture capacity', () => {
  assert.deepEqual(validate(live), {continuous: true, traceFrames: 100000, frameLimit: null});
  qualifyImage(image, true);
});
test('finite qualification retains its frame limit and old image compatibility', () => {
  assert.deepEqual(validate({interactive: false, resident: false, continuous: false, frames: 2400}),
    {continuous: false, traceFrames: 2400, frameLimit: 2400});
  qualifyImage({}, false);
});
for (const [name, patch] of Object.entries({
  'scripted input': {input: Buffer.alloc(4)},
  'nonresident launch': {resident: false},
  'noninteractive launch': {interactive: false, frames: 300},
  'missing AudioOut': {displayOption: 'display'},
  'unbounded capture allocation': {frames: 100001},
  'zero capture count': {frames: 0},
})) test('rejects ' + name, () => assert.throws(() => validate({...live, ...patch})));
test('old images cannot silently ignore the continuous flag', () => {
  assert.throws(() => qualifyImage({}, true));
  assert.throws(() => qualifyImage({runtimeFeatures: {continuousResidentPlay: {...image.runtimeFeatures.continuousResidentPlay, pacingCounterBits: 32}}}, true));
  assert.throws(() => qualifyImage({runtimeFeatures: {continuousResidentPlay: {...image.runtimeFeatures.continuousResidentPlay, boundedTrace: false}}}, true));
});
