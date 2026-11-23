// SPDX-License-Identifier: GPL-3.0+
#include "Common.h"
#include "Gif_Unit.h"

#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
#include <cstdio>
#include <cstdlib>
#include <zlib.h>
#endif

namespace
{
#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
const bool fast_packed_scan = std::getenv("IZ_LOCAL_FAST_GIF_SCAN") != nullptr;
u32 packet_crc[3]{};
u32 packet_meta_crc[3]{};
u64 packet_bytes[3]{};
u64 packet_count[3]{};
u64 scan_count = 0;
u64 ad_callbacks = 0;
u64 skipped_addresses = 0;
u64 dense_packets = 0;

void RecordCompletedPacket(const u8* buffer, GIF_PATH path, const GS_Packet& packet)
{
    const unsigned i = static_cast<unsigned>(path);
    if (i >= 3)
        std::abort();
    packet_crc[i] = crc32(packet_crc[i], buffer + packet.offset, packet.size);
    const u32 meta[] = {packet.offset, packet.size, static_cast<u32>(packet.cycles), static_cast<u32>(packet.readAmount)};
    packet_meta_crc[i] = crc32(packet_meta_crc[i], reinterpret_cast<const u8*>(meta), sizeof(meta));
    packet_bytes[i] += packet.size;
    ++packet_count[i];
}
#else
constexpr bool fast_packed_scan = true;
#endif

template <bool AllAd>
bool ScanPackedAd(u8* buffer, Gif_Tag& tag, GS_Packet& packet, u32& offset, u32 available)
{
    const u32 num_regs = tag.nRegs;
    const u32 total = tag.nLoop * num_regs - tag.nRegIdx;
    const u32 count = std::min(available, total);
    u8* cursor = buffer + offset;
    u32 reg_index = tag.nRegIdx;
    u32 remaining_loops = tag.nLoop;
    u32 consumed = 0;
    bool stalled = false;
#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
    ++scan_count;
    dense_packets += AllAd;
    u32 callbacks = 0;
    u32 skipped = 0;
#endif
    while (consumed < count)
    {
        if (AllAd || tag.regs[reg_index] == GIF_REG_A_D)
        {
            const u32 address = cursor[8];
            // All A+D data still goes to GS. Only these registers change EE state.
            if (address - GIF_A_D_REG_BITBLTBUF <= GIF_A_D_REG_TRXDIR - GIF_A_D_REG_BITBLTBUF ||
                address - GIF_A_D_REG_SIGNAL <= GIF_A_D_REG_LABEL - GIF_A_D_REG_SIGNAL)
            {
                stalled = Gif_HandlerAD(cursor);
#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
                ++callbacks;
#endif
            }
#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
            else
                ++skipped;
#endif
        }
        cursor += 16;
        ++consumed;
        if constexpr (!AllAd)
        {
            if (++reg_index == num_regs)
            {
                reg_index = 0;
                --remaining_loops;
            }
        }
        if (stalled)
            break;
    }
    if constexpr (AllAd)
        remaining_loops -= consumed;
    tag.nLoop = remaining_loops;
    tag.nRegIdx = reg_index;
    incTag(offset, packet.size, consumed * 16);
#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
    ad_callbacks += callbacks;
    skipped_addresses += skipped;
#endif
    return stalled;
}
}

#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
extern "C" void __real__ZN9VMManager8ShutdownEb(bool save_resume);
extern "C" void __wrap__ZN9VMManager8ShutdownEb(bool save_resume)
{
    __real__ZN9VMManager8ShutdownEb(save_resume);
    for (unsigned i = 0; i < 3; ++i)
        std::fprintf(stderr, "LOCAL_GIF_PACKETS path=%u packets=%llu bytes=%llu crc=%08x meta_crc=%08x\n",
            i, static_cast<unsigned long long>(packet_count[i]), static_cast<unsigned long long>(packet_bytes[i]),
            packet_crc[i], packet_meta_crc[i]);
    std::fprintf(stderr, "LOCAL_GIF_SCAN scans=%llu dense=%llu callbacks=%llu skipped=%llu\n",
        static_cast<unsigned long long>(scan_count), static_cast<unsigned long long>(dense_packets),
        static_cast<unsigned long long>(ad_callbacks), static_cast<unsigned long long>(skipped_addresses));
}
#endif

// Bind the new implementation to the same non-virtual ELF entry. Keep the
// original shared class definition and the calling convention of its member.
[[gnu::noinline, gnu::visibility("hidden")]]
GS_Packet RunGifPackedPath(Gif_Path* path, bool& done) asm("_ZN8Gif_Path15ExecuteGSPacketERb");

