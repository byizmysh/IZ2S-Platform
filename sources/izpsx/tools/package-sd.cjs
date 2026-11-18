'use strict';
// Publication uses the reviewed native export. Rebuild it explicitly before replacing its receipt.
require('./package.cjs').build({fromBuild:process.argv.includes('--from-build')}).then(r=>console.log(JSON.stringify(r))).catch(e=>{console.error(e.stack);process.exitCode=1});
