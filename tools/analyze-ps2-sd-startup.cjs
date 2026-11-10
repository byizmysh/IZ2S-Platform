'use strict';
// Offline reconstruction. Never imports a console client or opens a connection.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const readline = require('node:readline');
const {createReadStream} = require('node:fs');
const assert = require('node:assert/strict');
const ROOT = path.resolve(__dirname, '..');
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
function inside(file) {
  const p = path.resolve(file);
  assert(p.startsWith(ROOT + path.sep), 'Evidence must remain inside RUN22');
  return p;
}
async function analyze(directory) {
  directory = inside(directory);
  const reportFile = path.join(directory, 'report.json');
  const raw = await fs.readFile(reportFile), r = JSON.parse(raw);
  assert(r.phase === 'completed' && r.passed && r.runtime.closed);
  const journal = path.join(directory, 'host-command-journal.jsonl');
  const pending = new Map(), physical = [], progress = [];
  const lines = readline.createInterface({input: createReadStream(journal), crlfDelay: Infinity});
  for await (const line of lines) {
    const e = JSON.parse(line);
    if (!e.operation?.endsWith('.physical.request')) continue;
    if (e.phase === 'before') pending.set(e.id, e);
    else if (e.phase === 'after') {
      const b = pending.get(e.id); assert(b, 'Missing request start'); pending.delete(e.id);
      const q = typeof b.arguments[0] === 'object' ? b.arguments[0] : b.arguments[1];
      const row = {command: q.command, beginMs: b.elapsedMs, endMs: e.elapsedMs,
        durationMs: e.elapsedMs - b.elapsedMs, address: q.address};
      assert(row.durationMs >= 0);
      if (q.command === 'write' && typeof q.data === 'string' && q.data.length <= 24) {
        const data = Buffer.from(q.data, 'base64');
        if (data.length === 8) {
          const stage = data.readUInt32LE(), value = data.readUInt32LE(4);
          if (stage >= 1 && stage <= 6 && value <= 1000)
            progress.push({...row, stage, value});
        }
      }
      physical.push(row);
    }
  }
  assert.equal(pending.size, 0, 'Incomplete physical command evidence');
  const addresses = [...new Set(progress.filter(p => p.stage === 6 && p.value === 1000).map(p => p.address))];
  const stages = addresses.map(address => progress.filter(p => p.address === address))
    .filter(rows => rows.some(p => p.stage === 1 && p.value === 30) && rows.some(p => p.stage === 3 && p.value === 200));
  assert.equal(stages.length, 1, 'Ambiguous launcher progress evidence');
  const rows = stages[0], at = (stage, value) => {
    const found = rows.find(p => p.stage === stage && p.value === value); assert(found); return found.beginMs;
  };
  const readyMs = at(6, 1000), relinkStart = at(3, 200), relinkEnd = at(4, 300);
  const batches = r.runtime.inlineLoaderHistory.slice(0, 2).map((h, index) => {
    const calls = physical.filter(p => p.command === 'call-address' && p.address === h.code && p.beginMs < readyMs);
    const selected = calls.slice(index * h.requests, (index + 1) * h.requests);
    assert.equal(selected.length, h.requests);
    const first = selected[0].beginMs, last = selected.at(-1).endMs;
    const transfers = physical.filter(p => p.beginMs < readyMs && p.endMs >= first - 100 && p.beginMs <= last + 100 &&
      ((p.command === 'write' && p.address === h.context) || (p.command === 'read' && p.address === h.context + 8192)));
    return {phase: index === 0 ? 'map-code-pages' : 'query-every-RX-page', pages: h.operations,
      batches: h.requests, firstCallMs: first, lastCallMs: last, callSpanMs: last - first,
      consoleCallMs: selected.reduce((n, p) => n + p.durationMs, 0),
      existingRoundTrips: h.requests * 3, combinedWriteCallRoundTrips: h.requests * 2,
      roundTripReductionPercent: 100 / 3, nearbyTransfers: transfers.length};
  });
  const operations = {};
  for (const p of physical.filter(p => p.beginMs < readyMs)) {
    const v = operations[p.command] ||= {count: 0, durationMs: 0}; v.count++; v.durationMs += p.durationMs;
  }
  const image = r.runtime.images.find(i => i.image.codeBytes === r.image.codeBytes);
  assert(image.wholeCodeHashVerified && image.imageIntegrity.allBytesHashedOnConsole);
  return {nativeDirectory: directory, game: r.selectedGame, session: r.session,
    evidence: {report: reportFile, reportSha256: sha(raw), journal, journalSha256: sha(await fs.readFile(journal))},
    launcherReadyForEntryMs: readyMs, actualNativeEntryOccursLater: true,
    relinkAndLocalValidationSpanMs: relinkEnd - relinkStart, codeBytes: r.image.codeBytes,
    initialDataBytes: r.image.dataBytes, initialBssBytes: r.image.bssBytes, batches, physicalBeforeReady: operations,
    integrity: image.imageIntegrity,
    SDReadingBandwidthMeasured: false, hostGameReads: r.hostGameReads, hostBiosReads: r.hostBiosReads};
}
async function main() {
  const directories = process.argv.slice(2);
  assert(directories.length > 0);
  const results = [];
  for (const dir of directories) results.push(await analyze(dir));
  const result = {format: 'ps2-sd-startup-offline-analysis/1', passed: true, consoleCommands: 0,
    change: 'Reconstruct launcher preparation from preserved physical command journals',
    metric: 'milliseconds and physical bridge round trips', performanceGainPercent: null,
    before: null, after: null, status: 'Performance gain has not been measured',
    scenario: 'Existing R324 GTA SA and Tekken 5 starts; no new hardware run',
    environment: 'RUN22 local evidence only', results,
    findings: [
      'SD installation does not make this host PS2 path autonomous: runtime-owned relinks and uploads the native image on every launch.',
      'Console-side full-image SHA is already active; downloading the entire code for verification is no longer the current bottleneck.',
      'Mapping and querying all code pages uses separate write/call/read requests for every 64-page batch.',
      'These timings include preparation, transport and journal costs; they do not measure SD card bandwidth.'
    ], limitations: ['No new console commands, no engine changes, no overall startup acceleration or FPS claim.']};
  const out = path.join(ROOT, 'data/diagnostics/ps2-sd-startup-' + Date.now() + '.json');
  await fs.writeFile(out, JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
  console.log(JSON.stringify({passed: true, report: out, runs: results.map(r => ({game: r.game.name,
    readyMs: r.launcherReadyForEntryMs, relinkMs: r.relinkAndLocalValidationSpanMs, batches: r.batches}))}));
}
if (require.main === module) main().catch(e => {console.error(e.stack); process.exitCode = 1;});
module.exports = {analyze};
