// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSHeadlessPresent.h"

#include "GS/GS.h"
#ifdef PS2_NATIVE_GSVK_EXTERNAL
#include "GS/GSPerfMon.h"
#endif
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSTexture.h"
#ifdef PS2_NATIVE_GSVK_EXTERNAL
#include "GS/Renderers/Vulkan/GSTextureVK.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "GSNativeOpaqueReadbackCopy.h"
#include <cstdio>
#include "native_hwgs_av_contract_v1.h"
#include "native_readback_timing.h"
#include "native_readback_work_timing.h"
extern "C" {
#include "hbxl.h"
#include "hbx_sdk.h"
}

extern "C" {
[[gnu::used, gnu::visibility("hidden")]] NativeReadbackTiming ps2_native_readback_timing;
[[gnu::used, gnu::visibility("hidden")]] NativeReadbackWorkTiming ps2_native_readback_work;
}
#endif

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Image.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include <algorithm>
#include <atomic>
#include <memory>

namespace GSHeadless
{
	namespace
	{
		std::atomic<bool> s_enabled{false};
		std::atomic<u64> s_present_count{0};

		// GS thread only. Not owned.
		GSTexture* s_presented = nullptr;
#ifdef PS2_NATIVE_GSVK_EXTERNAL
		// Explicit GS ownership: no static destructor may call a dead VkDevice.
		// Normal release is ForgetPresented, before DestroyResources/allocator.
		struct NativeReadbackSlot {
			GSDownloadTextureVK* download = nullptr;
			u64 serial = 0, epoch = 0, copy_fence = 0;
			u32 guest_frame = 0;
			unsigned stage = 0; // 0 free, 1 accepted-submit/pending, 2 fence-complete, 3 awaiting submit
		};
		NativeReadbackSlot s_native_slots[2];
		// One later-present opportunity from an actual positive-serial request.
		// A poll-only call grants nothing: no persistent eager capture.
		bool s_native_capture_credit = false;
		struct PreparedReadback {
			GSTexture* source = nullptr; // non-owning; checked before the ring rotates
			u64 fence = 0;
			unsigned slot = 0;
		};
		PreparedReadback s_native_prepared{}; // GS-owner POD; no destructor/worker
		GSDevice* s_native_readback_device = nullptr;
		unsigned s_native_mode = 0; // 1 legacy synchronous, 2 private async; never mix.
		u64 s_native_epoch = 1, s_native_seen = 0, s_native_delivered = 0, s_native_accepted = 0;
		u64 s_native_queued = 0, s_native_completed = 0, s_native_reads = 0;
		u64 s_native_presenter_accepted = 0, s_native_dropped = 0, s_native_discarded = 0, s_native_bootstraps = 0;
		bool s_native_bootstrap_needed = true;
		NativeReadbackClosed s_native_closed{}; // Published by real close, never a GPU owner.

		u64 ReadbackTimingFrequency()
		{
			const hbx_ctx* ctx = hbxl_ctx;
			if (ctx && ctx->magic == HBX_ABI_MAGIC && ctx->abi_version == 3 && ctx->size >= sizeof(*ctx) &&
				ctx->flags == 0 && ctx->tick_hz && ctx->tick_hz <= 1000000000ull)
				return ctx->tick_hz;
			return 0;
		}

		template<class Call>
		auto TimeReadback(NativeReadbackWorkTiming::Stage& stage, Call call)
		{
			const u64 hz = ReadbackTimingFrequency();
			const u64 begin = hz ? hbxl_svc_tick() : 0;
			const auto result = call();
			if (hz)
				ps2_native_readback_work.Record(stage, hz, begin, hbxl_svc_tick());
			return result;
		}

		bool CollectCompletedReadbacks(u64 recording_fence)
		{
			bool pending = false;
			for (NativeReadbackSlot& slot : s_native_slots)
			{
				if (slot.stage != 1) continue;
				if (slot.download->Poll())
				{
					slot.stage = 2;
					++s_native_completed;
				}
				else if (slot.copy_fence < recording_fence) pending = true;
			}
			return pending;
		}

