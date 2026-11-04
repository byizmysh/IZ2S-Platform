'use strict';
const fs = require('node:fs/promises'), path = require('node:path');
const read = file => fs.readFile(file, 'utf8').then(JSON.parse).catch(() => null);

// Acquisition recovery can itself encounter inaccessible memory. Its bridge
// error is secondary: keep the original failed stage and retention decision.
async function inspect(directory, fallback, {runtimeError, stage, directFs = false} = {}) {
  if (!directory) return {message: fallback};
  const workspace = directFs ? directory : path.join(directory, 'fs-rpc');
  const [bridge, endpoint, supervisor, pipeline] = await Promise.all([
    read(path.join(directory, 'bridge-first-failure.json')),
    read(path.join(directory, 'fs-rpc-state.json')),
    read(path.join(workspace, 'switch2/acquisition-supervisor.json')),
    read(path.join(workspace, 'switch2/pipeline.json')),
  ]);
  if (endpoint?.state === 'failed' || directFs && pipeline?.session === path.basename(directory) && pipeline.error) {
    const acquisition = supervisor?.session === path.basename(directory) ? supervisor : null;
    const stage = pipeline?.session === path.basename(directory)
      ? pipeline.stages?.find(row => row.status === 'failed') : null;
    const detail = acquisition?.error || stage?.error || pipeline?.error || endpoint?.detail || endpoint?.error;
    const retained = endpoint?.resourcesRetained === true || acquisition?.resourcesRetained === true;
    const message = acquisition?.resourcesRetained === true
      ? 'FS RPC memory acquisition failed: native execution did not finish reliably. Resources retained; automatic retry disabled'
      : String(detail || fallback).split('\n')[0].replace(/^Error:\s*/, '');
    return {message, stage: stage?.name || endpoint?.failureStage || 'FS RPC initialization',
      resourcesRetained: retained, detail,
      report: acquisition?.attempts?.at(-1)?.report || endpoint?.failureReport};
  }
  if (stage && runtimeError) return {message: stage + ': ' + runtimeError.replace(/^Error:\s*/, ''), stage};
  return {message: bridge?.error || fallback};
}
module.exports = {inspect};
