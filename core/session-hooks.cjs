// Session lifecycle hooks are declared by modules, never by application names.
const registry = require('./modules.cjs');
async function create({
  root = require('./root.cjs'),
  context = {},
  inventory = () => registry.list(root),
  load = (id, name) => require('./components.cjs').create(root).get(id, name),
} = {}) {
  let rows = [],
    available = true,
    storage,
    closed = false,
    pending = null,
    running = null,
    controlBusy = 0,
    closePromise = null;
  const instances = new Map(),
    owners = new Map(),
    sequences = new Map(),
    packets = new Map(),
    held = new Set(),
    stopFailures = new Map(),
    errors = [];
  const workspace=require('./session-workspace.cjs').create({root,
    start:id=>withControl(()=>restartModule(id)),stop:id=>withControl(()=>stopModule(id))});
  const owned=(row,callback)=>workspace.run(row.id,row.descriptor.name||row.id,callback);
  async function withControl(callback) {
    controlBusy++;
    try{await running;if(closed)throw Error('The console session is stopping');return await callback();}
    finally{controlBusy--;if(pending&&!closed&&!controlBusy)onState(pending);}
  }
  async function stopModule(id) {
    if(stopFailures.has(id))throw stopFailures.get(id);
    held.add(id);
    const instance=instances.get(id);
    try{await instance?.close?.();}
    catch(error){stopFailures.set(id,error);throw error;}
    instances.delete(id);
    for(const [group,owner]of owners)if(owner===id)owners.delete(group);
  }
  async function startInstance(row,state) {
    const factory=await load(row.id,row.descriptor.server.sessionHook);
    const instance=await owned(row,()=>factory.create({...context,sdStorage:require('./module-storage.cjs').forModule(row.id)}));
    instance.setVideoStorage?.(storage);instance.setAvailable?.(available);instances.set(row.id,instance);
    await owned(row,()=>instance.onState?.(state));
    await owned(row,async()=>{
      await instance.drain?.();
      // Existing streaming modules publish this interface descriptor rather
      // than calling services.openPanel. Accept it only from their own listener.
      if(context.directory){
        const fs=require('node:fs/promises'),path=require('node:path');
        let panel;try{panel=JSON.parse(await fs.readFile(path.join(context.directory,'pc-screen-local.json'),'utf8'));}catch(error){if(error.code!=='ENOENT')throw error;}
        const scope=require('./session-workspace.cjs').current();
        if(panel?.url&&scope.accepts(panel.url))await scope.open(panel.url);
      }
    });
    return instance;
  }
  async function restartModule(id) {
    if(stopFailures.has(id))throw stopFailures.get(id);
    if(!available)throw Error('Return to the console menu before restarting this module.');
    const row=rows.find(row=>row.id===id),packet=packets.get(id);
    if(!row||!packet)throw Error('Start this module from the console first.');
    await stopModule(id);
    const group=row.descriptor.server.activation?.exclusiveGroup,previous=owners.get(group);
    if(previous&&previous!==id){await instances.get(previous)?.suspend?.();await workspace.mark(previous,'stopped');}
    if(group)owners.set(group,id);
    held.delete(id);await startInstance(row,packet);
  }
  function recordError(error) {
    errors.push(error);
    if (errors.length > 128) errors.splice(0, errors.length - 128);
  }
  async function refresh() {
    rows = (await inventory()).modules.filter(
      (m) => m.descriptor.server?.sessionHook,
    );
  }
  await refresh();
  function requested(row, state) {
    const a = row.descriptor.server.activation;
    if (
      !a ||
      !Number.isInteger(a.offset) ||
      !Number.isInteger(a.sequenceOffset) ||
      a.offset < 0 ||
      a.sequenceOffset < 0 ||
      a.offset + 4 > state.length ||
      a.sequenceOffset + 4 > state.length
    )
      return false;
    const value = state.readFloatLE(a.offset),
      seq = state.readFloatLE(a.sequenceOffset);
    if (
      !Number.isInteger(seq) ||
      seq < 1 ||
      !a.values.includes(value) ||
      stopFailures.has(row.id) ||
      sequences.get(row.id) === seq
    )
      return false;
    sequences.set(row.id, seq);
    held.delete(row.id);packets.set(row.id,Buffer.from(state));
    return true;
  }
  async function consume(state) {
    // Independent modules may use the same console video slot. Join the old
    // owner before handing it over; common controls go only to the active owner.
    if (available) for (const row of rows) {
      const group = row.descriptor.server.activation?.exclusiveGroup;
      if (!group || !requested(row, state) || owners.get(group) === row.id) continue;
      const previous = owners.get(group);
      try {
        await instances.get(previous)?.suspend?.();
        if(previous)await workspace.mark(previous,'stopped');
        owners.set(group, row.id);
      } catch (e) {
        recordError({ id: previous, phase: 'handover', error: e.message });
        return;
      }
    }
    for (const row of rows) {
      const group = row.descriptor.server.activation?.exclusiveGroup;
      if(held.has(row.id))continue;
      if (group && owners.get(group) !== row.id) continue;
      let instance = instances.get(row.id);
      if (!instance) {
        if (!available || (!group && !requested(row, state))) continue;
        try {
          await startInstance(row,state);
          continue;
        } catch (e) {
          recordError({ id: row.id, error: e.message });
          continue;
        }
      }
      try {
        await owned(row,()=>instance.onState?.(state));
      } catch (e) {
        recordError({ id: row.id, error: e.message });
      }
    }
  }
  function onState(state) {
    if (closed) return;
    pending = Buffer.from(state);
    if (running || controlBusy) return;
    running = (async () => {
      while (pending && !closed && !controlBusy) {
        const value = pending;
        pending = null;
        await consume(value);
      }
    })()
      .catch((e) => recordError({ error: e.message }))
      .finally(() => {running=null;if(pending&&!closed&&!controlBusy)onState(pending);});
  }
  return {
    onState,
    refresh,
    errors,
    setVideoStorage: (s) => {
      storage = s;
      for (const v of instances.values()) v.setVideoStorage?.(s);
    },
    setAvailable: (v) => {
      available = !!v;
      for (const i of instances.values()) i.setAvailable?.(available);
    },
    async pollInterval() {
      const values = await Promise.all(
        [...instances.entries()].map(async ([id, value]) => {
          try {
            const interval = (await value.pollInterval?.()) ?? 50;
            if (!Number.isFinite(interval) || interval < 0)
              throw Error('Invalid module polling interval');
            return interval;
          } catch (error) {
            recordError({ id, phase: 'poll', error: error.message });
            return 50;
          }
        }),
      );
      return Math.max(50, ...values);
    },
    async suspend() {
      pending = null;
      await workspace.drain();
      await running;
      await Promise.all([...instances].map(([id,v]) => stopFailures.has(id)?Promise.reject(stopFailures.get(id)):v.suspend?.()));
      for(const id of instances.keys())await workspace.mark(id,'stopped');
    },
    close() {
      if (closePromise) return closePromise;
      closed = true;
      pending = null;
      closePromise = (async () => {
        await workspace.drain();
        await running;
        const owned = [...instances.entries()];
        const results = await Promise.allSettled(owned.map(([id, value]) => stopFailures.has(id)?Promise.reject(stopFailures.get(id)):value.close?.()));
        instances.clear();
        const failed = results.flatMap((result, index) => {
          if (result.status !== 'rejected') return [];
          const id = owned[index][0];
          recordError({ id, phase: 'close', error: result.reason?.message || String(result.reason) });
          return [{ id, error: result.reason }];
        });
        await workspace.close({confirmed:failed.length===0});
        if (failed.length) throw new AggregateError(failed.map(row => row.error), 'Module shutdown unconfirmed: ' + failed.map(row => row.id).join(', '));
      })();
      return closePromise;
    },
  };
}
module.exports = { create };