		void PollNativeReadbackSlots()
		{
			// Poll() first uses the completion counter already advanced by the GS owner.
			// Keep the normal scan while spinning is configured, since it also retires spin queries.
			auto* const device = GSDeviceVK::GetInstance();
			const u64 recording_fence = device->GetCurrentFenceCounter();
			const bool pending = CollectCompletedReadbacks(recording_fence);
			if (pending || GSConfig.HWSpinGPUForReadbacks)
			{
				const u64 completed_before = pending ? device->GetCompletedFenceCounter() : 0;
				++ps2_native_readback_timing.scans;
				TimeReadback(ps2_native_readback_work.scan, [&] {
					device->ScanForCommandBufferCompletion();
					return true;
				});
				// Pending Poll() results only change when GS advances its completion counter.
				if (pending && device->GetCompletedFenceCounter() != completed_before)
					CollectCompletedReadbacks(recording_fence);
			}
			else
			{
				++ps2_native_readback_timing.skipped_scans;
				if (std::any_of(std::begin(s_native_slots), std::end(s_native_slots),
					[](const NativeReadbackSlot& slot) { return slot.stage == 1; }))
					++ps2_native_readback_work.unsubmitted_skips;
			}
		}
#endif
	} // namespace

	void SetEnabled(bool enabled)
	{
		s_enabled.store(enabled, std::memory_order_relaxed);
	}

	bool IsEnabled()
	{
		return s_enabled.load(std::memory_order_relaxed);
	}

	void NotePresented(GSTexture* backbuffer)
	{
		s_presented = backbuffer;
		s_present_count.fetch_add(1, std::memory_order_release);
	}

	void ForgetPresented()
	{
#ifdef PS2_NATIVE_GSVK_EXTERNAL
		if (s_native_prepared.source)
			ps2_hwgs_retain_ownership("native-readback-unaccepted-submit-close", -3);
		s_native_capture_credit = false;
#endif
		s_presented = nullptr;
#ifdef PS2_NATIVE_GSVK_EXTERNAL
		for (NativeReadbackSlot& slot : s_native_slots)
		{
			if (!slot.download) continue;
			if (s_native_readback_device != g_gs_device.get() ||
				g_gs_device->GetRenderAPI() != RenderAPI::Vulkan)
				ps2_hwgs_retain_ownership("readback-release-device-owner", -3);
			// GSDeviceVK::Destroy first submits still-recording copies, then
			// NativeBeforeDestroy/WaitForGPUIdle drain, before base Destroy calls us.
			// Retain the existing exact copy-fence proof before each VMA free.
			slot.download->ReleaseCompletedNativeReadback();
			if (slot.stage == 1) ++s_native_completed;
			delete slot.download; slot = {};
		}
		s_native_readback_device = nullptr;
		if (s_native_mode == 2)
		{
			s_native_closed={1,sizeof(NativeReadbackClosed),s_native_epoch,s_native_queued,s_native_completed,
				s_native_reads,s_native_presenter_accepted,s_native_dropped,s_native_discarded,s_native_bootstraps};
			std::fprintf(stderr,"PS2_ASYNC_READBACK queued=%llu completed=%llu read=%llu presenterAccepted=%llu dropped=%llu discarded=%llu bootstrapSyncs=%llu liveSlots=0 epoch=%llu\n",
				(unsigned long long)s_native_queued,(unsigned long long)s_native_completed,
				(unsigned long long)s_native_reads,(unsigned long long)s_native_presenter_accepted,
				(unsigned long long)s_native_dropped,(unsigned long long)s_native_discarded,
				(unsigned long long)s_native_bootstraps,(unsigned long long)s_native_epoch);
		}
		else s_native_closed={};
		// Reopening a GS device in the same native image starts a fresh epoch.
		if (s_native_epoch == ~u64(0)) ps2_hwgs_retain_ownership("native-readback-close-epoch", -3);
		ps2_native_readback_timing.have_previous = 0;
		++s_native_epoch; s_native_seen = PresentCount();
		s_native_delivered = s_native_accepted = 0; s_native_bootstrap_needed = true;
		s_native_mode = 0;
#endif
	}

