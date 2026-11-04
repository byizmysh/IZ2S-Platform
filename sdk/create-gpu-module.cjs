const fs = require('node:fs/promises'),
  path = require('node:path'),
  { create } = require('./create-extension.cjs');
function icon() {
  const { crc32 } = require('../core/archive.cjs'),
    z = require('node:zlib'),
    chunk = (name, b) => {
      const type = Buffer.from(name),
        length = Buffer.alloc(4),
        crc = Buffer.alloc(4);
      length.writeUInt32BE(b.length);
      crc.writeUInt32BE(crc32(Buffer.concat([type, b])));
      return Buffer.concat([length, type, b, crc]);
    },
    h = Buffer.alloc(13);
  h.writeUInt32BE(32);
  h.writeUInt32BE(32, 4);
  h[8] = 8;
  h[9] = 6;
  const rows = Buffer.alloc(32 * (1 + 32 * 4));
  for (let y = 0; y < 32; y++)
    for (let x = 0; x < 32; x++) {
      const at = y * 129 + 1 + x * 4;
      rows[at] = 183;
      rows[at + 1] = 239;
      rows[at + 2] = 48;
      rows[at + 3] = 255;
    }
  return Buffer.concat([
    Buffer.from('89504e470d0a1a0a', 'hex'),
    chunk('IHDR', h),
    chunk('IDAT', z.deflateSync(rows)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}
async function make(id, destination) {
  await create('module', id, destination);
  const folder = path.join(destination, 'switch');
  await fs.mkdir(path.join(folder, 'shaders'), { recursive: true });
  await fs.writeFile(path.join(folder, 'icon.png'), icon());
  await fs.rm(path.join(destination, 'server'), { recursive: true });
  await fs.writeFile(path.join(folder, 'state.bin'), Buffer.alloc(1024));
  await fs.writeFile(path.join(folder, 'data.bin'), Buffer.alloc(4));
  await fs.writeFile(
    path.join(folder, 'shaders/app.vert'),
    '#version 450\nvoid main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0,1);}\n',
  );
  await fs.writeFile(
    path.join(folder, 'shaders/app.frag'),
    '#version 450\nlayout(location=0) out vec4 color;void main(){color=vec4(0.72,0.94,0.19,1);}\n',
  );
  await fs.writeFile(
    path.join(folder, 'shaders/app.comp'),
    '#version 450\n#extension GL_GOOGLE_include_directive : require\nlayout(local_size_x=1) in;\n#include "abi.glsl"\n#include "hb.glsl"\nvoid main(){if((hbPressed()&HB_B)!=0u)hbExit();hbEndFrame();}\n',
  );
  const app = {
    format: 'hblauncher.gpu-app',
    abi: 1,
    id,
    name: id,
    version: '1.0.0',
    description: 'IZlauncher SDK example; B returns to menu',
    icon: 'icon.png',
    initialState: 'state.bin',
    data: 'data.bin',
    shaders: { vertex: 'app.vert.spv', fragment: 'app.frag.spv', compute: 'app.comp.spv' },
    sdk: { version: 1, capabilities: [] },
  };
  await fs.writeFile(path.join(folder, 'app.json'), JSON.stringify(app, null, 2));
  await require('./build.cjs').buildApp(folder);
  const files = [];
  async function walk(dir) {
    for (const e of await fs.readdir(dir, { withFileTypes: true })) {
      const f = path.join(dir, e.name);
      if (e.isDirectory()) await walk(f);
      else files.push('switch/' + path.relative(folder, f));
    }
  }
  await walk(folder);
  const d = JSON.parse(await fs.readFile(path.join(destination, 'module.json')));
  delete d.server;
  d.switch = { entry: 'switch/app.json', files, abi: 1 };
  d.requires = { platformApi: 1, shell: 'izlauncher', consoleAbi: 1 };
  await fs.writeFile(path.join(destination, 'module.json'), JSON.stringify(d, null, 2) + '\n');
  return destination;
}
if (require.main === module) {
  const [id, target] = process.argv.slice(2);
  make(id, path.resolve(target || id || '.'))
    .then(console.log)
    .catch((e) => {
      console.error(e.stack);
      process.exitCode = 1;
    });
}
module.exports = { make };
