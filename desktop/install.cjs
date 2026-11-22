#!/usr/bin/env node
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const ID = 'izlauncher-run25';
function desktopEntry({ root, icon }) {
  const quote = value => '"' + value.replace(/[\\"`$]/g, "\\$&").replace(/%/g, "%%") + '"';
  return `[Desktop Entry]\nVersion=1.0\nType=Application\nName=IZ2S Platform 0.3.0\nGenericName=Switch 2 control panel\nComment=Open IZ2S Platform in a browser window\nExec=${quote(path.join(root, "start.sh"))}\nPath=${root}\nIcon=${icon}\nTerminal=false\nStartupNotify=false\nCategories=Game;\nKeywords=IZ2S;IZ2s;IZPSlauncher;Switch;0.3.0;\n`;
}
async function install({
  home = os.homedir(),
  dataHome = process.env.XDG_DATA_HOME || path.join(home, ".local/share"),
  desktopShortcut = true,
} = {}) {
  const root = path.resolve(__dirname, "..");
  const applications = path.join(dataHome, "applications");
  const icon = path.join(
    dataHome,
    "icons/hicolor/scalable/apps/" + ID + ".svg",
  );
  const files = [path.join(applications, ID + ".desktop")];
  if (desktopShortcut) {
    let desktop = path.join(home, "Desktop");
    try {
      const configuration = await fs.readFile(
        path.join(process.env.XDG_CONFIG_HOME || path.join(home, ".config"), "user-dirs.dirs"),
        "utf8",
      );
      const match = /^XDG_DESKTOP_DIR="([^"]+)"$/m.exec(configuration);
      if (match) desktop = match[1].replace(/^\$HOME(?=\/|$)/, home);
    } catch (error) {
      if (error.code !== "ENOENT") throw error;
    }
    await fs.mkdir(desktop, { recursive: true });
    files.push(path.join(desktop, ID + ".desktop"));
  }
  await fs.mkdir(applications, { recursive: true });
  await fs.mkdir(path.dirname(icon), { recursive: true });
  await fs.copyFile(path.join(__dirname, "izlauncher.svg"), icon);
  const entry = desktopEntry({ root, icon });
  for (const file of files) {
    await fs.writeFile(file, entry, { mode: 0o755 });
    await fs.chmod(file, 0o755);
  }
  return { files, icon };
}
if (require.main === module)
  install({ desktopShortcut: !process.argv.includes('--menu-only') })
    .then((result) => console.log(JSON.stringify(result, null, 2)))
    .catch((error) => {
      console.error(error.message);
      process.exitCode = 1;
    });
module.exports = { install, desktopEntry };
