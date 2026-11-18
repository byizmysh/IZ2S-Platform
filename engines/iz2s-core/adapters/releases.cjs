'use strict';
const archive = require('../../../core/archive.cjs');
const sonicResident = require('../../../core/console/sd-launcher/instructions/izsonic-resident-releases.json');

// Each pair is the archive SHA-256 and the SHA-256 of its checksums.json.
// The alternate match is used only after archive.inspect/inspectFiles has
// checked the entire inventory against that exact checksum manifest.
const pins = new Map(Object.entries({
  'ims-music-bulk': [
    'aaf1c270e37ca1d89ab75987f4f906d8af3f11f6eed4b400ceacd079dbab3a50',
    'f2ae10f3e775db4e8d75c103e67f580616960d4bca422b7183dd5c537771cff3',
  ],
  'iz-controller-lab': [
    'fcfdcec069b9e4f0ff07247bd6596c5d2005997fca9d24efa3cbf5e1c47053c3',
    '91f76cdbdbcd1e541f946861ac6d26276e79cbcb72ac785b1d121f6b8d132628',
  ],
  izpsx: [
    '37a6fe309dfd2d97e0a54980a43d3df727fb22b66fdbc1107ee40e634ca5a4b1',
    'a60bbbf5fc245ff8d406dae2879e281ff3c5670ee217546990041658be790dd1',
  ],
  'izpsx@0.5.2': [
    '564e5ab53409dac85a12f276e688d6b8759cca55e94d5e5686f7ee2fb7a570f1',
    '130b81cfda225342e9256eebe57d53af70946a3f8fb0d23148f52f6fa4957bad',
  ],
  izps2: [
    '8e770afbf36721d8ace9967b235de615df8c9c04cc6b421e8802e84c3e79d74d',
    '9167ec8564efd971c09ae6cc6e48de78aed22e512a6376a602e4a801a52d3c06',
  ],
  'izps2@0.1.799': [
    'b2d727b1c3778865bc43c25cc05f3fbfb7c3c95218491b9606550ddcee778f7b',
    '2e6a26d8f5b966bcf92b612574b116d76483ac2252252ed5a8e933c95ba69e43',
  ],
  'izpsx-sd-test': [
    '175b7d3aa0f2313c31da5a66b46252540af1847cab084f27d4a14d4f58fc7d9d',
    'f5fdf67db47052deacc43b0807e0921c478fddad8b1acf2cfe4a0d6b8454c476',
  ],
  'izdoom-prime18': [
    '566ad4b4fd859d830dc4a06deaf36bcdd61907e26183abb845745f6efa62afd2',
    '3717fd3326a5fff72568dc84aff2304015056abd0d1830cdfaecaa052cacf3da',
  ],
  'izdoom-native18': [
    'db58526a450b14892713a48fa05564edb1e356235ac3e0fc69e1a03ce80f0d6d',
    'eac37388d172515bc96169dacf7fa42612c9852ec13014be5f0565be4002bb0e',
  ],
  'izdoom-wide18': [
    '9c964c8edf98081f65442c67c67491cccac5e32b9e8d567faf8d407ebfcdc0b2',
    '576fce1a6f0aec291c046308acc1ec97bcad8f157e35f599e1016550d20e0d69',
  ],
  'izdoom-audio-test': [
    'bce1cb22a1ed15985a9c5fa8385ecafb376f4ab62b67a75618def21d7bb934ec',
    '3ab55ae533bc62dab798144a41ce9ae4a4ce8f7978b9c829f916ed422152be1c',
  ],
  'izsonic-transfer18': [
    'f493eddbc0843c09c229fa39ccef98565f9f9198f0a2063d0be22dfca7467833',
    '30d0c1ef120cb596f7f980855f726d89ecdd9f0bf077ed53060e43e19880ff3c',
  ],
  'iznes-3a1cc81292c8': [
    'b40f6c94efae6f7c4fd25b1f64e797eeea7e532a8811dd056cc16dcc1ff19a59',
    'ac029330ed5417a8c80b08e2d03ebaf520e8d05e7db96090300d8ff97a14412a',
  ],
  'iznes-4bf6c8c01b84': [
    '519e252b3e71177fa5f5da07b873a28709a4b314bce302885c9ad858634c9b8c',
    '46c4c2c0007c549d12010100747069e613ebf97924db0301c1f3c7047cb1e105',
  ],
  'iznes-8d5e3d9ce20f': [
    '96892db2b67c1399960466c28d84ba9d4744d5abb9f7e4464b76cdbe8d544ead',
    'a5dc8beec66648b69994322e615b2447eed2c80b900dc69521831571bef6b919',
  ],
  'iznes-ba5337367a21': [
    '769adcd86136fbbea8032cd6d146e7a15beeb3e3251257a3344448fd372afe5d',
    '3f7c6eb38eeb3867a057aea782fe6661b7a24f16c9353bb4d50fb1036c984b24',
  ],
  'iznes-f45ca41c4636': [
    'b3cd2de4bc7f1513d0044f95c7ecfdb68aa774184b779da38edc94670dd50cb9',
    '5c40fe005425e09da9aedb2a80e67899c671aed0b827c94ffacaa72d46ff1a65',
  ],
}));

function checksumManifestSha256(files) {
  const manifest = files?.find(file => file.name === 'checksums.json');
  return manifest && archive.sha(manifest.bytes);
}

function releaseDigests(id) {
  const pin = pins.get(id);
  if (pin) return {archiveSha256:pin[0], checksumsSha256:pin[1]};
  if (Object.hasOwn(sonicResident,id)) {
    const {archiveSha256,checksumsSha256}=sonicResident[id];
    return {archiveSha256,checksumsSha256};
  }
}

function matchesRelease(id, sha256, validatedFiles) {
  const local = require('./bundled-releases.json')[id];
  if(local && (local.archiveSha256===sha256 || local.checksumsSha256===checksumManifestSha256(validatedFiles)))return true;
  const release = releaseDigests(id);
  return release && (release.archiveSha256 === sha256 ||
    release.checksumsSha256 === checksumManifestSha256(validatedFiles));
}

module.exports = {matchesRelease, releaseDigests, checksumManifestSha256};