	u64 PresentCount()
	{
		return s_present_count.load(std::memory_order_acquire);
	}

	bool CaptureFreshFrame(Frame* out, Error* error)
	{
		if (!IsEnabled())
		{
			Error::SetStringView(error, "headless present is not enabled (start the host with --headless WxH)");
			return false;
		}
		if (!g_gs_device)
		{
			Error::SetStringView(error, "the GS is not open");
			return false;
		}

		// A present of our own, so the texture below is the one this call just drew. The idle
		// loop would have kept it current anyway, but between its last frame and now the window
		// may have been resized (which retires the old backbuffer) or a page opened.
		const u64 before = PresentCount();
		s_presented = nullptr;
		GSPresentCurrentFrame();
		if (PresentCount() == before || !s_presented)
		{
			Error::SetStringView(error,
				"the renderer did not present an offscreen frame (only the Vulkan and OpenGL backends support headless present)");
			return false;
		}

		return ReadPresentedFrame(PresentCount(), out, error);
	}

	bool ReadPresentedFrame(u64 expected_serial, Frame* out, Error* error)
	{
		if (!IsEnabled() || !g_gs_device || !out || !s_presented ||
			expected_serial == 0 || expected_serial != PresentCount())
		{
			Error::SetStringView(error, "current GS-owner headless frame unavailable");
			return false;
		}
		GSTexture* const tex = s_presented;
		const int width = tex->GetWidth();
		const int height = tex->GetHeight();
		std::unique_ptr<GSDownloadTexture> dl(
			g_gs_device->CreateDownloadTexture(width, height, GSTexture::Format::Color));
		if (!dl)
		{
			Error::SetStringView(error, "could not create a download texture for the readback");
			return false;
		}

		const GSVector4i rc(0, 0, width, height);
		dl->CopyFromTexture(rc, tex, rc, 0);
		dl->Flush();

		out->width = static_cast<u32>(width);
		out->height = static_cast<u32>(height);
		out->pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
		if (!dl->ReadTexels(rc, out->pixels.data(), static_cast<u32>(width) * sizeof(u32)))
		{
			Error::SetStringView(error, "could not map the readback texture");
			return false;
		}

		// OpenGL stores a texture with row 0 at the bottom of the picture, the same convention
		// that makes it present with a lower-left origin.
		if (g_gs_device->UsesLowerLeftOrigin())
		{
			const size_t w = static_cast<size_t>(width);
			for (size_t y = 0; y < static_cast<size_t>(height) / 2; y++)
				std::swap_ranges(out->pixels.begin() + y * w, out->pixels.begin() + (y + 1) * w,
					out->pixels.begin() + (static_cast<size_t>(height) - 1 - y) * w);
		}

		// The present clears to alpha 1 but blending the UI over it leaves whatever the blend
		// equation produced; a screenshot is opaque.
		for (u32& px : out->pixels)
			px |= 0xFF000000u;

		return true;
	}

#ifdef PS2_NATIVE_GSVK_EXTERNAL
	bool NativeReadbackReleased()
	{
		return !s_native_prepared.source && !s_native_slots[0].download && !s_native_slots[1].download && !s_native_readback_device;
	}

	bool CopyNativeReadbackClosed(NativeReadbackClosed* out)
	{
		// Caller is the CPU owner AFTER the real GS join. No polling, wait,
		// allocator access or VkDevice dereference occurs here.
		if (!out || !NativeReadbackReleased() || s_native_mode ||
			s_native_closed.version!=1 || s_native_closed.bytes!=sizeof(NativeReadbackClosed) ||
			s_native_closed.epoch==~u64(0) || s_native_epoch!=s_native_closed.epoch+1)
			return false;
		*out=s_native_closed; return true;
	}

