function check(version = process.versions.node) {
  const [major, minor] = version.split('.').map(Number);
  if (!Number.isInteger(major) || !Number.isInteger(minor) || major < 18 || (major === 18 && minor < 17))
    throw Error('Super File Bros. needs Node.js 18.17 or newer, matching IZ2S Platform.');
}
module.exports = {check};
