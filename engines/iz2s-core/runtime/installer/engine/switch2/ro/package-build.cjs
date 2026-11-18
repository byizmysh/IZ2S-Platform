// Build a flat PFS0 ExeFS package from the exact image and signed policy.
const fs = require('node:fs/promises');

function align(value, size) {
  return (value + size - 1) & -size;
}

async function build(imagePath, policyPath, outputPath) {
  const files = [
    {name: 'main', bytes: await fs.readFile(imagePath)},
    {name: 'main.npdm', bytes: await fs.readFile(policyPath)},
  ];
  const names = Buffer.from(files.map(file => file.name + '\0').join(''));
  const headerSize = 16 + files.length * 24 + names.length;
  const dataStart = headerSize;
  let dataSize = 0;
  for (const file of files) {
    dataSize = align(dataSize, 0x20);
    file.offset = dataSize;
    dataSize += file.bytes.length;
  }
  const output = Buffer.alloc(dataStart + dataSize);
  output.write('PFS0');
  output.writeUInt32LE(files.length, 4);
  output.writeUInt32LE(names.length, 8);
  let nameOffset = 0;
  for (const [index, file] of files.entries()) {
    const entry = 16 + index * 24;
    output.writeBigUInt64LE(BigInt(file.offset), entry);
    output.writeBigUInt64LE(BigInt(file.bytes.length), entry + 8);
    output.writeUInt32LE(nameOffset, entry + 16);
    file.bytes.copy(output, dataStart + file.offset);
    nameOffset += Buffer.byteLength(file.name) + 1;
  }
  names.copy(output, 16 + files.length * 24);
  await fs.writeFile(outputPath, output);
  return {file: outputPath, size: output.length};
}

module.exports = {build};
