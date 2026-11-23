#include "unwind_registry.h"
#include <string.h>
static uint32_t u32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t u64(const unsigned char *p){uint64_t v;memcpy(&v,p,8);return v;}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn){return a<=b?b-a<an:a-b<bn;}
static int skip_leb(const unsigned char **p,const unsigned char *end){
    for(unsigned n=0;n<10 && *p<end;n++)if(!(*(*p)++&128))return 1;
    return 0;
}
static int skip_pointer(const unsigned char **p,const unsigned char *end,unsigned encoding){
    unsigned kind=encoding&15;size_t bytes=kind==0?8:(kind==3||kind==11)?4:(kind==4||kind==12)?8:0;
    if(!bytes || (size_t)(end-*p)<bytes)return 0;*p+=bytes;return 1;
}
static int cie_encoding(const unsigned char *cie,const unsigned char *end,unsigned *encoding){
    const unsigned char *p=cie+8;if(p>=end)return 0;unsigned version=*p++;
    if(version!=1 && version!=3)return 0;
    const unsigned char *aug=p;while(p<end && *p)p++;if(p==end || p-aug>16)return 0;
    size_t augmentation=(size_t)(p-aug);p++;
    if(!skip_leb(&p,end)||!skip_leb(&p,end))return 0;
    if(version==1){if(p==end)return 0;p++;}else if(!skip_leb(&p,end))return 0;
    if(!augmentation || aug[0]!='z' || p>=end)return 0;
    // Augmentation data is small in the supported ABI; bound its ULEB length.
    size_t length=0;unsigned shift=0;
    for(;;){if(p==end || shift>=sizeof(size_t)*8)return 0;unsigned b=*p++;if((size_t)(b&127)>((SIZE_MAX-length)>>shift))return 0;length|=(size_t)(b&127)<<shift;if(!(b&128))break;shift+=7;}
    if(length>(size_t)(end-p))return 0;const unsigned char *limit=p+length;*encoding=0;
    for(size_t i=1;i<augmentation;i++){
        switch(aug[i]){
        case 'R':if(p==limit)return 0;*encoding=*p++;break;
        case 'L':if(p==limit)return 0;p++;break;
        case 'P':if(p==limit)return 0;{unsigned e=*p++;if(!skip_pointer(&p,limit,e))return 0;}break;
        case 'S':break;
        default:return 0;
        }
    }
    return p==limit && (*encoding==0x1b || *encoding==0x1c);
}
static int frame_valid(const unsigned char *table,size_t bytes,uintptr_t text,size_t text_bytes,const Ps2UnwindFrame *f){
    uintptr_t begin=(uintptr_t)table,address=(uintptr_t)f->fde;
    if(address<begin || address-begin>bytes || bytes-(address-begin)<16)return 0;
    const unsigned char *p=f->fde;uint32_t length=u32(p),back=u32(p+4);
    if(length<12 || length==UINT32_MAX || length>bytes-(address-begin)-4 || !back || back>(address-begin)+4)return 0;
    const unsigned char *cie=p+4-back;size_t offset=(size_t)(cie-table);
    if(bytes-offset<8 || u32(cie+4)!=0)return 0;uint32_t cie_bytes=u32(cie);
    if(cie_bytes<8 || cie_bytes==UINT32_MAX || cie_bytes>bytes-offset-4 || cie+4+cie_bytes>p)return 0;
    unsigned encoding;if(!cie_encoding(cie,cie+4+cie_bytes,&encoding))return 0;
    unsigned width=encoding==0x1c?8:4;
    if(length<5+2*width)return 0;
    int64_t delta=width==8?(int64_t)u64(p+8):(int32_t)u32(p+8);
    int64_t span=width==8?(int64_t)u64(p+16):(int32_t)u32(p+12);
    const unsigned char *augmentation=p+8+2*width,*fde_end=p+4+length;
    size_t augmentation_bytes=0;unsigned shift=0;
    for(;;){if(augmentation==fde_end||shift>=sizeof(size_t)*8)return 0;unsigned b=*augmentation++;
        if((size_t)(b&127)>((SIZE_MAX-augmentation_bytes)>>shift))return 0;augmentation_bytes|=(size_t)(b&127)<<shift;if(!(b&128))break;shift+=7;}
    if(augmentation_bytes>(size_t)(fde_end-augmentation))return 0;
    uintptr_t origin=address+8;
    uintptr_t magnitude=delta<0?(uintptr_t)(-(delta+1))+1:(uintptr_t)delta;
    if((delta<0 && origin<magnitude) || (delta>=0 && magnitude>UINTPTR_MAX-origin))return 0;
    uintptr_t pc=delta<0?origin-magnitude:origin+magnitude;
    if(span<4 || (pc&3) || (span&3) || pc!=f->pc || (size_t)span!=f->bytes || pc<text || pc-text>=text_bytes || f->bytes>text_bytes-(pc-text))return 0;
    return 1;
}
uint32_t ps2_unwind_unregister(Ps2UnwindRegistry *r){
    if(!r || !r->ready)return PS2_UNWIND_INVALID;
    if(r->poisoned)return PS2_UNWIND_RETAINED;
    for(size_t i=r->count;i>0;i--){size_t n=i-1;if(!r->registered[n])continue;
        r->api.deregister_frame(r->frames[n].fde);r->deregistrations++;
        Ps2DwarfBases bases={0};r->lookups++;
        if(r->api.find_fde((void*)(r->frames[n].pc+1),&bases)==r->frames[n].fde){r->poisoned=1;r->last_error=PS2_UNWIND_RETAINED;return PS2_UNWIND_RETAINED;}
        r->registered[n]=0;r->live--;
    }
    r->ready=0;return 0;
}
uint32_t ps2_unwind_register(Ps2UnwindRegistry *r,const Ps2UnwindApi *api,const void *table,size_t bytes,
                           uintptr_t text,size_t text_bytes,const Ps2UnwindFrame *frames,size_t count,unsigned char *state){
    if(!r || !api || !api->register_frame || !api->deregister_frame || !api->find_fde || !table || bytes<16 || !text || !text_bytes ||
       bytes>UINTPTR_MAX-(uintptr_t)table || text_bytes>UINTPTR_MAX-text || !frames || !count || count>65536 || !state)return PS2_UNWIND_INVALID;
    if(r->ready || r->live || r->poisoned)return PS2_UNWIND_BUSY;
    size_t frame_bytes=count*sizeof(*frames);
    if(frame_bytes>UINTPTR_MAX-(uintptr_t)frames || count>UINTPTR_MAX-(uintptr_t)state ||
       overlap((uintptr_t)r,sizeof(*r),(uintptr_t)table,bytes) || overlap((uintptr_t)r,sizeof(*r),text,text_bytes) ||
       overlap((uintptr_t)r,sizeof(*r),(uintptr_t)frames,frame_bytes) || overlap((uintptr_t)r,sizeof(*r),(uintptr_t)state,count) ||
       overlap((uintptr_t)state,count,(uintptr_t)table,bytes) || overlap((uintptr_t)state,count,text,text_bytes) ||
       overlap((uintptr_t)state,count,(uintptr_t)frames,frame_bytes))return PS2_UNWIND_INVALID;
    for(size_t i=0;i<count;i++){
        if(state[i] || !frame_valid(table,bytes,text,text_bytes,&frames[i]))return PS2_UNWIND_INVALID;
        if(i && (frames[i].pc<frames[i-1].pc || frames[i].pc-frames[i-1].pc<frames[i-1].bytes))return PS2_UNWIND_INVALID;
    }
    Ps2UnwindApi saved=*api;memset(r,0,sizeof(*r));r->api=saved;r->frames=frames;r->registered=state;r->count=count;r->ready=1;
    for(size_t i=0;i<count;i++){
        state[i]=1;r->live++;r->registrations++;r->api.register_frame(frames[i].fde);
        Ps2DwarfBases bases={0};r->lookups++;
        if(r->api.find_fde((void*)(frames[i].pc+1),&bases)!=frames[i].fde){
            r->last_error=PS2_UNWIND_LOOKUP;uint32_t cleanup=ps2_unwind_unregister(r);return cleanup?cleanup:PS2_UNWIND_LOOKUP;
        }
    }
    return 0;
}