	bool ReadPresentedFrameInto(u64 expected_serial, u32 width, u32 height,
		u32* pixels, size_t bytes, Error* error)
	{
		if (s_native_mode == 2) { Error::SetStringView(error,"native readback sync/async mixed"); return false; }
		s_native_mode = 1;
		GSDownloadTextureVK*& s_native_readback = s_native_slots[0].download;
		if (!IsEnabled() || !g_gs_device || !s_presented || !pixels ||
			expected_serial == 0 || expected_serial != PresentCount() ||
			g_gs_device->GetRenderAPI() != RenderAPI::Vulkan ||
			width == 0 || height == 0 || width > 1024 || height > 1024 ||
			bytes != static_cast<size_t>(width) * height * sizeof(u32) ||
			s_presented->GetWidth() != static_cast<int>(width) ||
			s_presented->GetHeight() != static_cast<int>(height) ||
			s_presented->GetFormat() != GSTexture::Format::Color)
		{
			Error::SetStringView(error, "native readback exact serial/shape/storage unavailable");
			return false;
		}
		if (s_native_readback && (s_native_readback_device != g_gs_device.get() ||
			s_native_readback->GetWidth() != width || s_native_readback->GetHeight() != height ||
			s_native_readback->GetFormat() != GSTexture::Format::Color || s_native_readback->NeedsFlush()))
		{
			Error::SetStringView(error, "native readback owner cannot be replaced or reused while pending");
			return false;
		}
		if (!s_native_readback)
		{
			auto download = GSDownloadTextureVK::Create(width, height, GSTexture::Format::Color);
			if (!download)
			{
				Error::SetStringView(error, "native readback initial staging allocation failed");
				return false;
			}
			s_native_readback = download.release();
			s_native_readback_device = g_gs_device.get();
		}
		const GSVector4i rc(0, 0, width, height);
		s_native_readback->CopyFromTexture(rc, s_presented, rc, 0);
		s_native_readback->Flush();
		if (!s_native_readback->ReadTexels(rc, pixels, width * sizeof(u32)))
		{
			Error::SetStringView(error, "native persistent readback map failed");
			return false;
		}
		if (g_gs_device->UsesLowerLeftOrigin())
		{
			for (size_t y = 0; y < static_cast<size_t>(height) / 2; y++)
				std::swap_ranges(pixels + y * width, pixels + (y + 1) * width,
					pixels + (static_cast<size_t>(height) - 1 - y) * width);
		}
		for (size_t i = 0; i < static_cast<size_t>(width) * height; i++)
			pixels[i] |= 0xFF000000u;
		return true;
	}
	void InvalidateNativeReadback()
	{
		if (s_native_prepared.source)
			ps2_hwgs_retain_ownership("native-readback-unaccepted-submit-invalidate", -3);
		s_native_capture_credit = false;
		ps2_native_readback_timing.have_previous = 0;
		s_native_closed={}; // Old close proof cannot survive Load/reopen.
		if (s_native_mode == 1 || s_native_epoch == ~u64(0))
			ps2_hwgs_retain_ownership("native-async-readback-invalidate-owner", -3);
		++s_native_epoch;
		s_native_seen = PresentCount(); // Exclude the pre-load texture/serial.
		s_native_delivered = s_native_accepted = 0;
		s_native_bootstrap_needed = true;
		// Old-epoch pending slots remain owned until their actual copy completes.
	}

	void NativeReadbackPresenterAccepted(u64 serial)
	{
		if (s_native_mode != 2 || !serial || serial != s_native_delivered || serial <= s_native_accepted)
			ps2_hwgs_retain_ownership("native-async-present-serial", -3);
		s_native_accepted = serial; ++s_native_presenter_accepted;
		const hbx_ctx* ctx = hbxl_ctx;
		if (ctx && ctx->magic == HBX_ABI_MAGIC && ctx->abi_version == 3 && ctx->size >= sizeof(*ctx) &&
			ctx->flags == 0 && ctx->tick_hz && ctx->tick_hz <= 1000000000ull)
			ps2_native_readback_timing.Accept(hbxl_svc_tick(), ctx->tick_hz);
	}

