const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
test('install documentation links both current packages and the canonical SD path',()=>{
 const {version}=require('../package.json');
 const readme=fs.readFileSync(path.join(root,'README.md'),'utf8');
 for(const ext of ['iza','zip'])assert(readme.includes(`/releases/download/v${version}/izpsx-emulator-${version}.${ext}`));
 assert(readme.includes('SD:/iz2s/modules/izpsx/bios/bios.bin'));
 assert(readme.includes('SD:/iz2s/modules/izpsx/games/'));
});
test('frozen native companion identity remains bound to its name',()=>{const crypto=require('node:crypto'),m=require('../module/switch/app.json');for(const asset of m.native.assets){const b=fs.readFileSync(path.join(root,'module/switch',asset.file));assert.equal(b.length,asset.bytes);assert.equal(crypto.createHash('sha256').update(b).digest('hex'),asset.sha256);}});