GS_Packet RunGifPackedPath(Gif_Path* path, bool& done)
	{
        auto& mtvu = path->mtvu;
        auto& gifTag = path->gifTag;
        auto& curOffset = path->curOffset;
        auto& curSize = path->curSize;
        auto& gsPack = path->gsPack;
        auto& state = path->state;
        auto& dmaRewind = path->dmaRewind;
        const u32 buffLimit = path->buffLimit;
        u8* const buffer = path->buffer;
        const auto isMTVU = [path] { return path->isMTVU(); };
		if (mtvu.fakePackets)
		{ // For MTVU mode...
			mtvu.fakePackets--;
			done = true;
			return mtvu.fakePacket;
		}
		pxAssert(!isMTVU());
		for (;;)
		{
			if (!gifTag.isValid)
			{ // Need new Gif Tag
				// We don't have enough data for a Gif Tag
				if (curOffset + 16 > curSize)
				{
					//GUNIT_LOG("Path Buffer: Not enough data for gif tag! [%d]", curSize-curOffset);
					GUNIT_WARN("PATH %d not enough data pre tag, available %d wanted %d", gifRegs.stat.APATH, curSize - curOffset, 16);
					return gsPack;
				}

				// Move packet to start of buffer
				if (curOffset > buffLimit)
				{
					path->RealignPacket();
				}

				gifTag.setTag(&buffer[curOffset], 1);

				state = (GIF_PATH_STATE)(gifTag.tag.FLG + 1);
				GUNIT_WARN("PATH %d New tag State %d FLG %d EOP %d NLOOP %d", gifRegs.stat.APATH, gifRegs.stat.APATH, state, gifTag.tag.FLG, gifTag.tag.EOP, gifTag.tag.NLOOP);
				// We don't have enough data for a complete GS packet
				if (!gifTag.hasAD && curOffset + 16 + gifTag.len > curSize)
				{
					gifTag.isValid = false; // So next time we test again
					GUNIT_WARN("PATH %d not enough data, available %d wanted %d", gifRegs.stat.APATH, curSize - curOffset, 16 + gifTag.len);
					return gsPack;
				}

				incTag(curOffset, gsPack.size, 16); // Tag Size
				gsPack.cycles += 2 + gifTag.cycles; // Tag + Len ee-cycles
			}

			if (gifTag.hasAD)
			{ // Only can be true if GIF_FLG_PACKED
				bool dblSIGNAL = false;
                if (fast_packed_scan && !isMTVU() && gifTag.nLoop && curOffset <= curSize &&
                    gifTag.nRegs >= 1 && gifTag.nRegs <= 16 && gifTag.nRegIdx < gifTag.nRegs)
                {
                    const u32 available = (curSize - curOffset) / 16;
                    if (gifTag.nRegs == 1 && gifTag.regs[0] == GIF_REG_A_D)
                        dblSIGNAL = ScanPackedAd<true>(buffer, gifTag, gsPack, curOffset, available);
                    else
                        dblSIGNAL = ScanPackedAd<false>(buffer, gifTag, gsPack, curOffset, available);
                }
                // Retain the original short-transfer and unusual-state behavior.
				while (gifTag.nLoop && !dblSIGNAL)
				{
					if (curOffset + 16 > curSize)
					{
						GUNIT_WARN("PATH %d not enough data AD, available %d wanted %d", gifRegs.stat.APATH, curSize - curOffset, 16);
						return gsPack; // Exit Early
					}
					if (gifTag.curReg() == GIF_REG_A_D)
					{
						if (!isMTVU())
							dblSIGNAL = Gif_HandlerAD(&buffer[curOffset]);
					}
					incTag(curOffset, gsPack.size, 16); // 1 QWC
					gifTag.packedStep();
				}
				if (dblSIGNAL && !(gifTag.tag.EOP && !gifTag.nLoop))
				{
					GUNIT_WARN("PATH %d early exit (double signal)", gifRegs.stat.APATH);
					return gsPack; // Exit Early
				}
			}
			else
				incTag(curOffset, gsPack.size, gifTag.len); // Data length

			// Reload gif tag next loop
			gifTag.isValid = false;

			if (gifTag.tag.EOP)
			{
				GS_Packet t = gsPack;
				done = true;

				dmaRewind = 0;

				gsPack.Reset();
				gsPack.offset = curOffset;
				GUNIT_WARN("EOP PATH %d", gifRegs.stat.APATH);
				//Path 3 Masking is timing sensitive, we need to simulate its length! (NFSU2/Outrun 2006)

				if ((gifRegs.stat.APATH - 1) == GIF_PATH_3)
				{
					state = GIF_PATH_WAIT;

					if (curSize - curOffset > 0 && (gifRegs.stat.M3R || gifRegs.stat.M3P))
					{
						//Including breaking packets early (Rewind DMA to pick up where left off)
						//but only do this when the path is masked, else we're pointlessly slowing things down.
						dmaRewind = curSize - curOffset;
						curSize = curOffset;
					}
				}
				else
					state = GIF_PATH_IDLE;

#ifdef IZPS2_LOCAL_GIF_SCAN_COMPARE
                RecordCompletedPacket(buffer, path->idx, t);
#endif
				return t; // Complete GS packet
			}
		}
	}
