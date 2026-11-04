// Only installed shell contributions may initialize a host component.
function create({ context, load = (id, name) => require('./components.cjs').get(id, name) }) {
  const instances = new Map();
  return {
    async sync(catalog) {
      const retained = new Set(), apps = [];
      for (const app of catalog.apps) {
        if (!app.manifest.builtin || !app.manifest.hostRuntime) { apps.push(app); continue; }
        const key = app.fingerprint;
        try {
          let instance = instances.get(key);
          if (instance) await instance.reset?.();
          else {
            const match = /^module:([a-z0-9-]+)\/([A-Za-z0-9-]+)$/.exec(app.manifest.hostRuntime);
            if (!match) throw Error('Invalid built-in host runtime');
            const component = await load(match[1], match[2]);
            instance = await component.create({ ...context, app, sdStorage: require('./module-storage.cjs').forModule(match[1]) });
            instances.set(key, instance);
          }
          retained.add(key); apps.push(app);
        } catch (error) {
          if (/restore scratch/.test(error.message)) throw error;
          catalog.rejected.push({ folder: app.id, error: 'Host initialization failed: ' + error.message });
        }
      }
      for (const [key, instance] of instances) if (!retained.has(key)) {
        await instance?.close?.(); instances.delete(key);
      }
      catalog.apps = apps;
    },
    async close() {
      try { await Promise.all([...instances.values()].map(instance => instance?.close?.())); }
      finally { instances.clear(); }
    },
  };
}
module.exports = { create };
