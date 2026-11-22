// Formatting and byte-writing helpers.

function hex32(value) {
    return "0x" + (Number(value) >>> 0).toString(16).padStart(8, "0");
}

function ticksToUs(ticks) {
    return Number(ticks) * 1000000 / SYSTEM_TICK_FREQUENCY_HZ;
}

function write16(addr, val) {
    write8(addr, val & 0xFFn);
    write8(addr + 1n, (val >> 8n) & 0xFFn);
}
