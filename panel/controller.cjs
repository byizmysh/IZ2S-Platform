const path = require('node:path'),
  { readJson, saveJson, createLedger } = require('../core/session/ledger.cjs');
async function create({ root, settings: defaults, node, preview = false }) {
  const file = path.join(root, 'data/state/launcher-settings.json'),
    loaders = require('../engines/index.cjs').create(root),
    configurations = require('../hardware/profiles.cjs');
  const cardputerOperation=require('../core/services/cancellable-operation.cjs').create();
  const saved = await readJson(file, {});
  let choice = {
      loaderId: 'iz2s-core',
      variant: 'izlauncher',
      loaderSettings: {},
      installationSlots: {},
      sdLauncher: false,
      nativeRecovery: false,
      residentGraphics: false,
      residentFiniteAudio: false,
      ...saved,
    },
    settings,
    configuration,
    connections,
    input,
    devices,
    iz,
    upstream,
    delivery,
    configState,
    initializedInstalled,
    changing = false,
    lastInputDiscoveryAt = 0;
  const panelCatalog = require('../core/packages/store-catalog.cjs');
  const repository = require('../core/packages/repository.cjs');
  const coreLibrary = await repository.create(root, {engine: 'iz2s-core', source: 'current'});
  let libraryRead, librarySnapshot, libraryReadAt = 0;
  async function readCoreLibrary() {
    if (librarySnapshot && Date.now() - libraryReadAt < 2000) return librarySnapshot;
    if (!libraryRead) libraryRead = coreLibrary.list().then(result => {
      librarySnapshot = {packages: result.packages.map(repository.publicPackage), errors: result.errors};
      libraryReadAt = Date.now();
      return librarySnapshot;
    }).finally(() => { libraryRead = null; });
    return libraryRead;
  }

  const panelLoaders = () => panelCatalog.normalizeSnapshot({platform:{loaders:loaders.list()}}).platform.loaders;
  const available = (id, variant) => panelCatalog.engineAvailable(loaders.list().find(loader => loader.id === id), variant);
  const availableLoaders = new Set(panelLoaders().map(loader => loader.id));
  if (['civswitch','civswitch-ldn'].includes(choice.loaderId)) { choice.loaderId='iz2sciv-core'; choice.variant='original'; }
  choice.residentGraphics = false;
  choice.residentFiniteAudio = false;
  if (!availableLoaders.has(choice.loaderId)) choice.loaderId='iz2s-core';
  choice.loaderSettings=Object.fromEntries(Object.entries(choice.loaderSettings||{}).filter(([id])=>availableLoaders.has(id)));
  choice.installationSlots=Object.fromEntries(Object.entries(choice.installationSlots||{}).filter(([id])=>availableLoaders.has(id)));
  if (
    choice.variant !== 'original' &&
    !available(choice.loaderId, choice.variant)
  )
    choice.loaderId = 'iz2s-core';
  choice.variant = choice.loaderId === 'iz2s-core' ? 'izlauncher' : 'original';
  choice.sdLauncher = false;
  choice.nativeRecovery = false;
  choice.schemaVersion = 3;
  const isIZ = () => choice.variant !== 'original',
    engine = () => iz || upstream;
  const selectedLoader = () =>
    loaders.select(
      choice.loaderId,
      choice.loaderSettings?.[choice.loaderId] || {},
      { installSlot: choice.installationSlots?.[choice.loaderId] },
    );
  const originalPlan = () =>
    selectedLoader().plan({
      host: settings.consoleHost,
      node,
      input: 'manual',
      port: settings.consolePort,
      runtime: 'original',
    });
  async function initialize() {
    const installationReady = selectedLoader().manifest.installed !== false;
    configState = await configurations.load(root);
    let inputDiscovery;
    const pairingDevice={'pc-cardputer-adv':'cardputer-adv','pc-switch1':'switch1'}[configState.selected];
    if(!preview&&pairingDevice){
      const profile=configState.profiles?.[configState.selected]||{};
      const restored=await require('../hardware/devices/'+pairingDevice+'/pairing.cjs').restore({root,profile});
      inputDiscovery=restored.discovery;
      if(JSON.stringify(restored.profile)!==JSON.stringify(profile)){
        configState={...configState,profiles:{...configState.profiles,[configState.selected]:restored.profile}};
        await configurations.save(root,configState);
      }
    }
    configuration = await configurations.resolve(configState.selected, {
      root,
      settings: defaults,
      profile: configState.profiles?.[configState.selected] || {},
    });
    configuration.inputDiscovery=inputDiscovery;
    settings = configuration.settings;
    if (configuration.inputAdapter === 'cardputer-wifi' && inputDiscovery?.paired && inputDiscovery.endpoint)
      settings.cardputerEndpoint = inputDiscovery.endpoint;
    settings.consolePort =
      selectedLoader().manifest.consolePort ?? settings.consolePort;
    configuration.env = {
      ...configuration.env,
      HB_PORT: String(settings.consolePort),
    };
    connections =
      await require('../core/session/connections.cjs').createConnections({
        root,
        settings,
        networkEnabled: !preview,
      });
    const currentConfiguration=configuration;
    const recoverAddress=configuration.inputAdapter==='switch-wifi'?async()=>{
      if(changing||configuration!==currentConfiguration)return;
      const id=configuration.id,previous=configState.profiles?.[id]||{};
      const restored=await require('../hardware/devices/switch1/pairing.cjs').restore({root,profile:previous});
      if(changing||configuration!==currentConfiguration||configState.profiles?.[id]!==previous)return;
      configuration.inputDiscovery=restored.discovery;
      if(!restored.discovery.paired)return;
      const next={...configState,profiles:{...configState.profiles,[id]:restored.profile}};
      await configurations.save(root,next);configState=next;
      Object.assign(settings,{switchAddress:restored.profile.switchAddress,switchPort:restored.profile.switchPort,switchToken:restored.profile.switchToken});
    }:undefined;
    input = await configurations.createInputLink({ root, configuration, recoverAddress });
    devices = await configurations.createChecks({
      root,
      configuration,
      settings,
      node,
      arm: input,
      connections,
      ownsListener: () =>
        iz?.ownsListener() || upstream?.ownsListener() || false,
    });
    delivery = null;
    iz = null;
    const sessionFactory = ['civswitch', 'civswitch-ldn', 'iz2sciv-core'].includes(choice.loaderId)
      ? require(path.join(selectedLoader().folder, 'adapter.cjs')).createSession
      : require('./upstream-session.cjs').create;
    upstream = sessionFactory({
      root,
      settings,
      node,
      arm: input,
      devices,
      connections,
      preview,
      environment: configuration.env,
      selection: selectedLoader,
      configuration,
    });
    initializedInstalled = installationReady;
    lastInputDiscoveryAt = Date.now();
  }
  await initialize();
  await saveJson(file, choice);
  async function assertIdle() {
    const state = await engine().snapshot({statusOnly:true});
    if (changing || engine().ownsListener() || state.changing || state.active || state.emergencyInputPending)
      throw Error('Stop the current session before changing launch settings');
  }
  async function reconcileInstallation() {
    if (changing || initializedInstalled === (selectedLoader().manifest.installed !== false)) return;
    const current = await engine().snapshot();
    if (changing || engine().ownsListener() || current.active || current.emergencyInputPending) return;
    changing = true;
    try { await initialize(); }
    finally { changing = false; }
  }
  async function prepareCardputerBundle(signal){
    signal.throwIfAborted();
    const bundle=await require('../hardware/cardputer.cjs').prepare(root,{profile:choice.loaderId==='civswitch'?'civ':'sb4',signal});
    signal.throwIfAborted();
    const id='pc-cardputer-adv';configState=await configurations.load(root);
    await configurations.save(root,{selected:configState.selected,profiles:{...configState.profiles,[id]:{
      ...configState.profiles?.[id],cardputerEndpoint:configState.profiles?.[id]?.cardputerEndpoint||'http://iz2s-adv.local',
      cardputerToken:bundle.token,cardputerCapsLock:configState.profiles?.[id]?.cardputerCapsLock||'off',
    }}});
    signal.throwIfAborted();await initialize();signal.throwIfAborted();return bundle;
  }
  async function prepareSwitchKeyboardBundle(){
    const bundle=await require('../hardware/switch-keyboard.cjs').prepare(root);
    const id='pc-switch1';configState=await configurations.load(root);
    await configurations.save(root,{selected:configState.selected,profiles:{...configState.profiles,[id]:{
      ...configState.profiles?.[id],switchToken:bundle.token,
    }}});
    await initialize();return bundle;
  }
  return {
    preview,
    inputLink(selected) {
      if(selected!==configuration.id)throw Error('Input device selection changed; refresh the panel before starting.');
      if(configuration.inputDiscovery?.ambiguous || configuration.inputDiscovery?.incomplete)
        throw Error('Choose the address shown on your input device before continuing.');
      if(configuration.inputAdapter==='ssh-gadget' && !settings.armAddress)
        throw Error('Choose the address of your ARM input device.');
      return input;
    },
    inspectNS2HB: () => require('./ns2hb.cjs').inspect(root, choice.variant),
    refresh: async (...args) => {
      if(changing)return;
      // Recovery used to reinitialize an unpaired device on every UI poll.
      // Its temporary lock then made the picker permanently unavailable.
      // Civ7 uses its own wireless transport, not the SB4 USB keyboard. Replacing
      // its session to recover an unrelated input device loses live operations.
      if(!preview&&choice.loaderId!=='iz2sciv-core'&&(configuration.setupRequired||configuration.inputDiscovery?.paired===false||
          (configuration.inputAdapter==='cardputer-wifi'&&args[0]===true))&&['cardputer-wifi','switch-wifi'].includes(configuration.inputAdapter)&&
          (args[0]===true||Date.now()-lastInputDiscoveryAt>=30000)){
        const idle = await assertIdle().then(()=>true,()=>false);
        if (idle && !changing && !engine().ownsListener()) { changing=true;try{await initialize();}finally{changing=false;} }
      }
      return engine().refresh(...args);
    },
    begin: async (body) => {
      await reconcileInstallation();
      if(configuration.externalWorkflow)throw Error('Use the native companion app for this configuration');
      if (selectedLoader().manifest.installed === false)
        throw Error('Download or manually install the original engine first');
      if (!available(choice.loaderId, choice.variant))
        throw Error('Selected loader is unavailable for IZ2S');
      if(configuration.inputAdapter==='switch-wifi'&&['civswitch','civswitch-ldn'].includes(choice.loaderId))throw Error('Select ARM PC or Cardputer ADV for Civilization VII controller input. Switch 1 keyboard is configured for SmileBASIC 4.');
      if (choice.loaderId === 'iz2s-core') throw Error('Use the IZ2S core launch action.');
      return engine().begin(body);
    },
    stop: (...args) => engine().stop(...args),
    hardStop: (...args) => engine().hardStop(...args),
    ownsListener: () => engine().ownsListener(),
    async snapshot() {
      await reconcileInstallation();
      const state = await engine().snapshot(),
        // IZ2s shares the same local module library after either game.
        moduleInventory = isIZ() || choice.loaderId==='iz2sciv-core' || state.civLauncher?.state==='running' ? await require('../core/modules.cjs').metadata(root) : {modules: [], errors: []},
        extensions = require('../core/extensions.cjs').catalog(root);
      const library = isIZ() || choice.loaderId==='iz2sciv-core' ? await readCoreLibrary() : null;
      let base, current, baseSource;
      if (delivery) {
        const profile = await delivery.profile();
        base = delivery.installation(false);
        current = await delivery.transport(!!profile.recovery);
        current = {
          ...current,
          bootstrap: {
            ...current.bootstrap,
            saveName: await delivery.connectionBootstrapName(
              !!profile.recovery,
            ),
          },
        };
        baseSource = await delivery.sourceChecks.readPlan(root, false);
      } else {
        const plan = selectedLoader().sourcePlan();
        base = current = {
          bootstrap: plan.bootstrap || { saveName: null },
          preloaded: plan.startup?.preloaded === true,
        };
      }
      return {
        ...state,
        ...(library ? {library: library.packages} : {}),
        fsRpc: state.active?.runDirectory
          ? await readJson(path.join(state.active.runDirectory, 'fs-rpc-state.json'), null)
          : null,
        checks:['civswitch-ldn','iz2sciv-core'].includes(choice.loaderId)?state.checks:require('../hardware/index.cjs').present(configuration,state.checks),
        moduleCatalog: moduleInventory.modules.map((m) => m.descriptor).filter(panelCatalog.visible),
        moduleErrors: [...moduleInventory.errors, ...(library?.errors || [])],
        moduleStates: require('../core/host-modules.cjs').status(),
        hardwareOperation:cardputerOperation.snapshot(),
        version: 22,
        platform: {
          civswitchLdnAvailable: true,
          preparationCancelAvailable:true,
          host: { platform: process.platform, architecture: process.arch },
          loaderId: choice.loaderId,
          variant: choice.variant,
          consoleEndpoint: {port:settings.consolePort,
            host:choice.loaderId==='iz2sciv-core' ? state.iz2sciv?.host||'' : choice.loaderId==='civswitch-ldn' ? state.civswitchLdn?.suggestedHost||settings.consoleAddress : choice.loaderId==='civswitch' ? state.civswitch?.networkHost||settings.consoleAddress : settings.consoleHost,
            listener:['civswitch','civswitch-ldn','iz2sciv-core'].includes(choice.loaderId)?'console':'pc'},
          sdLauncher: choice.sdLauncher===true,
          nativeRecovery: selectedLoader().settings.fsRpcMode===true,
          residentGraphics: choice.residentGraphics===true,
          residentFiniteAudio: choice.residentFiniteAudio===true,
          residentAudioReady: false,
          shells: extensions.shells.items,
          extensionErrors: [
            ...extensions.engines.errors,
            ...extensions.shells.errors,
            ...extensions.profiles.errors,
            ...require('../hardware/index.cjs').configurations(root).errors,
          ],
          loaders: panelLoaders(),
          loaderSettings: selectedLoader().settings,
          engineInstalled: selectedLoader().manifest.installed !== false,
          engineInstallation: selectedLoader().manifest.installationStatus,
          installation: base.bootstrap.file
            ? {
                slot: base.bootstrap.sourceSlot,
                name: base.bootstrap.saveName,
                ...require('../core/session/install-slot.cjs').commands(
                  base.bootstrap.saveName,
                  base.bootstrap.sourceSlot,
                ),
              }
            : null,
          izModules: isIZ() || state.civLauncher?.state==='running',
          baseSource,
          delivery: {
            application: isIZ() ? (choice.variant === 'izpslauncher' ? 'IZPSlauncher' : 'IZ2S') : 'Core only',
            method: choice.loaderId,
            bootstrapName: current.bootstrap.saveName,
            baseBootstrapName: base.bootstrap.saveName,
            recoveryBootstrapName: delivery
              ? delivery.installation(true).bootstrap.saveName
              : base.bootstrap.saveName,
            preloaded: current.preloaded === true,
          },
          configurationId: configuration.id,
          configuration: {
            id: configuration.id,
            name: configuration.name,
            setupRequired: configuration.setupRequired,
            requirements: configuration.requirements,
            executionHost: configuration.executionHost,
            inputAdapter: configuration.inputAdapter,
            inputDirectory:settings.deviceFolder,
            inputDiscovery:configuration.inputDiscovery,
            externalWorkflow:configuration.externalWorkflow,
            hardware:configuration.hardware,
            ui:configuration.ui,
          },
          configurations: await configurations.list(root),
          profiles: configState.profiles || {},
          locked: !!state.active || !!state.emergencyInputPending || changing,
        },
      };
    },
    async prepareSwitchKeyboardSD() {
      await assertIdle();
      if(preview)throw Error('Prepare the SD package from the running panel');
      if(configuration.inputAdapter!=='switch-wifi')throw Error('Select Switch 1 first');
      changing=true;
      try{const bundle=await prepareSwitchKeyboardBundle();return {ok:true,filename:path.basename(bundle.path)};}
      finally{changing=false;}
    },
    async installSwitchKeyboard() {
      if(process.platform!=='linux')throw Error('Download the SD package and extract the ZIP to the root of the Switch 1 SD card.');
      await assertIdle();
      if(preview)throw Error('Preview cannot install homebrew');
      if(configuration.inputAdapter!=='switch-wifi')throw Error('Select Switch 1 first');
      changing=true;
      try{const bundle=await prepareSwitchKeyboardBundle();return await require('../hardware/switch-keyboard.cjs').install(root,bundle);}
      finally{changing=false;}
    },
    async confirmCardputerUsb() {
      await assertIdle();
      if(preview)throw Error('Preview cannot confirm device connections');
      if(configuration.inputAdapter!=='cardputer-wifi')throw Error('Select Cardputer ADV first');
      changing=true;
      try {
        const result=await input.confirmUsb();
        await engine().refresh(true);
        return result;
      } finally {changing=false;}
    },
    async prepareCardputerSD(body={}) {
      return cardputerOperation.run({id:body.requestId,kind:'package'},async signal=>{
        await assertIdle();signal.throwIfAborted();
        if(preview)throw Error('Prepare the SD package from the running panel');
        changing=true;
        try{await prepareCardputerBundle(signal);signal.throwIfAborted();return {ok:true,filename:'iz2s-cardputer-adv-sd.zip'};}
        finally{changing=false;}
      });
    },
    async installCardputerSD(body={}) {
      return cardputerOperation.run({id:body.requestId,kind:'firmware'},async signal=>{
        await assertIdle();signal.throwIfAborted();
        if(preview)throw Error('Preview cannot copy firmware');
        changing=true;
        try{
          const bundle=await prepareCardputerBundle(signal);signal.throwIfAborted();
          return await require('../hardware/cardputer.cjs').install(root,{target:body.target,profile:bundle.profile,bundle,signal,onProgress:value=>cardputerOperation.progress(value)});
        }finally{changing=false;}
      });
    },
    cancelCardputerSD:body=>cardputerOperation.cancel(body?.requestId),
    async installEngine(id) {
      await assertIdle();
      if (preview) throw Error('Preview does not download engines');
      changing = true;
      try {
        const result = await require('../core/engine-install.cjs').download(
          root,
          id,
        );
        await initialize();
        return result;
      } finally {
        changing = false;
      }
    },
    async rescanEngine() {
      await assertIdle();
      changing = true;
      try {
        await initialize();
        const selected = selectedLoader().manifest;
        if (selected.installed === false)
          throw Error(selected.installationStatus?.error || 'The engine source is incomplete');
        return { ok: true, installed: true, id: selected.id };
      } finally { changing = false; }
    },
    async configureLaunch(body) {
      await assertIdle();
      if(body.residentGraphics!==undefined&&typeof body.residentGraphics!=='boolean')throw Error('Resident graphics setting must be boolean');
      if(body.residentFiniteAudio!==undefined&&typeof body.residentFiniteAudio!=='boolean')throw Error('Resident finite audio setting must be boolean');
      if(body.sdLauncher!==undefined&&typeof body.sdLauncher!=='boolean')throw Error('SD launcher setting must be boolean');
      if(body.nativeRecovery!==undefined&&typeof body.nativeRecovery!=='boolean')throw Error('Native recovery setting must be boolean');
      const previous = JSON.parse(JSON.stringify(choice));
      const previousConfiguration = JSON.parse(JSON.stringify(configState));
      changing = true;
      try {
        const next = {
          ...choice,
          loaderId: body.loaderId ?? choice.loaderId,
          variant: body.variant ?? choice.variant,
          sdLauncher: body.sdLauncher ?? choice.sdLauncher,
          nativeRecovery: body.nativeRecovery ?? choice.nativeRecovery,
          residentGraphics: body.residentGraphics ?? choice.residentGraphics,
          residentFiniteAudio: body.residentFiniteAudio ?? choice.residentFiniteAudio,
        };
        if (
          ![
            'original',
            ...require('../core/descriptors.cjs')
              .scan(root, 'shell')
              .items.map((s) => s.id),
          ].includes(next.variant)
        )
          throw Error('Choose IZ2S, IZPSlauncher or Core only');
        if (next.loaderId === 'iz2s-core' && next.variant !== 'izlauncher') throw Error('Choose IZ2S.');
        loaders.select(
          next.loaderId,
          body.loaderSettings || choice.loaderSettings?.[next.loaderId] || {},
        );
        const selectingEngine = body.loaderId !== undefined || body.variant !== undefined || body.loaderSettings !== undefined;
        // Input/network preferences are independent of host engine qualification.
        // An unchanged engine must not prevent saving an input device on Windows.
        if (selectingEngine && !available(next.loaderId, next.variant)) {
          if (body.variant !== undefined && body.variant !== 'original' && body.loaderId === undefined)
            next.loaderId = 'iz2s-core';
          else
            throw Error(
              'Selected loading method is unavailable for IZ2S',
            );
        }
        if (body.installSlot !== undefined) {
          require('../core/session/install-slot.cjs').validate(
            body.installSlot,
          );
          next.installationSlots = {
            ...choice.installationSlots,
            [next.loaderId]: body.installSlot,
          };
        }
        choice = next;
        if (body.loaderSettings !== undefined) {
          loaders.select(choice.loaderId, body.loaderSettings);
          choice.loaderSettings = {
            ...choice.loaderSettings,
            [choice.loaderId]: body.loaderSettings,
          };
        }
        if (body.configurationId !== undefined || body.profile !== undefined) {
          const id = body.configurationId || configState.selected;
          await configurations.resolve(id, {
            root,
            settings: defaults,
            profile: body.profile || configState.profiles?.[id] || {},
          });
          await configurations.save(root, {
            selected: id,
            profiles: {
              ...configState.profiles,
              ...(body.profile ? { [id]: body.profile } : {}),
            },
          });
        }
        await initialize();
        if(body.profile&&Object.hasOwn(body.profile,'armAddress'))
          await connections.update({id:'arm',mode:connections.mode('arm'),ip:body.profile.armAddress||'',automatic:!body.profile.armAddress});
        await saveJson(file, choice);
        return { ok: true };
      } catch (error) {
        choice = previous;
        try {
          if (body.configurationId !== undefined || body.profile !== undefined)
            await configurations.save(root, previousConfiguration);
          await initialize();
        } catch {error.message += ' Settings could not be restored; reopen the panel before launching.';}
        throw error;
      } finally {
        changing = false;
      }
    },
    async openHardwareApp() {
      await assertIdle();
      if(preview)throw Error('Preview cannot open applications');
      if(!configuration.externalWorkflow)throw Error('This configuration has no native companion');
      const entry=require(path.join(configuration.folder,configuration.entry));
      if(typeof entry.open!=='function')throw Error('The hardware module does not provide a launcher');
      return entry.open({root,configuration});
    },
    async installInput(body) {
      if(preview)throw Error('Preview cannot install an input package');
      await assertIdle();
      if(configuration.inputAdapter!=='ssh-gadget')throw Error('This configuration has no SSH input installer');
      if(!body.confirm)throw Error('Confirm the ARM installation target first');
      changing=true;
      try {
        const profile={...configState.profiles?.[configuration.id],armAddress:body.address,armUser:body.user,sshIdentity:body.identity||''};
        const target=configurations.resolve(configuration.id,{root,settings:defaults,profile});
        if(!require('node:net').isIPv4(target.settings.armAddress))throw Error('Enter the ARM IPv4 address');
        if(!target.settings.armUser)throw Error('Enter the SSH user');
        await configurations.save(root,{selected:configuration.id,profiles:{...configState.profiles,[configuration.id]:profile}});
        await initialize();
        await connections.update({id:'arm',mode:connections.mode('arm'),ip:body.address,automatic:false});
        const result=await input.install();
        await engine().refresh(true);
        return {ok:true,...result};
      } finally { changing=false; }
    },
    configureConnection: (...args) => engine().configureConnection(...args),
    setupAction: (body) => engine().setupAction(body),
    async iz2scivAction(body) {
      if (changing) throw Error('Wait for the current operation to finish');
      if (choice.loaderId !== 'iz2sciv-core') throw Error('Select IZ2SCiv core first.');
      return upstream.action(body);
    },
    async civswitchAction(body) {
      if (choice.loaderId !== 'civswitch' || isIZ()) throw Error('Select CivSwitch in Core only first');
      return upstream.action(body);
    },
    async civswitchLdnAction(body) {
      if (choice.loaderId !== 'civswitch-ldn' || isIZ()) throw Error('Select CIV7 LAN in Core only first');
      return upstream.action(body);
    },
    retry: () => engine().retry(),
    setupReport: (...args) => engine().setupReport(...args),
    remove: async (id) => {
      const ledger = createLedger(root),
        row = await ledger.read(id);
      if (row?.upstream) return upstream.remove(id);
      if (iz) return iz.remove(id);
      return ledger.removeFailure(id);
    },
    historyLog: (...args) => engine().historyLog(...args),
    dismiss: () => engine().dismiss(),
    async restoreConnection(){
      if(preview)throw Error('Preview cannot reconnect a console');const state=await engine().snapshot(),directory=state.active?.runDirectory;
      if(!directory)throw Error('Start a launcher session with automatic connection recovery enabled');
      const rpc=require(path.join(directory,'shared/rpc.cjs')),status=await rpc.request({command:'recovery-status'});
      if(!status.enabled)throw Error('This session has no resident recovery service; enable automatic recovery before starting a fresh session');
      return rpc.request({command:'recovery-wake'});
    },
    loadPackages: async (ids,options) => {
      if(['civswitch','civswitch-ldn'].includes(choice.loaderId))return upstream.loadPackages(ids,options);
      if (!isIZ()) throw Error('Select a console shell to load IZA modules');
      if(choice.sdLauncher||options?.target==='sd'||Object.values(options?.targets||{}).includes('sd')){
        if(preview)throw Error('Preview cannot write to SD');
      }
      throw Error('Use the IZ2S core app library to install applications.');
    },
    unloadPackages: async (ids,target) => {
      if(['civswitch','civswitch-ldn'].includes(choice.loaderId))return upstream.unloadPackages(ids,target);
      if(preview)throw Error('Preview cannot remove console modules');
      if(!isIZ())throw Error('Select a console shell to remove modules');
      if(choice.residentGraphics&&(await engine().snapshot()).active)throw Error('Restart the resident session to change its prepared app set');
      throw Error('Use the IZ2S core app library to remove applications.');
    },
    refreshSdInventory:()=>{if(!isIZ())throw Error('Select a console shell first');throw Error('Use the IZ2S core SD inventory.');},
    async source(id) {
      await reconcileInstallation();
      if (loaders.select(id).manifest.installed === false)
        throw Error('Install the original engine first');
      if (id === 'iz2s-core') return {
        file: path.join(root, 'engines/iz2s-core/runtime/bootstrap.bas'), name: 'NS2IZA',
      };
      const loader = loaders.select(id, choice.loaderSettings?.[id] || {}),
        plan = loader.sourcePlan();
      if (!plan.bootstrap)
        throw Error('This engine does not require a bootstrap');
      return {
        file: plan.bootstrap.file,
        name: plan.bootstrap.saveName || 'NS2',
      };
    },
  };
}
module.exports = { create };
