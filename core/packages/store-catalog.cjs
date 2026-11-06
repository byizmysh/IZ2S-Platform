// The same pure factory feeds Node and the generated prefix of panel/ui/app.js.
// Match identities so third-party apps remain visible regardless of their names.
function createPolicy() {
  const hidden = new Set([
    'cpu-mailbox-lab',
    'cpu-mailbox-lab-v2',
    'cpu-mailbox-lab-v3',
    'ims-driver-cache-probe',
    'ims-gpu-pcm-cityfast',
    'ims-music-packed-lab',
    'izbrowser-check-20261101',
    'izbrowser-offline-v2-20261101',
    'izbrowser-offline-v3-20261101',
    'izbrowser-offline-v4-20261101',
    'izbrowser-wifi-20261101',
    'izdoom-native18',
    'izdoom-wide18',
    'izoff-012',
    'izsonic-transfer18',
    'ns1-core-constructor-check',
    'ns1-memory-budget-check',
    'ns1-memory-budget-extended-check',
    'ns1-native-cached-memory-check',
    'ns1-paged-core-check',
    'ns1-paged-core-current-check',
    'ns1-paged-kernel-check',
    'ns1-paged-kernel-current-check',
    'ns1-runtime-check',
    'ns1-runtime-tls-check',
    'ns1-vulkan-capabilities-check',
    'ns1-vulkan-memory-check',
    'ns1-vulkan-pool-check',
    'ps2-cpu-interpreter-check',
    'ps2-cpu-lightweight-check',
  ]);
  const hiddenEngines = new Set(['pepsipu', 'izmysh-resume']);
  const izEngines = new Set(['izmysh', 'bermthesequel']);
  const izVariants = new Set(['izlauncher', 'izpslauncher']);

  function visible(descriptor) {
    return !hidden.has(descriptor.id);
  }

  function engineAvailable(loader, variant) {
    if (!loader || hiddenEngines.has(loader.id) || loader.supported === false) return false;
    const declared = Array.isArray(loader.availableIn) ? loader.availableIn
      : Array.isArray(loader.modes) ? loader.modes : null;
    if (variant === 'original') return !declared || declared.includes('original');
    if (!izVariants.has(variant) || !izEngines.has(loader.id)) return false;
    // Older APIs declared only the common IZLauncher transport mode.
    return !declared || declared.some(mode => izVariants.has(mode));
  }

  function normalizeSnapshot(snapshot) {
    if (!snapshot || typeof snapshot !== 'object' || Array.isArray(snapshot)) return snapshot;
    const result = {...snapshot};
    if (Array.isArray(snapshot.library)) result.library = snapshot.library.filter(visible);
    if (Array.isArray(snapshot.moduleCatalog)) result.moduleCatalog = snapshot.moduleCatalog.filter(visible);
    if (snapshot.platform && Array.isArray(snapshot.platform.loaders)) {
      result.platform = {
        ...snapshot.platform,
        loaders: snapshot.platform.loaders.filter(loader => !hiddenEngines.has(loader.id)).map(loader => ({
          ...loader,
          availableIn: ['original', 'izlauncher', 'izpslauncher'].filter(variant => engineAvailable(loader, variant)),
        })),
      };
    }
    // Active sessions, installed apps, jobs, loaderId and connection state stay intact.
    return result;
  }

  return {visible, engineAvailable, normalizeSnapshot};
}

module.exports = {...createPolicy(), createPolicy};
