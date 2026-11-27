// HIPC transport helpers - packet construction, descriptors, and dispatch.

// -- Low-level IPC ------------------------------------------------

// Zero a buffer and write the HIPC header + extra words.
//   tag0 = HIPC word0 (type + descriptor counts)
//   tag1 = HIPC word1 (raw_words + flags)
//   words = [[offset, value], ...] - additional u32 writes
function prepPacket(buf, tag0, tag1, words) {
    for (let i = 0n; i < 0x200n; i += 8n) write64(buf + i, 0n);
    write32(buf, BigInt(tag0 >>> 0));
    write32(buf + 4n, BigInt(tag1 >>> 0));
    for (const [off, v] of words)
        write32(buf + BigInt(off), BigInt(v >>> 0));
}

// Write an X (pointer) buffer descriptor at the given offset.
// Switchbrew format:
//   word0 bits 31:16=size, 15:12=address[35:32], 8:6=address[38:36], 5:0=index
//   word1: addr[31:0]
// descriptorIndex is the ordinal in the request's X-descriptor list (0, 1, ...).
function writeXDesc(buf, descOffset, dataAddr, dataSize, descriptorIndex) {
    descriptorIndex = descriptorIndex || 0;
    const addrLow  = Number(dataAddr & 0xFFFFFFFFn);
    const addr3235 = Number((dataAddr >> 32n) & 0xFn);
    const addr3638 = Number((dataAddr >> 36n) & 0x7n);
    const word0 = (dataSize << 16) | (addr3638 << 6) | (addr3235 << 12) |
        (descriptorIndex & 0x3F);
    write32(buf + BigInt(descOffset), BigInt(word0 >>> 0));
    write32(buf + BigInt(descOffset + 4), BigInt(addrLow >>> 0));
}

// Send an IPC message via the dispatcher using a heap buffer.
// Forces the svc #0x22 path (heap buffer != tpidrro_el0).
function sendIPC(handle, buf) {
    return call_rop(IPC_SEND_DISPATCHER, BigInt(handle), buf, 0x1000n);
}

// Close a kernel handle (svc #0x16 wrapper)
function closeHandle(handle) {
    return call_rop(F_CloseHandle, BigInt(handle));
}
