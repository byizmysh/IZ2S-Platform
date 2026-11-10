'use strict';
const {test} = require('node:test'), assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../panel/ui/app.js'), 'utf8');

// Exercise the actual UI functions with a small DOM; no console or devices are used.
class Element {
  constructor(tag = 'div') {
    this.tagName = tag; this.className = ''; this.children = []; this.dataset = {}; this.hidden = false;
    this.classList = {contains:name=>this.className.split(/\s+/).includes(name),toggle: (name, enabled) => {
      const classes = new Set(this.className.split(/\s+/).filter(Boolean));
      enabled ? classes.add(name) : classes.delete(name); this.className = [...classes].join(' ');
    }};
  }
  append(...nodes) {this.children.push(...nodes);}
  replaceChildren(...nodes) {this.children = nodes;}
  setAttribute(name, value) {this[name] = value;}
  addEventListener() {}
  querySelectorAll(selector) {
    return this.children.flatMap(node => [
      ...(selector[0] === '.' ? node.className.split(/\s+/).includes(selector.slice(1)) : node.tagName === selector) ? [node] : [],
      ...node.querySelectorAll(selector),
    ]);
  }
  querySelector(selector) {return this.querySelectorAll(selector)[0] || null;}
}
function dom() {
  const elements = new Map();
  const $ = id => {if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id);};
  const element = (tag, className = '', textContent = '') => Object.assign(new Element(tag), {className, textContent});
  return {$, element, setText: (node, text) => {node.textContent = String(text);}};
}
test('empty library and Core only mode render successfully across empty/populated transitions', () => {
  const context = {...dom(), URL, selection: new Set(), packageStamp: null, serverUnavailable: false, busy: false,
    state: {library: [], installed: [], jobs: [], platform: {variant: 'original'}, connected: false},
    IZ2S_PRESENTATION:require('../panel/ui/presentation.js'),document:{querySelectorAll:()=>[],addEventListener(){}},libraryContext:null,libraryBatch:false,
    renderModuleHostActions() {}, renderJobs() {}, selectedShellName: () => 'IZlauncher',
    folderButton: () => new Element('button'), workspace: {openModule() {}}, storageControls: {mount:()=>new Element('div'),update(){},renderInventory(){}}};
  context.$('library-connection').append(new Element('span'));context.$('library-search').value='';
  context.applyLibrarySearch=vm.runInNewContext(source.slice(source.indexOf('function applyLibrarySearch()'),source.indexOf('\nfunction resetLibraryFilters()'))+'\napplyLibrarySearch;',context);
  const render = vm.runInNewContext(source.slice(source.indexOf('function renderLibrary()'), source.indexOf('\nfunction renderModuleHostActions()')) + '\nrenderLibrary;', context);
  assert.doesNotThrow(render);
  assert.equal(context.$('app-count').textContent, '0');
  assert.equal(context.$('library').querySelector('.empty').querySelector('h2').textContent, 'No modules installed');
  context.state.library = [{id: 'example', name: 'Example', version: '1.0', bytes: 1024, description: 'Example module'}];
  context.state.installed = ['example'];
  assert.doesNotThrow(render);
  assert.equal(context.$('library').querySelector('.app-card').querySelector('img').title, 'Loaded on console');
  context.state.library = []; context.state.installed = [];
  assert.doesNotThrow(render);
  assert.equal(context.$('library').querySelectorAll('.app-card').length, 0);
  assert.equal(context.$('app-count').textContent, '0');
});
function polling({readError, renderError, readState} = {}) {
  const errors = [], context = {...dom(), polling: null, actionGeneration: 0, initialRenderComplete: false, serverUnavailable: false,
    state: null, workspaceReady: false, notificationKey: null,
    RUN21_CATALOG: {normalizeSnapshot: value => value}, workspace: {sync() {}},
    api: async (route, data, options) => {
      if (route === '/api/state' || route === '/api/startup') {
        if (readError) throw readError;
        if (readState) return readState(route, options);
      }
      return route === '/api/workspace' ? {tabs: []} : {library: []};
    },
    readFsRpcProof: async () => {}, render: () => {if (renderError) throw renderError;},
    renderDolphin() {}, dolphinReady: () => false, notice() {}, console: {error: (...args) => errors.push(args)}};
  const poll = vm.runInNewContext(source.slice(source.indexOf('function poll()'), source.indexOf('\ndocument\n', source.indexOf('function poll()'))) + '\npoll;', context);
  return {context, errors, poll};
}
test('a successful initial status read reveals the panel', async () => {
  const {context, poll} = polling(); await poll();
  assert.equal(context.initialRenderComplete, true);
  assert.equal(context.$('panel-startup').hidden, true);
  assert.equal(context.$('panel-main').hidden, false);
});
test('a rendering failure is not reported as an unavailable server and remains retryable', async () => {
  const {context, errors, poll} = polling({renderError: new TypeError('Missing view element')}); await poll();
  assert.match(context.$('startup-message').textContent, /could not display the server response/);
  assert.equal(context.serverUnavailable, false);
  assert.equal(context.$('startup-retry').hidden, false);
  assert.equal(context.$('startup-retry').disabled, false);
  assert.equal(errors.length, 1);
});
test('an unsuccessful state request still reports the unavailable server', async () => {
  const {context, errors, poll} = polling({readError: new Error('Connection refused')}); await poll();
  assert.match(context.$('startup-message').textContent, /local server is unavailable/);
  assert.equal(context.serverUnavailable, true);
  assert.equal(context.$('startup-retry').disabled, false);
  assert.equal(errors.length, 0);
});
test('cold preparation waits and reveals the panel automatically without a second read', async () => {
  let complete, reads = 0;
  const {context, poll} = polling({readState: (route, options) => {
    reads++;
    assert.equal(route, '/api/startup');
    assert.equal(options.timeoutMs, 120000);
    return new Promise(resolve => { complete = resolve; });
  }});
  const pending = poll();
  assert.equal(poll(), pending);
  assert.match(context.$('startup-message').textContent, /panel will open automatically/);
  assert.equal(context.serverUnavailable, false);
  assert.equal(context.initialRenderComplete, false);
  complete({library: []});
  await pending;
  assert.equal(reads, 1);
  assert.equal(context.initialRenderComplete, true);
});
test('slow preparation retries automatically without calling the server unavailable', async () => {
  const {context, poll} = polling({readError: Object.assign(new Error('Waiting'), {name: 'AbortError'})});
  await poll();
  assert.equal(context.serverUnavailable, false);
  assert.match(context.$('startup-message').textContent, /retry automatically/);
  context.api = async () => ({library: []});
  await poll();
  assert.equal(context.initialRenderComplete, true);
});
test('a preparation error is distinct from a network failure and can be retried', async () => {
  const {context, poll} = polling({readError: Object.assign(new Error('Bad library'), {statusCode: 400})});
  await poll();
  assert.equal(context.serverUnavailable, false);
  assert.match(context.$('startup-message').textContent, /library could not be prepared/);
  assert.equal(context.$('startup-retry').disabled, false);
});
test('ordinary status polling keeps its short deadline after startup', async () => {
  const {context, poll} = polling({readState: (route, options) => {
    assert.equal(route, '/api/state');
    assert.equal(options.timeoutMs, 15000);
    return {library: []};
  }});
  context.initialRenderComplete = true;
  await poll();
  assert.equal(context.serverUnavailable, false);
});
test('Civ polls do not request SmileBASIC workspace or FS proof',async()=>{
 const {context,poll}=polling();const routes=[];let proofs=0;
 context.api=async route=>{routes.push(route);return {library:[],platform:{loaderId:'civswitch-ldn'}};};
 context.readFsRpcProof=async()=>{proofs++;};
 await poll();assert.deepEqual(routes,['/api/startup']);assert.equal(proofs,0);assert.equal(context.workspaceReady,false);
});

