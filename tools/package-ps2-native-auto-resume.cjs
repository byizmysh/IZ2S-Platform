'use strict';
const fs = require('node:fs/promises'), path = require('node:path'), assert = require('node:assert/strict'), crypto = require('node:crypto');
const ROOT = path.resolve(__dirname, '..'), W = path.join(ROOT, 'data/ps2-cold-work');
const J = async p => JSON.parse(await fs.readFile(p));
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const put = (p,v) => fs.writeFile(p, JSON.stringify(v,null,2)+'\n', {flag:'wx'});
async function listing(dir, base=dir) {
  const rows=[];
  for(const e of await fs.readdir(dir,{withFileTypes:true})) {
    assert(!e.isSymbolicLink());const p=path.join(dir,e.name);
    if(e.isDirectory())rows.push(...await listing(p,base));else if(e.isFile())rows.push({name:path.relative(base,p),path:p});
  }
  return rows;
}
async function main() {
  const pointer=await J(process.argv[2]), bundle=pointer.bundle, native=await J(pointer.report);
  assert(native.passed && native.originalInputObjectsUnchanged && native.libcInitializesTLSBeforeWrapper && !native.foreignSDKCallsToMuslFunctions);
  assert(native.children.length===3 && native.children.every(c=>c.reaped&&c.returnCode===0&&c.groupEmpty&&!c.timedOut));
  const S=path.join(W,'build/r324-os-JIT-package-source-v1'), O=path.join(path.dirname(pointer.report),'package'), M=path.join(O,'module');
  await fs.mkdir(O); const result={format:'ps2-native-auto-resume-package/1',passed:false,consoleCommands:0,installed:false,hardwareEntryAllowed:false,performanceGainPercent:null};
  try {
    await fs.cp(path.join(S,'module'),M,{recursive:true});
    const id='ps2-cold-r324-auto', candidateFile=path.join(W,'build/native-auto-resume-run22-session-package-candidate-v1.json');
    for(const f of await listing(M)) {
      if(['checksums.json','server/runtime.cjs'].includes(f.name)||!/\.(json|cjs|md)$/.test(f.name))continue;
      const t=await fs.readFile(f.path,'utf8');const u=t.replaceAll('ps2-cold-r324',id).replaceAll('r324-run22-','native-auto-resume-run22-');
      if(u!==t)await fs.writeFile(f.path,u);
    }
    const manifestFile=path.join(bundle,'manifest.json'), profileFile=path.join(bundle,'profile/report.json');
    const manifestHash=sha(await fs.readFile(manifestFile)), profileHash=sha(await fs.readFile(profileFile)), toolHash=sha(await fs.readFile(path.join(bundle,'relink.py')));
    assert.equal(manifestHash,pointer.manifestSha256);assert.equal(profileHash,pointer.profileSha256);assert.equal(toolHash,pointer.toolSha256);
    const parentAdmission=await fs.readFile(path.join(S,'module/server/native-profile-admission-r64.cjs'),'utf8');
    let admission=parentAdmission.replace('async function loadSource(', 'async function loadParentSource(');
    const verifyAnchor='function verifyBuilt({built,nativeProfile,codeBase,dataBase}){';
    assert.equal(admission.split(verifyAnchor).length,2);
    admission=admission.replace(verifyAnchor,verifyAnchor+'\n const MANIFEST_SHA='+JSON.stringify(manifestHash)+', TOOL_SHA='+JSON.stringify(toolHash)+';');
    admission+='\n'+`async function loadSource(options){
 const parent=await loadParentSource(options);
 const bundle=${JSON.stringify(bundle)},manifestFile=${JSON.stringify(manifestFile)},profileFile=${JSON.stringify(profileFile)};
 async function exact(file,hash){const b=await (options.readFile||fs.readFile)(file);assert.equal(sha(b),hash,file);return b;}
 const manifest=JSON.parse(await exact(manifestFile,${JSON.stringify(manifestHash)})),profile=JSON.parse(await exact(profileFile,${JSON.stringify(profileHash)}));
 await exact(path.join(bundle,'relink.py'),${JSON.stringify(toolHash)});
 const q=JSON.parse(await exact(${JSON.stringify(pointer.report)},${JSON.stringify(sha(await fs.readFile(pointer.report)))}));
 assert(q.passed&&q.originalInputObjectsUnchanged&&q.libcInitializesTLSBeforeWrapper&&!q.foreignSDKCallsToMuslFunctions&&!q.consoleContacted&&q.SDKCalls===0);
 assert(q.children.length===3&&q.children.every(c=>c.reaped&&c.returnCode===0&&c.groupEmpty&&!c.timedOut));
 await exact(q.newObject.path,q.newObject.sha256);await exact(q.source.path,q.source.sha256);
 assert.deepEqual(manifest.executionInputs,{...parent.manifest.executionInputs,[q.newObject.path]:q.newObject.sha256});
 assert.deepEqual(manifest.metadataInputs,{...parent.manifest.metadataInputs,[q.source.path]:q.source.sha256,[${JSON.stringify(pointer.report)}]:${JSON.stringify(sha(await fs.readFile(pointer.report)))}});
 const inherited=structuredClone(parent.manifest);
 for(const key of ['imageBounds','expectedExports','linkMapLoads','linkCommand','nativeActivity','nativeFusedOpaqueCopy','sourceNativeProfile','relinkToolSha256','executionInputs','executionInputCount','metadataInputs'])inherited[key]=manifest[key];
 assert.deepEqual(manifest,inherited);assert.equal(manifest.executionInputCount,parent.manifest.executionInputCount+1);
 assert.equal(manifest.linkCommand.filter(v=>v==='-Wl,--wrap=main').length,1);
 const expected=structuredClone(parent.profile);Object.assign(expected,{image:q.image,exports:q.exports,nativeActivity:manifest.nativeActivity,nativeFusedOpaqueCopy:manifest.nativeFusedOpaqueCopy});assert.deepEqual(profile,expected);
 imageExports(profile.image,profile.exports,manifest,Number(profile.image.codeBase),Number(profile.image.dataBase));
 return {profile,manifest,bundle,manifestFile,evidence:{...parent.evidence,manifestFile,profileFile,manifestSha256:${JSON.stringify(manifestHash)},profileSha256:${JSON.stringify(profileHash)},executionInputs:manifest.executionInputCount,nativeActivity:profile.nativeActivity,nativeFusedOpaqueCopy:profile.nativeFusedOpaqueCopy,nativeAutoResume:true,autoResumeQualification:${JSON.stringify(pointer.report)},foreignSDKCallsToMuslFunctions:false}};
}
module.exports={loadSource,verifyBuilt,CORE_SHA256:module.exports.CORE_SHA256};
`;
    const admissionFile=path.join(M,'server/native-profile-admission-r64.cjs');await fs.writeFile(admissionFile,admission);
    const H=require(admissionFile), ready=await H.loadSource({base:W});
    assert.equal(ready.evidence.nativeAutoResume,true);
    const before=await fs.readFile(path.join(S,'module/server/runtime-owned.cjs'),'utf8');
    const after=await fs.readFile(path.join(M,'server/runtime-owned.cjs'),'utf8');
    assert.equal(after.replaceAll(id,'ps2-cold-r324').replaceAll('native-auto-resume-run22-','r324-run22-'),before,'No SDK callback hooks may enter the production runtime');
    for(const name of ['code.bin','data.bin'])await fs.copyFile(path.join(path.dirname(pointer.report),'app/build',name),path.join(M,'server/native',name));
    const canonical={format:'ps2-native-launcher-auto-resume-canonical/1',passed:true,image:native.image,exports:native.exports,nativeExecuted:false,hardwareEntryAllowed:false};
    await fs.writeFile(path.join(M,'server/native/report.json'),JSON.stringify(canonical,null,2)+'\n');
    const transferLog=JSON.parse(await fs.readFile(path.join(ROOT,'data/build-cache/ps2-startup-transport-local-first.log'),'utf8'));
    const transfer=await J(transferLog.report);assert(transfer.passed&&!transfer.nativeHelperBytesChanged);
    const body=await fs.readFile(transfer.evidence.candidate);assert.equal(sha(body),transfer.evidence.candidateSha256);
    assert.equal(sha(await fs.readFile(path.join(M,'server/loader-native-batch-v1.cjs'))),transfer.evidence.baselineSha256);
    await fs.writeFile(path.join(M,'server/loader-native-batch-v1.cjs'),body);
    const spec=await J(path.join(S,'execution-pins.json'));spec.externalPins[manifestFile]=manifestHash;spec.externalPins[profileFile]=profileHash;
    spec.externalPins[path.join(bundle,'relink.py')]=toolHash;spec.externalPins[pointer.report]=sha(await fs.readFile(pointer.report));
    spec.externalPins[native.newObject.path]=native.newObject.sha256;spec.externalPins[native.source.path]=native.source.sha256;
    spec.policy+=' Native auto resume: original libc entry invokes a BTI launcher main wrapper, sets PS2_AUTO_RESUME=1 and calls unchanged original main. Every prior engine input object, allocator, cache, worker and cleanup remains unchanged. No SDK thread calls a musl session export. Combined loader transfer retains native batch bytes and all original page/PID checks. Physical activation pending.';
    spec.modulePins={};
    for(const f of await listing(M))if(!['checksums.json','server/runtime.cjs'].includes(f.name))spec.modulePins[f.name]=sha(await fs.readFile(f.path));
    await put(path.join(O,'execution-pins.json'),spec);
    let wrapper=await fs.readFile(path.join(S,'module/server/runtime.cjs'),'utf8');const a=wrapper.indexOf('const spec=')+11,b=wrapper.indexOf(';\nexports.create',a);assert(a>11&&b>a);
    wrapper=wrapper.slice(0,a)+JSON.stringify(spec)+wrapper.slice(b);await fs.writeFile(path.join(M,'server/runtime.cjs'),wrapper);
    // The runtime's full inherited pin verifier remains in place. Packaging
    // validates the actual current native admission rather than granting entry.
    const checks={};for(const f of await listing(M))if(f.name!=='checksums.json')checks[f.name]=sha(await fs.readFile(f.path));
    await fs.writeFile(path.join(M,'checksums.json'),JSON.stringify({algorithm:'sha256',files:checks},null,2)+'\n');
    const archive=require(path.join(ROOT,'core/archive.cjs')),bytes=await archive.pack(M),unpacked=archive.inspect(bytes);assert.equal(unpacked.descriptor.id,id);
    for(const f of unpacked.files)if(f.name!=='checksums.json')assert.equal(sha(f.bytes),checks[f.name]);
    const packageFile=path.join(O,id+'.izm');await fs.writeFile(packageFile,bytes,{flag:'wx'});
    const app=await require(path.join(ROOT,'core/services/app-catalog.cjs')).readApp(path.join(M,'switch'),{validate:false});
    const parent=await J(path.join(W,'build/r324-run22-session-package-candidate-v1.json'));
    const candidate={...parent,id,module:M,package:packageFile,bytes:bytes.length,sha256:sha(bytes),appFingerprint:app.fingerprint,
      manifestFile,nativeProfileFile:profileFile,executionPinsFile:path.join(O,'execution-pins.json'),
      nativeBuild:{report:pointer.report,sha256:sha(await fs.readFile(pointer.report))},readyMetadata:{report:pointer.report,sha256:sha(await fs.readFile(pointer.report))},
      localPackageReport:path.join(O,'report.json'),hostCompatibilityRevision:'R324-native-auto',nativeRevision:'R286 plus launcher entry opt-in',
      inheritsR262ExceptPublicationProvider:false,originalR286InputObjectsUnchanged:true,nativeAutoResume:true,
      installed:false,hardwareEntryAllowed:false,fullRuntimeRelinkQualified:false,performanceGainPercent:null,
      change:'Enable original native SD auto resume through libc-initialized launcher main, preserve R324 OS JIT allocator and all original PS2 objects',
      status:'Local candidate; actual relocated relink and hardware activation pending',
      limitations:['Initial GTA gameplay snapshot has not yet been captured. Package is not installed on SD. No measured latency gain. Host executor still required.']};
    await put(candidateFile,candidate);
    Object.assign(result,{passed:true,candidate:candidateFile,package:packageFile,sha256:candidate.sha256,bytes:bytes.length,autoRestore:true,
      nativeAdmissionPassed:true,runtimeSourceInverseExact:true,nativeOriginalInputObjectsUnchanged:true,
      currentOSJITAllocatorPreserved:true,foreignSDKCallsToMuslFunctions:false,ready:pointer.bundle,qualification:pointer.report});
    console.log(JSON.stringify({passed:true,candidate:candidateFile,package:packageFile,ready:bundle,installed:false}));
  }catch(e){result.error=e.stack;throw e;}finally{await put(path.join(O,'report.json'),result);}
}
main().catch(e=>{console.error(e.stack);process.exitCode=1;});
