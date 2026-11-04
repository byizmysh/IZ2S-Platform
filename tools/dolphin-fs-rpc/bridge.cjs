#!/usr/bin/env node
const control = require('./control.cjs');
const emit = (type, values = {}) => process.stdout.write(JSON.stringify({type, ...values}) + '\n');
const cancellation = new AbortController();
process.on('SIGTERM', () => cancellation.abort());
process.on('SIGINT', () => cancellation.abort());
async function main() {
  const [command, value] = process.argv.slice(2);
  if (command === 'list') {
    emit('progress', {message: 'Checking FS RPC and reading games…'});
    const result = await control.request('list', {refresh: value === '--refresh'});
    result.games.unshift({title: 'storage', name: 'SD & console storage (read-only)', storages: []});
    return emit('games', result);
  }
  if (command !== 'connect') throw Error('Usage: bridge.cjs list [--refresh] | connect <title-id>');
  let state = await control.request(value === 'storage' ? 'start-storage' : 'start', {title: value});
  const lease = state.lease;
  let announced = false, progress = '';
  while (!cancellation.signal.aborted && state.lease === lease) {
    if (state.progress !== progress) {progress = state.progress; emit('progress', {message: progress || 'Preparing game files…'});}
    if (state.status === 'failed') throw Error(state.error || 'Game export failed');
    if (state.status === 'closed') break;
    if (state.status === 'ready' && !announced) {
      announced = true;
      emit('ready', {game: {title: state.title, name: state.name}, url: state.url, listing: state.listing});
    }
    await new Promise(resolve => setTimeout(resolve, 400));
    state = await control.request('status');
  }
  if (cancellation.signal.aborted) await control.request('stop', {lease});
  emit('closed');
}
main().catch(error => {emit('error', {message: error.message}); process.exitCode = 1;});
