#include "sw_frame_mode.h"
#include "sw_deinterlace.h"
#include "sw_temporal_storage.h"
#include "GS/GS.h"
#include "GS/GSPerfMon.h"
#include "GS/Renderers/SW/GSRendererSW.h"
#include "GS/Renderers/Common/GSInterlaceModePolicy.h"
#include <cstring>
#include <cstdio>

namespace {
int failure(Ps2SwFrameModeInfo *info,int code,const char *reason)
{
    info->reason=reason;return code;
}
Ps2SwMergeRect rectangle(const GSVector4i& r)
{
    return {r.left,r.top,r.right,r.bottom};
}
size_t align32(size_t bytes) { return (bytes+31u)&~size_t{31}; }
// MTGS serializes composition; the two history banks are acquired only for
// temporal fields, after the runtime's cached allocator is ready.
Ps2SwDeinterlaceState temporal_state{};
const Ps2SwFrameModeHistory* temporal_owner=nullptr;
void* temporal_scratch=nullptr;
size_t temporal_scratch_bytes=0;
uint64_t temporal_key[8]{};
bool temporal_valid=false;
bool overlaps(uintptr_t a,size_t an,uintptr_t b,size_t bn)
{
    return a<b+bn && b<a+an;
}
}

int ps2_sw_frame_mode_compose_field(Ps2SwFrameModeHistory *history,void *scratch,
                                   size_t scratch_bytes,unsigned field,
                                   Ps2SwFrameModeInfo *info)
{
    if(!info)return PS2_FRAME_MODE_ARGUMENT;
    *info={};info->interlace_shader=-2;
    if(!history)return failure(info,PS2_FRAME_MODE_ARGUMENT,"missing-history");
    if(GSGetCurrentRenderer()!=GSRendererType::SW || !g_gs_renderer ||
       GSConfig.SWExtraThreads || GSConfig.BackThread)
        return failure(info,PS2_FRAME_MODE_RENDERER,"software-mtgs-renderer-required");
    auto* renderer=isa_native::GSRendererSW::GetInstance();
    renderer->Ps2SynchronizeReadback();
    GSPrivRegSet regs;
    std::memcpy(&regs,renderer->GetRegsMem(),sizeof(regs));
    const auto original=renderer->PCRTCDisplays;
    info->frame=g_perfmon.GetFrame();info->pmode=regs.PMODE.U64;
    info->smode1=regs.SMODE1.U64;info->smode2=regs.SMODE2.U64;
    info->syncv=regs.SYNCV.U64;info->bgcolor=regs.BGCOLOR.U64;
    info->interlaced=regs.SMODE2.INT;info->anti_blur=GSConfig.PCRTCAntiBlur;
    const bool owner_reset=!history->valid;
    bool stable=history->valid && info->frame==history->frame+1u &&
                history->smode2==regs.SMODE2.U64;
    for(unsigned i=0;i<2;i++) {
        stable=stable && history->display[i]==regs.DISP[i].DISPLAY.U64 &&
               (history->dispfb[i]&~uint64_t{0x1ff})==(regs.DISP[i].DISPFB.U64&~uint64_t{0x1ff});
        history->dispfb[i]=regs.DISP[i].DISPFB.U64;
        history->display[i]=regs.DISP[i].DISPLAY.U64;
        info->circuit[i].dispfb=regs.DISP[i].DISPFB.U64;
        info->circuit[i].display=regs.DISP[i].DISPLAY.U64;
    }
    history->valid=1;history->frame=info->frame;history->smode2=regs.SMODE2.U64;
    if(regs.EXTWRITE.WRITE)return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"feedback-extwrite");
    if(renderer->m_scanmask_used || renderer->m_env.SCANMSK.MSK)
        return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"scanmask-or-scanmask-history");
    if(GSConfig.InterlaceMode!=GSInterlaceMode::Automatic || GSConfig.MovieCombDeinterlace)
        return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"explicit-or-movie-interlace-policy");
    const bool game_deinterlacing=
        (original.PCRTCDisplays[0].prevFramebufferOffsets.y!=original.PCRTCDisplays[0].framebufferOffsets.y)!=
        (original.PCRTCDisplays[1].prevFramebufferOffsets.y!=original.PCRTCDisplays[1].framebufferOffsets.y);
    const auto selection=SelectGSInterlaceMode(0,true,game_deinterlacing,regs.SMODE2.FFMD,false,false,false);
    info->interlace_shader=selection.shader_mode;
    const bool temporal=regs.SMODE2.INT && regs.SMODE2.FFMD && renderer->isReallyInterlaced();
    if(!temporal)temporal_valid=false;
    if(regs.SMODE2.INT) {
        if(!renderer->isReallyInterlaced())
            return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"unknown-interlaced-timing");
        if(temporal) {
            if(field>1)return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"actual-gsvsync-field-required");
            if(selection.shader_mode!=3 || selection.present_field_direct)
                return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"unsupported-field-policy");
        } else {
            if(!stable)return failure(info,PS2_FRAME_MODE_HISTORY,"consecutive-stable-frame-required");
            if(selection.shader_mode!=-1 || selection.present_field_direct)
                return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"temporal-deinterlace-required");
        }
    } else if(renderer->isReallyInterlaced()) {
        return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"progressive-field-toggling");
    }

    // Retain raw history but rebuild all current rectangles from actual regs.
    // GetOutput may already have subtracted the block-aligned origin in original.
    auto fresh=original;
    fresh.SetVideoMode(renderer->GetVideoMode());
    if(fresh.videomode<0 || fresh.videomode>5)
        return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"unknown-video-mode");
    fresh.EnableDisplays(regs.PMODE,regs.SMODE2,renderer->isReallyInterlaced());
    for(unsigned i=0;i<2;i++) {
        fresh.SetRects(i,regs.DISP[i].DISPLAY,regs.DISP[i].DISPFB);
        fresh.PCRTCDisplays[i].prevFramebufferReg=original.PCRTCDisplays[i].prevFramebufferReg;
        fresh.PCRTCDisplays[i].prevFramebufferOffsets=original.PCRTCDisplays[i].prevFramebufferOffsets;
        fresh.PCRTCDisplays[i].prevDisplayOffset=original.PCRTCDisplays[i].prevDisplayOffset;
    }
    fresh.CheckSameSource();fresh.CalculateDisplayOffset(false);
    fresh.CalculateFramebufferOffset(false,regs.DISP[0].DISPFB,regs.DISP[1].DISPFB);
    if(fresh.FrameWrap())return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"wrapped-framebuffer");
    const auto size=fresh.GetResolution();
    if(size.x<=0 || size.y<=0 || size.x>1024 || size.y>1024)
        return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"output-extent");
    info->width=size.x;info->height=size.y;info->pitch=size.x*4u;
    info->rgba_bytes=info->pitch*info->height;
    struct Plane { GSVector4i rect;unsigned circuit;size_t offset,bytes; } planes[2];
    unsigned plane_count=0,plane_for[2]={0,0};
    Ps2SwMergeParameters parameters{};
    parameters.background_rgb=regs.BGCOLOR.U32[0]&0xffffffu;
    parameters.mmod=regs.PMODE.MMOD;parameters.alp=regs.PMODE.ALP;parameters.slbg=regs.PMODE.SLBG;
    for(unsigned i=0;i<2;i++) {
        const auto& d=fresh.PCRTCDisplays[i];auto& metadata=info->circuit[i];
        metadata.enabled=d.enabled;metadata.block=d.Block();metadata.fbw=d.FBW;metadata.psm=d.PSM;
        metadata.source=rectangle(d.framebufferRect);metadata.destination=rectangle(d.displayRect);
        if(!d.enabled)continue;
        if(d.FBW<=0 || (d.PSM!=PSMCT32 && d.PSM!=PSMCT24 && d.PSM!=PSMCT16 && d.PSM!=PSMCT16S) ||
           d.framebufferRect.rempty() || d.displayRect.rempty())
            return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"source-format-or-extent");
        unsigned group=plane_count;
        for(unsigned j=0;j<plane_count;j++) {
            const auto& first=fresh.PCRTCDisplays[planes[j].circuit];
            if(first.FBP==d.FBP && first.FBW==d.FBW && first.PSM==d.PSM) {group=j;break;}
        }
        plane_for[i]=group;
        if(group==plane_count) {
            planes[group].rect=d.framebufferRect;planes[group].circuit=i;plane_count++;
        } else planes[group].rect=planes[group].rect.runion(d.framebufferRect);
    }
    size_t needed=align32(info->rgba_bytes);
    for(unsigned group=0;group<plane_count;group++) {
        auto& plane=planes[group];const auto& d=fresh.PCRTCDisplays[plane.circuit];
        plane.rect=plane.rect.ralign<Align_Outside>(GSLocalMemory::m_psm[d.PSM].bs);
        if(plane.rect.left<0 || plane.rect.top<0 || plane.rect.right>2048 || plane.rect.bottom>2048 ||
           plane.rect.width()>1024 || plane.rect.height()>1024)
            return failure(info,PS2_FRAME_MODE_UNSUPPORTED,"aligned-read-extent");
        plane.offset=needed;plane.bytes=size_t{4096}*plane.rect.height();needed+=plane.bytes;
    }
    info->required_scratch_bytes=needed;info->source_reads=plane_count;
    const uintptr_t storage=reinterpret_cast<uintptr_t>(scratch);
    if(!scratch || (storage&31u) || scratch_bytes<needed || scratch_bytes>UINTPTR_MAX-storage)
        return failure(info,PS2_FRAME_MODE_STORAGE,"scratch-storage");
    uint8_t *temporal_pixels=temporal?ps2_sw_temporal_acquire():nullptr;
    if(temporal && !temporal_pixels)
        return failure(info,PS2_FRAME_MODE_STORAGE,"temporal-history-allocation");
    if(temporal_pixels && overlaps(storage,scratch_bytes,reinterpret_cast<uintptr_t>(temporal_pixels),PS2_SW_FRAME_MODE_TEMPORAL_BYTES))
        return failure(info,PS2_FRAME_MODE_STORAGE,"scratch-history-overlap");
    auto* bytes=static_cast<u8*>(scratch);
    for(unsigned group=0;group<plane_count;group++) {
        const auto& plane=planes[group];const auto& d=fresh.PCRTCDisplays[plane.circuit];
        GIFRegTEXA texa{};texa.TA0=d.PSM==PSMCT24?0x80:0;texa.TA1=0x80;
        GSLocalMemory::m_psm[d.PSM].rtx(renderer->m_mem,renderer->m_mem.GetOffset(d.Block(),d.FBW,d.PSM),
                                      plane.rect,bytes+plane.offset,4096,texa);
    }
    uint32_t pad_first[2]{},pad_end[2]{};
    const int shift=temporal && !GSConfig.DisableInterlaceOffset ? static_cast<int>(field^selection.field_offset) : 0;
    for(unsigned i=0;i<2;i++) {
        const auto& d=fresh.PCRTCDisplays[i];if(!d.enabled)continue;
        const auto& plane=planes[plane_for[i]];auto& layer=parameters.circuit[i];layer.enabled=1;
        layer.image={bytes+plane.offset,plane.bytes,4096,static_cast<u32>(plane.rect.width()),
                     static_cast<u32>(plane.rect.height())};
        layer.source=rectangle(d.framebufferRect-GSVector4i(plane.rect.left,plane.rect.top,plane.rect.left,plane.rect.top));
        layer.destination=rectangle(d.displayRect);
        const int top=layer.destination.top;
        layer.destination.top+=shift;layer.destination.bottom+=shift;
        const auto clamp_row=[&](int row){return static_cast<uint32_t>(std::clamp(row,0,static_cast<int>(info->height)));};
        pad_first[i]=clamp_row(top);pad_end[i]=clamp_row(top+shift);
        info->circuit[i].destination=layer.destination;
    }
    const Ps2SwMergeOutput output{bytes,info->rgba_bytes,info->pitch,info->width,info->height};
    info->merge_result=ps2_sw_merge_rgba(&output,&parameters);
    if(info->merge_result)return failure(info,PS2_FRAME_MODE_MERGE,"spatial-merge-failed");
    if(temporal) {
        // A framebuffer flip/source offset is content, not a new video mode.
        // Dimensions and temporal continuity are additionally checked by the kernel.
        const uint64_t key[8]={regs.SMODE2.U64,static_cast<uint64_t>(fresh.videomode),
            regs.DISP[0].DISPLAY.U64,regs.DISP[1].DISPLAY.U64,
            static_cast<uint64_t>(fresh.PCRTCDisplays[0].FBW)|(static_cast<uint64_t>(fresh.PCRTCDisplays[0].PSM)<<32),
            static_cast<uint64_t>(fresh.PCRTCDisplays[1].FBW)|(static_cast<uint64_t>(fresh.PCRTCDisplays[1].PSM)<<32),
            static_cast<uint64_t>(regs.PMODE.EN1)|(static_cast<uint64_t>(regs.PMODE.EN2)<<1),
            GSConfig.DisableInterlaceOffset?1u:0u};
        if(!temporal_valid || owner_reset || temporal_owner!=history || temporal_scratch!=scratch ||
           temporal_scratch_bytes!=scratch_bytes || std::memcmp(key,temporal_key,sizeof(key))) {
            if(ps2_sw_deinterlace_reset(&temporal_state))
                return failure(info,PS2_FRAME_MODE_MERGE,"temporal-reset-failed");
            temporal_owner=history;temporal_scratch=scratch;temporal_scratch_bytes=scratch_bytes;
            std::memcpy(temporal_key,key,sizeof(key));temporal_valid=true;
        }
        const auto& rc0=fresh.PCRTCDisplays[0];const auto& rc1=fresh.PCRTCDisplays[1];
        const bool use_rc0=rc0.enabled && (!(regs.PMODE.MMOD==1 && regs.PMODE.ALP==0) || regs.PMODE.AMOD==0);
        const bool rc0_overwrites_rc1=use_rc0 && rc0.displayRect.rcontains(rc1.displayRect) &&
            regs.PMODE.MMOD==1 && regs.PMODE.ALP==255;
        const bool use_rc1=rc1.enabled && ((regs.PMODE.SLBG==0 && !rc0_overwrites_rc1) || regs.PMODE.AMOD==1);
        const unsigned selected=(fresh.FrameRectMatch() || use_rc1)?1:0;
        const Ps2SwMergeImage merged{bytes,info->rgba_bytes,info->pitch,info->width,info->height};
        const Ps2SwDeinterlaceParameters deinterlace{info->frame,field,pad_first[selected],pad_end[selected],0};
        Ps2SwDeinterlaceInfo details{};
        if(ps2_sw_deinterlace_fastmad(&temporal_state,&merged,&output,&deinterlace,
                                     temporal_pixels,PS2_SW_FRAME_MODE_TEMPORAL_BYTES,&details)) {
            temporal_valid=false;
            return failure(info,PS2_FRAME_MODE_MERGE,"temporal-reconstruction-failed");
        }
        if(details.history_reset)
            std::fprintf(stderr,"PS2_SW_DEINTERLACE mode=fastmad sequence=%u field=%u width=%u height=%u historyBytes=%zu index=%u validFields=%u\n",
                info->frame,field,info->width,info->height,details.required_history_bytes,details.index,details.valid_fields);
    }
    info->rgba=bytes;info->reason=temporal?"automatic-field-fastmad":
        regs.SMODE2.INT?"stable-full-frame-pass-through":"progressive";
    return PS2_FRAME_MODE_OK;
}

int ps2_sw_frame_mode_compose(Ps2SwFrameModeHistory *history,void *scratch,
                             size_t scratch_bytes,Ps2SwFrameModeInfo *info)
{
    return ps2_sw_frame_mode_compose_field(history,scratch,scratch_bytes,2,info);
}
