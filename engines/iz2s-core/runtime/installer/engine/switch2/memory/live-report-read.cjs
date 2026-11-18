'use strict';

// Retry only reads of the host's replace-in-place JSON report. This never
// retries a console request, native operation, worker wave, or acquisition.
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

function isTransient(error) {
  return error instanceof SyntaxError || ['ENOENT', 'ENODATA'].includes(error?.code);
}

async function readLiveReport(file, reader, options = {}) {
  if (typeof reader !== 'function') throw Error('A live-report reader is required');
  const attempts = options.attempts ?? 8;
  const delayMs = options.delayMs ?? 25;
  if (!Number.isInteger(attempts) || attempts < 1 || attempts > 8 ||
      !Number.isInteger(delayMs) || delayMs < 0 || delayMs > 25)
    throw Error('Invalid live-report retry bound');
  let lastError;
  for (let attempt = 0; attempt < attempts; attempt++) {
    try { return await reader(file); }
    catch (error) {
      if (!isTransient(error)) throw error;
      lastError = error;
      if (attempt + 1 < attempts) await delay(delayMs);
    }
  }
  throw lastError;
}

module.exports = {isTransient, readLiveReport};
