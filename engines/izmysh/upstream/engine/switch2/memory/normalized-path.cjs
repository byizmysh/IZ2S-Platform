// Build an already normalized missing path from short components.
module.exports = function normalizedPath(prefix, length) {
  if (!prefix.startsWith('/') || prefix.includes('//') || prefix.endsWith('/') || prefix.length > length)
    throw Error('Invalid normalized path prefix');
  let text = prefix;
  while (text.length < length) {
    const remaining = length - text.length;
    text += remaining === 1 ? 'A' : '/' + 'A'.repeat(Math.min(32, remaining - 1));
  }
  return text;
};
