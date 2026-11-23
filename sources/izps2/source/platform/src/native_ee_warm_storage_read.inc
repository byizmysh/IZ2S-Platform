// Included by the SD owner. The same header parser and generation selector
// are used for streamed saves and for the small in-memory EE profile.
namespace {

Slot ReadWarmSlot(const std::string& key, unsigned index, std::span<uint8_t> buffer)
{
    Ps2SdkFile file{};
    const auto rc = ps2_sdk_fs_open(&filesystem, Name(key, index).c_str(), PS2_FS_READ, &file);
    if (rc)
        return {filesystem.api.is_not_found && filesystem.api.is_not_found(rc) ? Kind::Missing : Kind::Error};

    Slot slot{Kind::Corrupt};
    if (file.size >= Header && file.size <= buffer.size())
    {
        auto bytes = buffer.first(static_cast<size_t>(file.size));
        if (!Read(file, 0, bytes.data(), bytes.size()))
            slot.kind = Kind::Error;
        else
        {
            slot = ParseSlotHeader(key, file.size, bytes.size() - Header, bytes.data());
            if (slot.kind == Kind::Valid && (Crc(~0u, bytes.data() + Header, slot.bytes) ^ ~0u) != slot.crc)
                slot.kind = Kind::Corrupt;
        }
    }
    if (ps2_sdk_fs_close(&file))
        slot.kind = Kind::Error;
    return slot;
}

} // namespace

namespace ee_warm {

size_t ReadSdProfile(const std::string& key, std::span<uint8_t> output)
{
    std::lock_guard<std::mutex> lock(owner);
    if (!filesystem.ready || !ps2::Key(key) || !key.starts_with("ee-warm2-"))
        return 0;

    const auto capacity = Header + std::min<size_t>(output.size(), MaxBytes);
    std::vector<uint8_t> buffers(2 * capacity);
    const auto first = std::span(buffers).first(capacity);
    const auto second = std::span(buffers).last(capacity);
    const Slot slots[]{ReadWarmSlot(key, 0, first), ReadWarmSlot(key, 1, second)};
    SdSaveResult result;
    const int index = Select(slots, result);
    if (index < 0)
        return 0;

    const auto payload = (index == 0 ? first : second).subspan(Header, slots[index].bytes);
    std::copy(payload.begin(), payload.end(), output.begin());
    return payload.size();
}
