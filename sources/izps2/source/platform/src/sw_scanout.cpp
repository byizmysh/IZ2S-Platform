#include "sw_scanout.h"
#include "sw_frame_mode.h"
#include "sw_presentation.h"
#include "ps2_app_menu.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "GS/GS.h"
#include "GS/GSPerfMon.h"
#include "GS/Renderers/SW/GSRendererSW.h"
#include "GS/Renderers/Common/GSFunctionMap.h"
static int error(Ps2SwScanout*s,int e){if(s)s->failure=e;return e;}
namespace {
Ps2SwFrameModeHistory history{};
Ps2SwScanout* history_owner=nullptr;
void* history_scratch=nullptr;
size_t history_scratch_bytes=0;
Ps2NativeVideo* history_video=nullptr;
Ps2SwFrameModeInfo last_composed{};
bool last_valid=false;
bool empty_menu_presented=false;
bool same_storage(const Ps2SwScanout*s){
    return s&&s==history_owner&&s->scratch==history_scratch&&
           s->scratch_bytes==history_scratch_bytes&&s->video==history_video;
}
void picture_aspect(uint32_t& num,uint32_t& den){
    num=4;den=3;float custom=0;
    switch(GSConfig.AspectRatio){
    case AspectRatioType::Stretch:num=16;den=9;return;
    case AspectRatioType::R4_3:return;
    case AspectRatioType::R16_9:num=16;den=9;return;
    case AspectRatioType::R10_7:num=10;den=7;return;
    case AspectRatioType::R21_9:num=21;den=9;return;
    case AspectRatioType::R20_9:num=20;den=9;return;
    case AspectRatioType::R19_5_9:num=39;den=18;return;
    case AspectRatioType::Custom:custom=GSConfig.CustomAspectRatio;break;
    default:
        custom=EmuConfig.CurrentCustomAspectRatio;
        if(isa_native::GSRendererSW::GetInstance()->GetVideoMode()==GSVideoMode::SDTV_480P){num=3;den=2;}
        break;
    }
    if(std::isfinite(custom)&&custom>0){
        if(custom<0.5f)custom=0.5f;if(custom>5.f)custom=5.f;
        num=static_cast<uint32_t>(custom*10000.f+0.5f);den=10000;
    }
}
int present_picture(Ps2SwScanout*s,const Ps2SwFrameModeInfo& raw){
    // Readback planes are dead after merge. Reuse their storage for a separate
    // square-pixel 16:9 presentation surface and retain raw GS pixels for pause.
    const size_t offset=(raw.rgba_bytes+31u)&~size_t{31};
    constexpr unsigned width=960,height=540;
    constexpr size_t bytes=static_cast<size_t>(width)*height*4;
    if(offset>s->scratch_bytes || bytes>s->scratch_bytes-offset)
        return error(s,PS2_SCANOUT_STORAGE);
    const Ps2SwMergeImage source{static_cast<const uint8_t*>(raw.rgba),raw.rgba_bytes,raw.pitch,raw.width,raw.height};
    Ps2SwMergeOutput output{static_cast<uint8_t*>(s->scratch)+offset,bytes,width*4,width,height};
    Ps2SwPresentationOptions options{};picture_aspect(options.desired_aspect_num,options.desired_aspect_den);
    options.viewport_aspect_num=16;options.viewport_aspect_den=9;
    if(ps2_sw_presentation_rgba(&source,&output,&options,nullptr))return error(s,PS2_SCANOUT_UNSUPPORTED);
    Ps2AppMenuView menu{};ps2_app_menu_snapshot(&menu);
    if(ps2_app_menu_render(&output,&menu))return error(s,PS2_SCANOUT_STORAGE);
    const int e=ps2_native_video_present(s->video,output.pixels,output.bytes,width,height,output.pitch,raw.frame);
    if(e)return error(s,e);
    empty_menu_presented=false;
    s->display_index=2;s->width=width;s->height=height;s->gs_frame=raw.frame;
    auto*r=isa_native::GSRendererSW::GetInstance();
    s->draws=r->s_n;s->gs_jit_bytes=GSCodeReserve::GetMemoryUsed();s->failure=0;return 0;
}
}
int ps2_sw_scanout_repeat(Ps2SwScanout*s){
    if(same_storage(s)&&last_valid&&s->video&&s->scratch&&last_composed.rgba==s->scratch)
        return present_picture(s,last_composed);
    Ps2AppMenuView menu{};ps2_app_menu_snapshot(&menu);
    // A game can disable PCRTC displays during a movie transition. The paused
    // application menu remains usable on an explicit opaque black background;
    // these pixels are UI only, never retained or reported as a guest picture.
    if(!menu.visible&&!(same_storage(s)&&empty_menu_presented))return error(s,PS2_SCANOUT_NO_DISPLAY);
    constexpr unsigned width=960,height=540;
    constexpr size_t bytes=static_cast<size_t>(width)*height*4;
    if(!s||!s->video||!s->scratch||((uintptr_t)s->scratch&31)||s->scratch_bytes<bytes)
        return error(s,PS2_SCANOUT_STORAGE);
    const uintptr_t scratch=(uintptr_t)s->scratch,mapped=(uintptr_t)s->video->mapped;
    if(s->scratch_bytes>UINTPTR_MAX-scratch||s->video->mapped_bytes>UINTPTR_MAX-mapped||
       (scratch<mapped+s->video->mapped_bytes&&mapped<scratch+s->scratch_bytes))
        return error(s,PS2_SCANOUT_STORAGE);
    if(!same_storage(s)){
        history={};history_owner=s;history_scratch=s->scratch;
        history_scratch_bytes=s->scratch_bytes;history_video=s->video;
        last_valid=false;empty_menu_presented=false;
    }
    auto*pixels=static_cast<uint8_t*>(s->scratch);
    std::memset(pixels,0,bytes);
    for(size_t i=3;i<bytes;i+=4)pixels[i]=255;
    Ps2SwMergeOutput output{pixels,bytes,width*4,width,height};
    if(ps2_app_menu_render(&output,&menu))return error(s,PS2_SCANOUT_STORAGE);
    const uint32_t frame=static_cast<uint32_t>(g_perfmon.GetFrame());
    const int e=ps2_native_video_present(s->video,output.pixels,output.bytes,width,height,output.pitch,frame);
    if(e)return error(s,e);
    empty_menu_presented=menu.visible!=0;
    s->display_index=2;s->width=width;s->height=height;s->gs_frame=frame;s->failure=0;
    return 0;
}
void ps2_sw_scanout_reset_history(void){
    // Retain only the UI cleanup receipt: hiding a menu drawn without a game
    // picture must still clear that overlay once after a load/history reset.
    history={};last_valid=false;
}
int ps2_sw_scanout_present_field(Ps2SwScanout*s,unsigned circuit,unsigned field){
    if(!s||!s->video||!s->scratch||((uintptr_t)s->scratch&31)||circuit>2)
        return error(s,PS2_SCANOUT_STORAGE);
    if(GSGetCurrentRenderer()!=GSRendererType::SW||!g_gs_renderer||GSConfig.SWExtraThreads||GSConfig.BackThread)
        return error(s,PS2_SCANOUT_RENDERER);
    auto*r=isa_native::GSRendererSW::GetInstance();r->Ps2SynchronizeReadback();
    if(circuit==2){
        // MTGS has one serial software renderer. Keep only mode-observation
        // history here, preserving the existing public scanout/config ABI.
        const auto frame=static_cast<uint32_t>(g_perfmon.GetFrame());
        if(!same_storage(s) || (history.valid && frame<history.frame)){
            if(!same_storage(s))empty_menu_presented=false;
            history={};history_owner=s;history_scratch=s->scratch;
            history_scratch_bytes=s->scratch_bytes;history_video=s->video;last_valid=false;
        }
        const uintptr_t scratch=(uintptr_t)s->scratch,mapped=(uintptr_t)s->video->mapped;
        if(s->scratch_bytes>UINTPTR_MAX-scratch || s->video->mapped_bytes>UINTPTR_MAX-mapped ||
           (scratch<mapped+s->video->mapped_bytes && mapped<scratch+s->scratch_bytes))
            return error(s,PS2_SCANOUT_STORAGE);
        Ps2SwFrameModeInfo composed{};
        last_valid=false;
        const int result=field<=1?ps2_sw_frame_mode_compose_field(&history,s->scratch,s->scratch_bytes,field,&composed):
            ps2_sw_frame_mode_compose(&history,s->scratch,s->scratch_bytes,&composed);
        if(result==PS2_FRAME_MODE_HISTORY)return error(s,PS2_SCANOUT_NO_DISPLAY);
        if(result){
            if(!r->PCRTCDisplays.PCRTCDisplays[0].enabled && !r->PCRTCDisplays.PCRTCDisplays[1].enabled)
                return error(s,PS2_SCANOUT_NO_DISPLAY);
            std::fprintf(stderr,"PS2_SW_SCANOUT_REJECT result=%d reason=%s frame=%u pmode=%llx smode1=%llx smode2=%llx syncv=%llx dispfb0=%llx dispfb1=%llx display0=%llx display1=%llx actualInterlaced=%u videoMode=%d scratchRequired=%zu scratchAvailable=%zu\n",
                result,composed.reason?composed.reason:"unspecified",composed.frame,
                (unsigned long long)composed.pmode,(unsigned long long)composed.smode1,
                (unsigned long long)composed.smode2,(unsigned long long)composed.syncv,
                (unsigned long long)composed.circuit[0].dispfb,(unsigned long long)composed.circuit[1].dispfb,
                (unsigned long long)composed.circuit[0].display,(unsigned long long)composed.circuit[1].display,
                r->isReallyInterlaced()?1u:0u,static_cast<int>(r->GetVideoMode()),
                composed.required_scratch_bytes,s->scratch_bytes);
            return error(s,result==PS2_FRAME_MODE_STORAGE?PS2_SCANOUT_STORAGE:PS2_SCANOUT_UNSUPPORTED);
        }
        last_composed=composed;last_valid=true;
        return present_picture(s,composed);
    }
    // Diagnostic readback reuses scratch and destroys the retained raw image.
    // A later pause refresh must wait for a fresh successful composition.
    last_valid=false;
    unsigned first=circuit,last=circuit+1;
    for(unsigned i=first;i<last;i++){
        const auto&d=r->PCRTCDisplays.PCRTCDisplays[i];
        if(!d.enabled)continue;
        const auto size=r->PCRTCDisplays.GetFramebufferSize(i);int w=d.FBW*64,h=size.y;
        if(w<=0||h<=0||w>1024||h>1024||d.PSM>=64)
            return error(s,PS2_SCANOUT_UNSUPPORTED);
        const auto&psm=GSLocalMemory::m_psm[d.PSM];
        const GSVector4i wanted(d.DBX,d.DBY,d.DBX+w,d.DBY+h);
        const GSVector4i rect=wanted.ralign<Align_Outside>(psm.bs);
        if(rect.left<0||rect.top<0||rect.right>2048||rect.bottom>2048||rect.width()>1024)
            return error(s,PS2_SCANOUT_UNSUPPORTED);
        const size_t needed=static_cast<size_t>(1024)*rect.height()*4;
        const uintptr_t scratch=(uintptr_t)s->scratch,mapped=(uintptr_t)s->video->mapped;
        if(s->scratch_bytes<needed||needed>UINTPTR_MAX-scratch||
           s->video->mapped_bytes>UINTPTR_MAX-mapped||
           (scratch<mapped+s->video->mapped_bytes&&mapped<scratch+needed))
            return error(s,PS2_SCANOUT_STORAGE);
        GIFRegTEXA texa{};texa.TA0=(d.PSM==PSMCT24||d.PSM==PSGPU24)?0x80:0;texa.TA1=0x80;
        psm.rtx(r->m_mem,r->m_mem.GetOffset(d.Block(),d.FBW,d.PSM),rect,(u8*)s->scratch,1024*4,texa);
        const size_t offset=(static_cast<size_t>(wanted.top-rect.top)*1024+wanted.left-rect.left)*4;
        const int e=ps2_native_video_present(s->video,(u8*)s->scratch+offset,needed-offset,w,h,1024*4,g_perfmon.GetFrame());
        if(e)return error(s,e);
        empty_menu_presented=false;
        s->display_index=i;s->width=w;s->height=h;s->gs_frame=g_perfmon.GetFrame();
        s->draws=r->s_n;s->gs_jit_bytes=GSCodeReserve::GetMemoryUsed();s->failure=0;return 0;
    }
    return error(s,PS2_SCANOUT_NO_DISPLAY);
}
int ps2_sw_scanout_present(Ps2SwScanout*s,unsigned circuit){
    return ps2_sw_scanout_present_field(s,circuit,2);
}
