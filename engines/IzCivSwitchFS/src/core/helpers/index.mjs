import {
  buffersEqual,
  checkAddressHeaderEncoding,
  cleanup,
  hasByteDifferent,
  hasByteDifferentFast,
  ipcGetEntryTypeRawBytes,
  pad2,
  padRight,
  formatWaveSummaryLine,
  readFsAddressBytes,
  readHexBytes,
  bytesToHex,
  scanPointerWords
} from "./util.mjs";

import {
  inspectFspIdentityTargetsReadOnly as _inspectFspIdentityTargetsReadOnly,
  scanNativeLinkBuffer,
  scanRootDomainChunk,
  scanRootDomainChunkPrefiltered,
  captureDomainTail,
  recordRootDomainCandidate,
  inspectRootBackend
} from "./scan.mjs";

import {
  resolvePoolCandidates,
  discoverMetadataAnchors,
  qualifyMetadataPage,
  verifyAddressCandidate
} from "./discovery.mjs";

import {
  inspectCandidateCacheAlias,
  installAddressHeader,
  qualifyCacheBucketPeriod,
  retireCandidateCache
} from "./cache.mjs";

import {
  buildPlans,
  configureProfile,
  configurePayload,
  writeIntoPayload,
  checkPathPairQuiet,
  captureRenameInputState,
  queryCanaryHealth,
  nulWrites,
  analyzeWave
} from "./wave.mjs";

export function createFspIdentityHelpers(options = {}) {
  const optimizeRootCandidateScan =
      options.optimizeRootCandidateScan === true;
  const collectRootScanTiming = options.collectRootScanTiming === true;

  return {
    inspectFspIdentityTargetsReadOnly: (input) =>
        _inspectFspIdentityTargetsReadOnly({
            ...input, optimizeRootCandidateScan, collectRootScanTiming
        }),
    scanNativeLinkBuffer,
    resolvePoolCandidates,
    discoverMetadataAnchors,
    qualifyMetadataPage,
    verifyAddressCandidate,
    scanRootDomainChunk,
    scanRootDomainChunkPrefiltered,
    captureDomainTail,
    recordRootDomainCandidate,
    readFsAddressBytes,
    inspectRootBackend,
    pad2,
    padRight,
    formatWaveSummaryLine,
    readHexBytes,
    bytesToHex,
    ipcGetEntryTypeRawBytes,
    checkAddressHeaderEncoding,
    buffersEqual,
    hasByteDifferent,
    hasByteDifferentFast,
    scanPointerWords,
    cleanup,
    inspectCandidateCacheAlias,
    installAddressHeader,
    qualifyCacheBucketPeriod,
    retireCandidateCache,
    buildPlans,
    configureProfile,
    configurePayload,
    writeIntoPayload,
    checkPathPairQuiet,
    captureRenameInputState,
    queryCanaryHealth,
    nulWrites,
    analyzeWave
  };
}
