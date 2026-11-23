// Included by the SD owner, so profiles share its file handles and lock.
namespace ee_warm {

size_t ReadSdProfile(const std::string& key, std::span<uint8_t> output)
{
    std::lock_guard<std::mutex> lock(owner);
    if (!filesystem.ready || !ps2::Key(key) || !key.starts_with("ee-warm2-"))
        return 0;

    const Slot slots[]{Inspect(key, 0, output.size()), Inspect(key, 1, output.size())};
    SdSaveResult result;
    const int index = Select(slots, result);
    if (index < 0)
        return 0;

    Ps2SdkFile file{};
    if (ps2_sdk_fs_open(&filesystem, Name(key, index).c_str(), PS2_FS_READ, &file))
        return 0;
    const Slot& slot = slots[index];
    const bool valid = file.size == Header + slot.bytes && Read(file, Header, output.data(), slot.bytes) &&
        (Crc(~0u, output.data(), slot.bytes) ^ ~0u) == slot.crc;
    const uint32_t closed = ps2_sdk_fs_close(&file);
    return valid && !closed ? slot.bytes : 0;
}

bool WriteSdProfile(const std::string& key, std::span<const uint8_t> input)
{
    if (!key.starts_with("ee-warm2-") || input.empty() || input.size() > MaxBytes)
        return false;
    const auto reader = [](void* opaque, uint64_t offset, void* output, size_t size, size_t* actual) -> uint32_t {
        const auto bytes = *static_cast<const std::span<const uint8_t>*>(opaque);
        if (offset > bytes.size() || size > bytes.size() - offset)
            return PS2_FS_BAD_REPLY;
        std::memcpy(output, bytes.data() + offset, size);
        *actual = size;
        return 0;
    };
    std::lock_guard<std::mutex> lock(owner);
    return StoreReaderLocked(key, input.size(), reader, &input, nullptr, MaxBytes).success;
}

} // namespace ee_warm
