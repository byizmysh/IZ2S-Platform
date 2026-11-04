const { StringDecoder } = require('node:string_decoder');

// Observe process output only. No commands, arguments or environment are logged.
function captureProcess({ child, append, onLine = async () => {}, source = 'session',
  maxQueuedRecords = 256, maxQueuedObservers = 256, drainTimeoutMs = 2000 }) {
  if (!Number.isSafeInteger(maxQueuedRecords) || maxQueuedRecords < 1 ||
      !Number.isSafeInteger(maxQueuedObservers) || maxQueuedObservers < 1)
    throw Error('Invalid process output queue limit');
  if (!Number.isFinite(drainTimeoutMs) || drainTimeoutMs < 1)
    throw Error('Invalid process log drain timeout');
  const started = Date.now(), records = [], observers = [], readers = [],
    pendingReaders = [], failures = new Map(), drainChecks = new Set();
  let writerActive = false, observerActive = false, parsing = false, paused = false,
    dropped = 0, totalDropped = 0, droppedAt = null, closeDetails = null, closeQueued = false;
  const rememberError = (phase, error) => {
    if (!failures.has(phase)) failures.set(phase, error);
  };
  const changed = () => { for (const check of [...drainChecks]) check(); };
  const appendDropNotice = () => {
    if (!dropped || records.length >= maxQueuedRecords) return;
    records.push({ type: 'warning',
      message: 'Process log dropped ' + dropped + ' records because its writer could not keep up.',
      event: { at: droppedAt, elapsedMs: Date.now() - started, source,
        pid: child.pid ?? null, droppedRecords: dropped, reason: 'queue-limit' },
    });
    dropped = 0;
    droppedAt = null;
  };
  function writeRecords() {
    if (writerActive || !records.length) return;
    writerActive = true;
    Promise.resolve().then(async () => {
      while (records.length) {
        const { type, message, event } = records.shift();
        try { await append(type, message, event); }
        catch (error) { rememberError('log', error); }
        appendDropNotice();
      }
    }).catch(error => rememberError('log', error)).finally(() => {
      writerActive = false;
      // Output can arrive between the last empty check and this settlement.
      // Restart that work before publishing quiescence to drain().
      writeRecords();
      changed();
    });
  }
  function observeRecords() {
    if (observerActive || !observers.length) return;
    observerActive = true;
    Promise.resolve().then(async () => {
      while (observers.length) {
        const { message, isError } = observers.shift();
        parseReaders();
        try { await onLine(message, isError); }
        catch (error) { rememberError('observer', error); }
      }
    }).catch(error => rememberError('observer', error)).finally(() => {
      observerActive = false;
      parseReaders();
      observeRecords();
      changed();
    });
  }
  function queue(type, message, details = {}, notify = false) {
    const event = { at: new Date().toISOString(), elapsedMs: Date.now() - started,
      source, pid: child.pid ?? null, ...details };
    if (notify) {
      observers.push({ message, isError: event.stream === 'stderr' });
      observeRecords();
    }
    appendDropNotice();
    if (records.length < maxQueuedRecords) records.push({ type, message, event });
    else { dropped++; totalDropped++; droppedAt ||= event.at; }
    writeRecords();
    changed();
  }
  function setPaused(value) {
    if (paused === value) return;
    paused = value;
    for (const reader of readers) reader.stream?.[value ? 'pause' : 'resume']?.();
  }
  function scheduleReader(reader) {
    if (reader.queued) return;
    reader.queued = true;
    pendingReaders.push(reader);
  }
  function maybeQueueClose() {
    if (!closeDetails || closeQueued || readers.some(reader => !reader.final || reader.buffer.length)) return;
    closeQueued = true;
    const { code, signal } = closeDetails;
    queue('process', `Session process closed (exit code: ${code ?? 'none'}, signal: ${signal || 'none'})`, { code, signal });
  }
  function parseReaders() {
    if (parsing) return;
    parsing = true;
    try {
      while (pendingReaders.length) {
        const reader = pendingReaders[0], newline = reader.buffer.indexOf('\n');
        let length = -1, skip = 0;
        if (newline >= 0 && newline <= 1400) { length = newline; skip = 1; }
        else if (reader.buffer.length > 1400) length = 1400;
        else if (reader.final && reader.buffer.length) length = reader.buffer.length;
        if (length < 0) {
          pendingReaders.shift(); reader.queued = false;
          continue;
        }
        if (observers.length >= maxQueuedObservers) break;
        const line = reader.buffer.slice(0, length).replace(/\r$/, '');
        reader.buffer = reader.buffer.slice(length + skip);
        if (line.trim()) queue('detail', line, { stream: reader.name }, true);
      }
      setPaused(observers.length >= maxQueuedObservers || pendingReaders.length > 0);
      maybeQueueClose();
    } finally { parsing = false; }
    changed();
  }
  function finishReader(reader, ended = false) {
    reader.finishRequested = true;
    if (reader.final || (!ended && reader.stream?.readableLength > 0)) return;
    reader.buffer += reader.decoder.end();
    reader.final = true;
    scheduleReader(reader);
    parseReaders();
  }
  for (const name of ['stdout', 'stderr']) {
    const reader = { name, stream: child[name], decoder: new StringDecoder('utf8'),
      buffer: '', final: false, finishRequested: false, queued: false };
    readers.push(reader);
    reader.stream?.on('data', bytes => {
      reader.buffer += reader.decoder.write(bytes);
      scheduleReader(reader);
      if (reader.finishRequested && !reader.stream.readableLength) finishReader(reader);
      parseReaders();
    });
    reader.stream?.once('end', () => finishReader(reader, true));
  }
  queue('process', 'Session process requested');
  child.once('spawn', () => queue('process', 'Session process started'));
  child.once('error', error => queue('process', 'Session process error: ' + error.message, { code: error.code ?? null }));
  child.once('close', (code, signal) => {
    closeDetails = { code, signal };
    readers.forEach(reader => finishReader(reader));
    maybeQueueClose();
    changed();
  });
  function isQuiet() {
    return !writerActive && !observerActive && !records.length && !observers.length &&
      !pendingReaders.length && (!closeDetails || closeQueued);
  }
  return {
    status: { get queuedObservers() { return observers.length; }, get queuedRecords() { return records.length; },
      get paused() { return paused; }, get bufferedCharacters() { return readers.reduce((sum, reader) => sum + reader.buffer.length, 0); } },
    drain: async () => {
      let timer, timeoutError, check;
      try {
        await new Promise(resolve => {
          check = () => { if (isQuiet()) resolve(); };
          drainChecks.add(check);
          timer = setTimeout(() => {
            timeoutError = Object.assign(Error('Process output drain timed out after ' + drainTimeoutMs +
              ' ms; ' + totalDropped + ' log records dropped.'), { code: 'ERR_PROCESS_LOG_DRAIN_TIMEOUT',
              droppedRecords: totalDropped });
            resolve();
          }, drainTimeoutMs);
          check();
        });
      } finally { clearTimeout(timer); drainChecks.delete(check); }
      const errors = [...failures.values(), ...(timeoutError ? [timeoutError] : [])];
      if (errors.length === 1) throw errors[0];
      if (errors.length > 1)
        throw new AggregateError(errors, 'Process output handling failed: ' + errors.map(error => error.message).join('; '));
    },
  };
}

module.exports = { captureProcess };
