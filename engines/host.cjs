// The selected acquisition transport feeds one qualified IZlauncher runtime.
// An engine may explicitly retain its FS endpoint before graphics starts.
const fs = require('node:fs/promises'),
  path = require('node:path');
const { chooseHost } = require('./izmysh/server/lib/network.cjs');
const { serve } = require('./izmysh/server/lib/bridge.cjs');
const { saveJson } = require('../core/session/ledger.cjs');
async function main({admission} = {}) {
  const root = path.resolve(__dirname, '..'),
    id = process.env.IZ15_LOADER || 'izmysh';
  if (
    !require('./index.cjs').IDS.includes(id) ||
    !require('./index.cjs').available(id, 'izlauncher')
  )
    throw Error('Unknown acquisition loader');
  const host = await chooseHost(process.argv.slice(2)),
    port = Number(process.env.HB_PORT || 39380),
    settings = JSON.parse(process.env.IZ15_LOADER_SETTINGS || '{}');
  const transport = process.env.IZ15_ENGINE_FOLDER
    ? require(
        path.join(process.env.IZ15_ENGINE_FOLDER, 'transport.cjs'),
      ).create({ host, port, recovery: false, settings })
    : require('./index.cjs').transport(id, {
        host,
        port,
        recovery: false,
        settings,
      });
  const workspace = path.join(root, 'data/sessions');
  if (admission !== undefined) {
    if (!admission || typeof admission.verifyRelease !== 'function' ||
        !path.isAbsolute(admission.clientModule || '') ||
        !/^[0-9a-f]{64}$/.test(admission.clientModuleSha256 || '') ||
        typeof transport.startRuntime !== 'function')
      throw Error('Native admission requires explicit original-host verifier, pinned client and FS runtime');
  }
  await fs.mkdir(workspace, { recursive: true, mode: 0o700 });
  const directory = await fs.mkdtemp(path.join(workspace, 'launcher-'));
  const record = {
    started: new Date().toISOString(),
    purpose: 'Qualified engine session for IZlauncher',
    loader: id,
    transport: { protocol: transport.protocol, resumable: transport.resumable },
    bootstrap: transport.bootstrap,
  };
  let native,
    bridge,
    endpoint,
    stateWrites = Promise.resolve(),
    actualProtocol = transport.protocol,
    actualResumable = transport.resumable;
  const state = (event) => {
    const snapshot = {
      ...event,
      protocol: actualProtocol,
      recoveryAvailable: actualResumable,
      loader: id,
    };
    stateWrites = stateWrites
      .then(async () => {
        await fs.appendFile(
          path.join(directory, 'connection-events.jsonl'),
          JSON.stringify(snapshot) + '\n',
        );
        await saveJson(path.join(directory, 'connection-state.json'), snapshot);
      })
      .catch(() => {});
    console.log('Console link: ' + event.state);
  };
  try {
    await fs.writeFile(
      path.join(directory, 'connect.txt'),
      transport.command + '\n',
      { mode: 0o600 },
    );
    await saveJson(path.join(directory, 'loader.json'), {
      id,
      settings,
      bootstrap: transport.bootstrap,
      protocol: transport.protocol,
      resumable: transport.resumable,
    });
    const connected = await transport.listen({
      onRejected: console.error,
      onResumeState: state,
      onWaiting() {
        console.log('Graphics listener ready at ' + host + ':' + port + '.');
        console.log(
          'Acquisition loader: ' +
            id +
            '. Shared IZlauncher runtime follows qualification.',
        );
        console.log(
          '  LOAD "TXT:' +
            transport.bootstrap.saveName +
            '"\n  RUN\n  REMOVE NSR,&H60000000,&H3FFFFFFF',
        );
        console.log(
          'After the expected Out of memory message, enter:\n  ' +
            transport.command,
        );
        console.log('Run folder: ' + directory);
      },
    });
    native = require('../core/session-api.cjs').validateNative(
      transport.native(connected),
    );
    record.qualification = await transport.qualify(native);
    const peer = connected.connection.socket.remoteAddress?.replace(
      /^::ffff:/,
      '',
    );
    if (require('node:net').isIPv4(peer || ''))
      console.log('Console peer: ' + peer);
    const protocol = connected.addresses.protocol ?? 1;
    actualProtocol = protocol;
    actualResumable = protocol === 2 && transport.resumable;
    record.transport = { protocol, resumable: actualResumable };
    record.reconnectProtocol =
      protocol === 2
        ? 'receipt-v2'
        : 'upstream-legacy: dock reconnect unavailable';
    if (protocol !== 2)
      state({ state: 'connected', protocol, at: new Date().toISOString() });
    console.log(
      protocol === 2
        ? 'Dock reconnect protocol v2 active.'
        : 'Selected upstream bootstrap has no receipt reconnect protocol.',
    );
    // Preserve an in-flight operation during HOME; never replay an uncertain call.
    connected.connection.timeout = 120000;
    record.homeResumeGraceMs = 120000;
    bridge = await serve(native, directory, admission ? {
      admission: {verifyRelease: admission.verifyRelease},
    } : {});
    bridge.keepAwake(record.qualification.activity);
    await fs.mkdir(path.join(directory, 'shared'));
    await fs.copyFile(
      transport.rpcClient || path.join(__dirname, 'izmysh/server/lib/rpc.cjs'),
      path.join(directory, 'shared/rpc.cjs'),
    );
    await fs.writeFile(
      path.join(directory, 'connection.json'),
      JSON.stringify({
        port: bridge.port,
        token: bridge.token,
        addresses: {
          ...native.addresses,
          session: path.basename(directory),
          target: 'switch2',
        },
      }),
      { mode: 0o600 },
    );
    if (transport.startRuntime) {
      console.log('Preparing held FS RPC before graphics.');
      let fsAdmission;
      if (admission) {
        fsAdmission = await require('../core/session/native-admission-config.cjs').provision({
          directory, bridge,
          addresses: {...native.addresses, session: path.basename(directory), target: 'switch2'},
          clientModule: admission.clientModule, clientModuleSha256: admission.clientModuleSha256,
        });
        record.nativeAdmission = {enabled: true, scope: 'sdk-dispatch-with-requested-fs-binding',
          fsRuntimeProvisioned: true, fsRuntimeFencingVerified: false, kernelLeaseGranted: false, helperAttested: false};
      }
      endpoint = await transport.startRuntime({directory, ...(fsAdmission ? {admission: fsAdmission} : {})});
      if (admission) {
        const proof = endpoint.state?.nativeAdmission;
        if (!proof || proof.authorityId !== bridge.admission.channel.authorityId ||
            proof.session !== path.basename(directory) || proof.outerDispatchFenced !== true ||
            proof.nestedSdkScoped !== true || proof.runtimeConfirmed !== true)
          throw Error('FS runtime did not confirm the original host admission binding');
        record.nativeAdmission.fsRuntimeFencingVerified = true;
      }
      record.fsRpc = endpoint.state;
    }
    console.log('Qualified native session ready for graphics inspection.');
    await new Promise((resolve, reject) => {
      const socket = connected.connection.socket;
      const done = () => {
        process.removeListener('SIGINT', done);
        process.removeListener('SIGTERM', done);
        socket.removeListener('close', done);
        resolve();
      };
      socket.once('close', done);
      process.once('SIGINT', done);
      process.once('SIGTERM', done);
      endpoint?.done.then(() => reject(Error('Held FS endpoint exited')), reject);
      if (socket.destroyed || connected.connection.error) done();
    });
    if (native.connection.error)
      record.disconnect = native.connection.error.message;
    if (protocol !== 2)
      state({
        state: 'closed',
        reason: record.disconnect || 'host-stopped',
        at: new Date().toISOString(),
      });
  } catch (error) {
    // An enabled child's initialization may already have reached native code.
    // The original authority keeps its parent alive instead of replay/cleanup.
    bridge?.admission?.retain({});
    record.error = error.stack;
    throw error;
  } finally {
    try {
      await require('../core/session/native-owner-host.cjs').finish({
        directory, session: path.basename(directory), bridge, record,
        reason: record.disconnect || record.error || 'host-stopped',
        onRetain: async status => {
          state({state: 'offline', reason: status.reason, resourcesRetained: true,
            nativeOwnerPhase: status.phase, at: new Date().toISOString()});
          await stateWrites;
          console.log('Native application owner retained: host cleanup parked until joined-clean proof.');
        },
        ...(admission ? {prepareCleanup: async ({admissionCleanup}) => {
          if (typeof endpoint?.close !== 'function') throw Error('Admitted FS endpoint cleanup is unavailable');
          await endpoint.close({admissionCleanup});
        }} : {}),
        cleanup: async ({bridgeAlreadyClosing, endpointAlreadyClosed}) => {
          try {
            try { if (!endpointAlreadyClosed) await endpoint?.close(); }
            finally { if (!bridgeAlreadyClosing) await bridge?.close(); }
          } finally {
            await native?.stop();
          }
        },
      });
    } finally {
      await stateWrites;
      record.completed = new Date().toISOString();
      await fs.writeFile(
        path.join(directory, 'result.json'),
        JSON.stringify(record, null, 2),
        { mode: 0o600 },
      );
    }
  }
}
if (require.main === module)
  main().catch((error) => {
    console.error(error.stack);
    process.exitCode = 1;
  });
module.exports = { main };
