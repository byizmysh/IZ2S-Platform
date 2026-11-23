namespace {
struct WarmRecord {
    u32 pc;
    u32 size;
    u64 hash;
};

struct WarmCapture {
    std::array<WarmRecord, ps2::ee_warm::MaxEntries> records;
    static_assert(ps2::ee_warm::MaxEntries <= UINT16_MAX);
    std::array<u16, std::bit_ceil(2 * ps2::ee_warm::MaxEntries)> indexes;
    u32 count = 0;
    u32 disc = 0;
    u32 elf = 0;
};

static WarmCapture warm_capture;

static void capture_ee_block(u32 address, u32 size, const u8* direct)
{
    if (!warm_capture.disc || warm_capture.count == warm_capture.records.size() ||
        !eeWarmSpanEligible(address, size) || direct != eeMem->Main + address ||
        !eeWarmDelaySlotVerified(direct, size))
        return;

    // Keep first use order and avoid hashing the same address on a later clear.
    auto& capture = warm_capture;
    const u32 mask = capture.indexes.size() - 1;
    u32 slot = ((address >> 2) * 2654435761u) >> (32 - std::bit_width(mask));
    while (capture.indexes[slot])
    {
        if (capture.records[capture.indexes[slot] - 1].pc == address)
            return;
        slot = (slot + 1) & mask;
    }
    capture.records[capture.count] = {address, size, ps2::ee_warm::Hash({direct, size})};
    capture.indexes[slot] = ++capture.count;
}
} // namespace

void recEeStartWarmProfile(u32 disc, u32 elf)
{
    warm_capture.count = 0;
    warm_capture.disc = elf ? disc : 0;
    warm_capture.elf = elf;
    warm_capture.indexes.fill(0);
}

size_t recEeFinishWarmProfile(const std::string& key, std::span<uint8_t> output)
{
    using namespace ps2::ee_warm;
    const u32 disc = std::exchange(warm_capture.disc, 0);
    const u32 elf = warm_capture.elf;
    const u32 count = warm_capture.count;
    const size_t bytes = HeaderBytes + count * RecordBytes;
    if (!disc || !count || output.size() < bytes || Key(disc, elf) != key)
        return 0;

    std::copy(Magic.begin(), Magic.end(), output.begin());
    WriteLE(output.subspan(8, 4), disc);
    WriteLE(output.subspan(12, 4), elf);
    WriteLE(output.subspan(16, 4), count);
    WriteLE(output.subspan(20, 4), 0);
    for (u32 i = 0; i < count; ++i)
    {
        const auto& record = warm_capture.records[i];
        auto row = output.subspan(HeaderBytes + i * RecordBytes, RecordBytes);
        WriteLE(row.first(4), record.pc);
        WriteLE(row.subspan(4, 4), record.size);
        WriteLE(row.last(8), record.hash);
    }
    return bytes;
}