test('standalone files preview executes the whole script without out-of-scope panel handlers',()=>{
 const elements=new Map();const document={documentElement:{lang:''},getElementById:id=>{if(!elements.has(id))elements.set(id,{hidden:id!=='panel-startup'});return elements.get(id);}};
 assert.doesNotThrow(()=>vm.runInNewContext(source,{document,location:{search:'?ui-preview=files'},URLSearchParams}));
 assert.equal(document.getElementById('panel-startup').hidden,true);
 assert.equal(document.getElementById('manual-files-document').hidden,false);
});
// Historical source is preserved in the migration archive. Active RUN22 is
// exercised above; cross-build audits must not reopen the old workspace.
test.skip('historical RUN21 panel audit (archived; RUN22 is the active workspace)',()=>{
 const legacy=fs.readFileSync(path.join(__dirname,'../data/migrations/run21-to-run22-20261108/run21-conflicts/panel/ui/app.js'),'utf8');
 const elements=new Map();const document={documentElement:{lang:''},getElementById:id=>{if(!elements.has(id))elements.set(id,{hidden:id!=='panel-startup'});return elements.get(id);}};
 assert.doesNotThrow(()=>vm.runInNewContext(legacy,{document,location:{search:'?ui-preview=files'},URLSearchParams}));
});
