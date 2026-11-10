'use strict';
const fs = require('node:fs/promises'), path = require('node:path');
const {create} = require('./client.cjs');
const {inspectModule} = require('../../../core/services/inspect.cjs');
// Reuse the launcher's MOD0/dynamic-symbol reader, without starting SB4's
// filesystem acquisition, graphics worker or SDK qualification.
async function inspect({host, directory, client = create({host})}) {
  await fs.mkdir(directory, {recursive:true, mode:0o700});
  const info = await client.connect();
  const report = {engine:'civswitch', filesystem:false, started:new Date().toISOString(), info, modules:{}};
  try {
    for (const module of info.modules) {
      const result = await inspectModule(client, Number(module.base), Math.min(0x10000000,module.size));
      report.modules[module.name] = result;
      await fs.writeFile(path.join(directory,module.name+'-symbols.json'),JSON.stringify(result,null,2),{mode:0o600});
    }
  } finally {if (!client.uncertain) await client.quiet(false);}
  const symbols = Object.values(report.modules).flatMap(m=>m.symbols);
  report.graphics = symbols.filter(s=>s.section && /^(?:vk|nvn)|nn2vi|nn3hid/.test(s.name));
  report.completed = new Date().toISOString();
  await fs.writeFile(path.join(directory,'inspection.json'),JSON.stringify(report,null,2),{mode:0o600});
  return report;
}
if (require.main === module) inspect({host:process.argv[2],directory:path.resolve(process.argv[3]||'data/diagnostics/civswitch-runtime')})
  .then(report=>console.log(JSON.stringify({modules:Object.fromEntries(Object.entries(report.modules).map(([name,m])=>[name,m.symbols.length])),graphicsExports:report.graphics.length,filesystem:false})))
  .catch(error=>{console.error(error.stack);process.exitCode=1;});
module.exports = {inspect};
