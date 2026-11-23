'use strict';
const localRuntime = require('./adapters/local-runtime.cjs');
const izps2 = require('./adapters/izps2.cjs');
const izpsx = require('./adapters/izpsx.cjs');
const pcMedia = require('./adapters/pc-media.cjs');
const legacyAdapters = [izps2, izpsx, pcMedia];
const packages = require('../../core/console/sd-launcher/package.cjs');
const {sha} = require('../../core/archive.cjs');

// Package compatibility, not a claim that a game was tested on hardware.
// A direct-address legacy ABI never acquires a fake SDK or RO handle here.
function legacyAbi(abi) {
  return abi===5 || abi===6;
}
function legacyReason() {
  return 'Legacy ABI 5–6 requires the original SmileBASIC SDK and process. This build is unavailable in IZ2S.';
}
function packed(header) {
  if (header.delegated) return {compatible:false,reason:'This SD package requires a PC executor.'};
  if (legacyAbi(header.nativeAbi)) return {compatible:false,reason:legacyReason()};
  return {compatible:true,kind:header.nativeAbi ? 'native-abi-'+header.nativeAbi : 'gpu'};
}
function directImage(descriptor, files) {
  const entry = descriptor.switch?.sdImage;
  if (!entry) return null;
  const bytes = files?.find(file => file.name === entry)?.bytes;
  if (!bytes) throw Error('Standalone SD executable is missing.');
  const header = packages.decode(bytes);
  const policy = packed(header);
  if (!policy.compatible) throw Error(policy.reason);
  if (header.id !== descriptor.id) throw Error('Standalone SD executable ID differs from the module ID.');
  const assets = require('../../core/console/sd-launcher/assets.cjs');
  const prefix = require('node:path').posix.dirname(entry) + '/';
  const metadata = files.find(file => file.name === prefix + 'sd-bundle.json');
  let companions = [];
  if (metadata) {
    const bundle = JSON.parse(metadata.bytes);
    if (bundle.format !== 'iz2s.sd-bundle' || bundle.version !== 1 || bundle.id !== header.id ||
        bundle.launcher?.bytes !== bytes.length || bundle.launcher.sha256 !== sha(bytes) || !Array.isArray(bundle.assets))
      throw Error('Standalone SD bundle identity or launcher checksum differs.');
    companions = assets.declarations({native:{assets:bundle.assets}});
    if (!descriptor.switch.files.includes(metadata.name) || companions.some(row =>
      row.file !== row.path || !descriptor.switch.files.includes(prefix + row.file)))
      throw Error('Declare the standalone SD metadata and companions in the Switch file list.');
    assets.inspectPacked({native:{assets:companions}}, files.filter(file => file.name.startsWith(prefix))
      .map(file => ({name:file.name.slice(prefix.length),bytes:file.bytes})));
  }
  assets.verifyBinding(header, companions);
  return {bytes, header, companions: companions.map(row => ({...row,
    contents: files.find(file => file.name === prefix + row.file).bytes}))};
}
function eligibility(manifest,descriptor={},files=[],{ checkedSd, sha256, archiveFiles }={}) {
  if (descriptor.switch?.sdImage) {
    try {
      const {header, bytes} = directImage(descriptor, archiveFiles);
      return {...packed(header), prebuilt:true, imageSha256:sha(bytes)};
    } catch (error) {
      return {compatible:false,reason:error.message};
    }
  }
  if(manifest.format==='izlauncher.nro-app')
    return {compatible:false,reason:'Install this application from a mounted SD card.'};
  const adapter = legacyAdapters.find(value => value.matchesArchive(manifest,descriptor,sha256,archiveFiles));
  if (adapter) return {compatible:true,adapter:adapter.adapter};
  if (legacyAbi(manifest.native?.abi)) return {compatible:false,reason:legacyReason()};
  try {
    const sd=checkedSd||require('../../core/console/sd-launcher/adapters.cjs').eligibility(manifest,files);
    if (sd.compatible && ['launcher-instructions-v1','launcher-reader-v1'].includes(sd.adapter) &&
        localRuntime.resolvesDependencies(descriptor,sha256,archiveFiles)) return sd;
    if (descriptor.dependencies?.length || manifest.hostRuntime)
      return {compatible:false,reason:'This app requires a PC service or another module.'};
    return packages.eligibility(manifest,{sb4:true});
  } catch(error) { return {compatible:false,reason:error.message}; }
}
async function buildApp(source,output,policy,{build=packages.build,entry,includeLibrary}={}) {
  const adapter = legacyAdapters.find(value => value.adapter===policy.adapter);
  if (adapter) await adapter.prepareSource(source);
  return build(source,output,{sb4:true,entry,includeLibrary});
}
async function convertArchive(checked, app) {
  const adapter = legacyAdapters.find(value =>
    value.matchesArchive(app.manifest,checked.descriptor,checked.sha256,checked.files));
  return adapter ? adapter.convertArchive(checked) : localRuntime.convertArchive(checked,app);
}
async function convertImage(bytes, header, scratch) {
  for (const entry of legacyAdapters) {
    const result = await entry.convertImage?.(bytes, header, scratch);
    if (result) return Buffer.isBuffer(result) ? {adapter:entry.adapter,bytes:result,sources:[]} : result;
  }
  return null;
}
module.exports={packed,directImage,eligibility,buildApp,convertArchive,convertImage};
