// Compatibility filename: no kaiHEN release or staging path is supported.
import {main} from "./launch.mjs";
const args = process.argv.slice(2);
if (args[0] && !args[0].startsWith("--")) args.unshift("--host");
main(["launch", ...args]).catch(error => { console.error(error.message); process.exitCode = 1; });
