// Shared shader build entry point. All paths are explicit; no shell interpolation.
const fs = require('node:fs/promises'),
  path = require('node:path'),
  { execFileSync } = require('node:child_process');
const { tool } = require('./tools/toolchain.cjs');
async function headers(directory) {
  await fs.mkdir(directory, { recursive: true });
  for (const [source, target] of [
    ['state.glsl', 'abi.glsl'],
    ['hb.glsl', 'hb.glsl'],
    ['pad-merge.glsl', 'pad-merge.glsl'],
    ['draw.glsl', 'draw.glsl'],
  ])
    await fs.copyFile(path.join(__dirname, 'include', source), path.join(directory, target));
}
function compile(source, output) {
  execFileSync(
    tool('GLSLANG', 'glslangValidator'),
    ['-V', '-Os', '-g0', '--target-env', 'vulkan1.0', source, '-o', output],
    { stdio: 'inherit' },
  );
  execFileSync(tool('SPIRV_VAL', 'spirv-val'), ['--target-env', 'vulkan1.0', output], {
    stdio: 'inherit',
  });
}
async function buildApp(directory) {
  directory = path.resolve(directory);
  await headers(path.join(directory, 'shaders'));
  for (const stage of ['vert', 'frag', 'comp'])
    compile(
      path.join(directory, 'shaders/app.' + stage),
      path.join(directory, 'app.' + stage + '.spv'),
    );
}
module.exports = { headers, compile, buildApp };
if (require.main === module)
  buildApp(process.argv[2] || '.').catch((e) => {
    console.error(e);
    process.exitCode = 1;
  });