	void NativePreparePresentedReadback(GSTexture* backbuffer)
	{
		if (s_native_prepared.source)
			ps2_hwgs_retain_ownership("native-readback-prepare-reentry", -3);
		// The existing first/Load bootstrap stays post-present and synchronous.
		// Only a real positive-serial consumer request grants one opportunity.
		if (!s_native_capture_credit || s_native_mode != 2 || s_native_bootstrap_needed)
			return;
		s_native_capture_credit = false;
		if (!IsEnabled() || !g_gs_device || s_native_readback_device != g_gs_device.get() ||
			g_gs_device->GetRenderAPI() != RenderAPI::Vulkan || !backbuffer ||
			backbuffer->GetFormat() != GSTexture::Format::Color || PresentCount() == ~u64(0))
			ps2_hwgs_retain_ownership("native-readback-prepare-owner", -3);
		NativeReadbackSlot* free = nullptr;
		unsigned free_index = 0;
		for (unsigned i = 0; i != 2; ++i)
		{
			NativeReadbackSlot& slot = s_native_slots[i];
			if (!slot.download || slot.download->GetWidth() != backbuffer->GetWidth() ||
				slot.download->GetHeight() != backbuffer->GetHeight() ||
				slot.download->GetFormat() != GSTexture::Format::Color || slot.stage > 2)
				ps2_hwgs_retain_ownership("native-readback-prepare-slot", -3);
			if (!free && !slot.stage) { free = &slot; free_index = i; }
		}
		if (!free)
		{
			// Only the full ring needs an extra nonblocking completion scan.
			// Reusing a completed older capture lets this copy join the CURRENT
			// normal present submission. Never steal the sole/newest eligible
			// completed picture from the existing after-vsync consumer.
			PollNativeReadbackSlots();
			NativeReadbackSlot* newest_completed = nullptr;
			for (NativeReadbackSlot& slot : s_native_slots)
				if (slot.stage == 2 && slot.epoch == s_native_epoch &&
					slot.serial > s_native_delivered &&
					(!newest_completed || slot.serial > newest_completed->serial))
					newest_completed = &slot;
			for (NativeReadbackSlot& slot : s_native_slots)
			{
				if (slot.stage != 2) continue;
				const bool obsolete = slot.epoch != s_native_epoch || slot.serial <= s_native_delivered;
				const bool older = newest_completed && slot.epoch == s_native_epoch &&
					slot.serial < newest_completed->serial;
				if (obsolete || older) { slot.stage = 0; ++s_native_discarded; }
			}
			for (unsigned i = 0; i != 2; ++i)
				if (!s_native_slots[i].stage) { free = &s_native_slots[i]; free_index = i; break; }
			if (!free) return;
		}
		const u32 width = backbuffer->GetWidth(), height = backbuffer->GetHeight();
		const GSVector4i rc(0, 0, width, height);
		const u64 fence = GSDeviceVK::GetInstance()->GetCurrentFenceCounter();
		free->download->CopyFromTexture(rc, backbuffer, rc, 0);
		if (GSDeviceVK::GetInstance()->GetCurrentFenceCounter() != fence)
			ps2_hwgs_retain_ownership("native-readback-prepare-counter-changed", -3);
		free->copy_fence = fence;
		free->serial = PresentCount() + 1;
		free->guest_frame = static_cast<u32>(g_perfmon.GetFrame());
		free->epoch = s_native_epoch;
		free->stage = 3; // Recorded commands, NOT an accepted queued capture.
		s_native_prepared = {backbuffer, fence, free_index};
	}

