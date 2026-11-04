const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const descriptor = require("../engine.json");

// Return a recipe only; never type or run console commands here.
function plan({
  slot = descriptor.bootstrap.installSlot,
  saveName = descriptor.bootstrap.saveName,
} = {}) {
  if (!Number.isInteger(slot) || slot < 0 || slot > 3)
    throw Error("Choose an installation slot from 0 to 3");
  if (!/^[A-Z0-9_]{1,16}$/.test(saveName))
    throw Error("Invalid bootstrap save name");
  const file = path.resolve(__dirname, "..", descriptor.bootstrap.file);
  const bytes = fs.readFileSync(file);
  const sha256 = crypto.createHash("sha256").update(bytes).digest("hex");
  if (sha256 !== descriptor.bootstrap.sha256)
    throw Error("Bootstrap checksum differs");
  return {
    file,
    sha256,
    sourceSlot: slot,
    saveName,
    arity: descriptor.bootstrap.arity,
    list: `LIST ${slot}:1`,
    save: `SAVE "TXT:${saveName}",${slot}`,
    load: `LOAD "TXT:${saveName}"`,
    lines: bytes.toString("ascii").trimEnd().split("\n").length,
  };
}
if (require.main === module) {
  try {
    const args = process.argv.slice(2);
    if (
      args.length &&
      !(args.length === 2 && args[0] === "--slot" && /^[0-3]$/.test(args[1]))
    )
      throw Error("Usage: node server/install.cjs [--slot 0|1|2|3]");
    console.log(
      JSON.stringify(
        plan(args.length ? { slot: Number(args[1]) } : {}),
        null,
        2,
      ),
    );
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
module.exports = { plan };