	void NativeAcceptPresentedReadback(GSTexture* backbuffer)
	{
		if (!s_native_prepared.source) return;
		if (s_native_prepared.slot >= 2 || s_native_prepared.source != backbuffer ||
			s_presented != backbuffer || s_native_mode != 2 || !g_gs_device ||
			s_native_readback_device != g_gs_device.get() ||
			GSDeviceVK::GetInstance()->GetCurrentFenceCounter() <= s_native_prepared.fence)
			ps2_hwgs_retain_ownership("native-readback-accepted-submit-owner", -3);
		NativeReadbackSlot& slot = s_native_slots[s_native_prepared.slot];
		if (!slot.download || slot.stage != 3 || slot.epoch != s_native_epoch ||
			slot.serial != PresentCount() || slot.serial <= s_native_seen)
			ps2_hwgs_retain_ownership("native-readback-accepted-submit-ticket", -3);
		// Native SubmitCommandBuffer retains ownership and never returns on
		// error. Only its accepted submit and NotePresented reach this commit.
		// The existing per-copy fence/Poll/Map remains completion authority.
		slot.stage = 1; ++s_native_queued; s_native_seen = slot.serial;
		s_native_prepared = {};
	}

	static int ReadPresentedFrameAsyncImpl(u64 expected_serial, u32 guest_frame, u32 width, u32 height,
		u32* pixels, size_t bytes, PrepareNativeReadbackTarget prepare, ConsumeNativeReadback consume, void* opaque,
		u64* completed_serial, u32* completed_guest_frame, Error* error)
	{
		const bool use_callback = prepare || consume;
		if (!IsEnabled() || !g_gs_device || (!use_callback && !pixels) || !completed_serial || !completed_guest_frame ||
			g_gs_device->GetRenderAPI() != RenderAPI::Vulkan || s_native_mode == 1 ||
			!width || !height || width > 1024 || height > 1024 || (!use_callback && bytes != size_t(width)*height*sizeof(u32)) ||
			(expected_serial > s_native_seen && (!s_presented || expected_serial != PresentCount() ||
				s_presented->GetWidth() != int(width) || s_presented->GetHeight() != int(height) ||
				s_presented->GetFormat() != GSTexture::Format::Color)))
		{ Error::SetStringView(error,"native async readback serial/shape/owner"); return -1; }
		*completed_serial = 0; *completed_guest_frame = 0;
		if (s_native_prepared.source)
			ps2_hwgs_retain_ownership("native-readback-consume-before-submit", -3);
		if (!s_native_readback_device && expected_serial <= s_native_seen) return 0;
		if (expected_serial && expected_serial == PresentCount()) s_native_capture_credit = true;
		if (s_native_readback_device && s_native_readback_device != g_gs_device.get())
		{ Error::SetStringView(error,"native async readback device changed"); return -1; }
		if (s_native_mode != 2) s_native_closed={};
		s_native_mode = 2;
		if (!s_native_readback_device)
		{
			s_native_readback_device = g_gs_device.get();
			for (NativeReadbackSlot& slot : s_native_slots)
			{
				auto download = GSDownloadTextureVK::Create(width,height,GSTexture::Format::Color);
				if (!download) { Error::SetStringView(error,"native async staging allocation"); return -1; }
				slot.download = download.release();
			}
		}
		for (const NativeReadbackSlot& slot : s_native_slots)
			if (!slot.download || slot.download->GetWidth()!=width || slot.download->GetHeight()!=height)
			{ Error::SetStringView(error,"native async staging owner/shape"); return -1; }
		PollNativeReadbackSlots();
		NativeReadbackSlot* newest = nullptr;
		for (NativeReadbackSlot& slot : s_native_slots)
		{
			if (slot.stage != 2) continue;
			if (slot.epoch != s_native_epoch || slot.serial <= s_native_delivered)
			{ slot.stage = 0; ++s_native_discarded; continue; }
			if (!newest || slot.serial > newest->serial) newest = &slot;
		}
		const GSVector4i rc(0,0,width,height);
		bool callback_called = false;
		using Result = NativeReadbackConsumeResult;
		auto read_completed = [&](NativeReadbackSlot& slot) {
			// Poll success cleared NeedsFlush; Map preserves the exact current
			// completed-counter guard and whole-region cache invalidate.
			if (slot.download->NeedsFlush() || slot.download->GetFormat()!=GSTexture::Format::Color ||
				!TimeReadback(ps2_native_readback_work.map, [&] { return slot.download->Map(rc); })) return Result::Failed;
			// Only the same full Color rc(0,0,width,height) after actual Map may
			// expose pointer/pitch. Map retains the copy-fence and full invalidate.
			u32 copy_offset=0, copy_row_bytes=0, copy_rows=0;
			slot.download->GetTransferSize(rc,&copy_offset,&copy_row_bytes,&copy_rows);
			if (copy_offset || copy_row_bytes!=width*sizeof(u32) || copy_rows!=height) return Result::Failed;
			const u32 source_pitch=slot.download->GetMapPitch();
			const size_t source_span=GSDownloadTexture::GetTransferRegionSize(source_pitch,copy_row_bytes,copy_rows);
			const void* source = slot.download->GetMapPointer();
			u32* target_pixels = pixels;
			size_t target_bytes = bytes;
			if (use_callback)
			{
				// No slot reservation until a real completed copy has passed Map,
				// exact transfer geometry and readable source-span arithmetic.
				// A failed target/copy never requests a second target or falls back.
				size_t checked_source_span = 0;
				const uintptr_t source_address = reinterpret_cast<uintptr_t>(source);
				if (!source || !NativeOpaqueCopy::Span(source_pitch,copy_row_bytes,copy_rows,
					source_span,&checked_source_span) || checked_source_span != source_span ||
					source_span > std::numeric_limits<uintptr_t>::max() - source_address || callback_called)
					return Result::Failed;
				callback_called = true;
				if (prepare)
				{
					NativeReadbackTarget target{};
					if (!prepare(opaque,&target) || !target.pixels || target.bytes != size_t(width)*height*sizeof(u32))
						return Result::Failed;
					target_pixels = target.pixels;
					target_bytes = target.bytes;
				}
			}
			const bool flip = g_gs_device->UsesLowerLeftOrigin();
			const Result output_result = TimeReadback(ps2_native_readback_work.output, [&] {
				if (consume)
					return consume(opaque,
						{source, source_span, source_pitch, slot.guest_frame, flip}, !s_native_bootstrap_needed);
				return NativeOpaqueCopy::Copy(source,source_span,source_pitch,target_pixels,target_bytes,
					width*sizeof(u32),width,height,flip) ? Result::Consumed : Result::Failed;
			});
			if (output_result != Result::Consumed)
				return output_result;
			*completed_serial=slot.serial; *completed_guest_frame=slot.guest_frame;
			s_native_delivered=slot.serial; slot.stage=0; ++s_native_reads;
			s_native_bootstrap_needed=false; return Result::Consumed;
		};
		const u64 newest_serial = newest ? newest->serial : 0;
		if (newest && read_completed(*newest) == Result::Failed)
		{ Error::SetStringView(error,"native async completed map/invalidate"); return -1; }
		// Even when presentation deferred, only the newest completed picture
		// remains eligible. Pending buffers still belong to their copy fences.
		for (NativeReadbackSlot& slot : s_native_slots)
			if (slot.stage==2 && slot.epoch==s_native_epoch && slot.serial<newest_serial)
			{ slot.stage=0; ++s_native_discarded; }
		if (expected_serial > s_native_seen)
		{
			NativeReadbackSlot* free=nullptr;
			for (NativeReadbackSlot& slot : s_native_slots) if (!slot.stage) { free=&slot; break; }
			if (!free) ++s_native_dropped; // Failed capture attempts, including refresh retries; never skip EE/GS work.
			else
			{
				free->download->CopyFromTexture(rc,s_presented,rc,0);
				free->copy_fence = GSDeviceVK::GetInstance()->GetCurrentFenceCounter();
				free->serial=expected_serial; free->guest_frame=guest_frame; free->epoch=s_native_epoch;
				free->stage=1; ++s_native_queued;
				s_native_seen=expected_serial; // Commit only actual enqueue; retry a full-slot drop on refresh.
				// Intentionally no Flush/Execute(false) here. EndPresent already
				// submitted the frame; this copy is recorded in its NEXT command
				// buffer and piggybacks the next normal GS submission, before that
				// same backbuffer can be rendered again (same queue + barriers).
				if (s_native_bootstrap_needed && !*completed_serial)
				{
					// One original synchronous readback per initial/Load epoch.
					// Ensures a usable frame even if emulation pauses immediately.
					free->download->Flush(); free->stage=2;
					++s_native_completed; ++s_native_bootstraps;
					if (read_completed(*free) != Result::Consumed) { Error::SetStringView(error,"native async bootstrap readback"); return -1; }
				}
			}
		}
		return *completed_serial ? 1 : 0;
	}

	int ReadPresentedFrameAsyncInto(u64 expected_serial, u32 guest_frame, u32 width, u32 height,
		u32* pixels, size_t bytes, u64* completed_serial, u32* completed_guest_frame, Error* error)
	{
		// Keep the original fixed-target early guards and semantics in the shared implementation.
		return ReadPresentedFrameAsyncImpl(expected_serial,guest_frame,width,height,pixels,bytes,
			nullptr,nullptr,nullptr,completed_serial,completed_guest_frame,error);
	}

	int ReadPresentedFrameAsyncGenerated(u64 expected_serial, u32 guest_frame, u32 width, u32 height,
		PrepareNativeReadbackTarget prepare, void* opaque, u64* completed_serial,
		u32* completed_guest_frame, Error* error)
	{
		if (!prepare) { Error::SetStringView(error,"native async generated target callback absent"); return -1; }
		return ReadPresentedFrameAsyncImpl(expected_serial,guest_frame,width,height,nullptr,0,
			prepare,nullptr,opaque,completed_serial,completed_guest_frame,error);
	}

	int ReadPresentedFrameAsyncConsumed(u64 expected_serial, u32 guest_frame, u32 width, u32 height,
		ConsumeNativeReadback consume, void* opaque, u64* completed_serial, u32* completed_guest_frame, Error* error)
	{
		if (!consume) { Error::SetStringView(error,"native async pixel consumer absent"); return -1; }
		return ReadPresentedFrameAsyncImpl(expected_serial,guest_frame,width,height,nullptr,0,
			nullptr,consume,opaque,completed_serial,completed_guest_frame,error);
	}

#endif

	bool CaptureFreshFrameToPNG(const std::string& path, u32* width, u32* height, Error* error)
	{
		if (!StringUtil::EndsWithNoCase(path, ".png"))
		{
			Error::SetStringView(error, "the output path must end in .png");
			return false;
		}

		Frame frame;
		if (!CaptureFreshFrame(&frame, error))
			return false;

		const std::string dir(Path::GetDirectory(path));
		if (!dir.empty() && !FileSystem::EnsureDirectoryExists(dir.c_str(), true, error))
			return false;

		// ".part.png" so SaveToFile still sees a png extension.
		const std::string partial = path.substr(0, path.size() - 4) + ".part.png";
		*width = frame.width;
		*height = frame.height;
		RGBA8Image image;
		image.SetPixels(frame.width, frame.height, std::move(frame.pixels));
		if (!image.SaveToFile(partial.c_str()))
		{
			Error::SetStringFmt(error, "could not write '{}'", partial);
			return false;
		}
		if (!FileSystem::RenamePath(partial.c_str(), path.c_str(), error))
		{
			FileSystem::DeleteFilePath(partial.c_str());
			return false;
		}
		return true;
	}
} // namespace GSHeadless
