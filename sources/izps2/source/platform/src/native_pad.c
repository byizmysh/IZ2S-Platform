#include "native_pad.h"
#ifndef PS2_PAD_PORTABLE
#include "native_pad_sdk_fingerprints.h"
#endif
#include <string.h>
_Static_assert(sizeof(Ps2NativeNpadState)==64,"original SDK Npad state ABI");
_Static_assert(offsetof(Ps2NativeNpadState,attributes)==32,"Npad attributes ABI");
static int lock(Ps2NativePad *p){uint32_t zero=0;return p&&__atomic_compare_exchange_n(&p->busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
static uint32_t done(Ps2NativePad *p,uint32_t result){p->last_error=result;__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return result;}
static Ps2NativePadFrame neutral(void){Ps2NativePadFrame f={0};f.style=f.npad_id=PS2_NPAD_NONE;return f;}
static uint32_t bind(Ps2NativePad *p,const Ps2NativePadApi *api,uint32_t verified,
                     Ps2NativePadReader reader,const void *context){
    if(!p||(!api&&!reader))return PS2_PAD_INVALID;
    if(!lock(p))return PS2_PAD_BUSY;
    if(p->ready)return done(p,PS2_PAD_BUSY);
    if(api)for(unsigned i=0;i<5;i++)if(!api->get[i])return done(p,PS2_PAD_INVALID);
    p->api=api?*api:(Ps2NativePadApi){0};
    p->reader=reader;p->reader_context=context;
    p->frame=neutral();p->polls=p->reads=p->switches=p->disconnects=0;p->max_reads_per_poll=0;p->sdk_verified=verified;p->ready=1;return done(p,0);
}
uint32_t ps2_native_pad_bind_callbacks(Ps2NativePad *p,const Ps2NativePadApi *api){return bind(p,api,0,NULL,NULL);}
uint32_t ps2_native_pad_bind_reader(Ps2NativePad *p,Ps2NativePadReader reader,const void *context){
    return bind(p,NULL,0,reader,context);
}
#ifndef PS2_PAD_PORTABLE
uint32_t ps2_native_pad_bind_sdk(Ps2NativePad *p,uintptr_t sdk,size_t bytes){
    if(!sdk||(sdk&4095)||bytes!=6164480||sdk>((uint64_t)1<<39)-bytes)return PS2_PAD_INVALID;
    Ps2NativePadApi api={0};
    for(unsigned i=0;i<5;i++){
        if(memcmp((void *)(sdk+ps2_npad_offsets[i]),ps2_npad_bytes[i],104))return PS2_PAD_UNSUPPORTED;
        api.get[i]=(Ps2NativeNpadGet)(sdk+ps2_npad_offsets[i]);
    }
    return bind(p,&api,1,NULL,NULL);
}
#endif
static int32_t clamp(int32_t n){return n<-32767?-32767:n>32767?32767:n;}
static Ps2NativePadFrame map(const Ps2NativeNpadState *s,uint32_t id,uint32_t style){
    /* Nintendo physical A/right -> Circle, B/bottom -> Cross, X/top ->
     * Triangle, Y/left -> Square. Minus is reserved for the emulator menu.
     * Plus -> Start. Select has no dedicated physical binding. */
    static const uint8_t bits[16]={13,14,15,12,2,0,1,3,255,10,6,8,7,9,4,5};
    Ps2NativePadFrame f={.sample=s->sample,.lx=clamp(s->lx),.ly=-clamp(s->ly),.rx=clamp(s->rx),.ry=-clamp(s->ry),.connected=1,.npad_id=id,.style=style};
    for(unsigned i=0;i<16;i++)if(bits[i]<32 && (s->buttons&(1u<<bits[i])))f.buttons|=1u<<i;
    // The frontend forwards only bits 0..15 to DualShock2. Bit 16 belongs to
    // the application controller and never reaches the emulated game.
    if(s->buttons&(1u<<11))f.buttons|=1u<<16;
    // Contradictory directional buttons neutralize each other. Sticks remain
    // independent and never synthesize a D-pad or mode-toggle button.
    if((f.buttons&5)==5)f.buttons&=~5u;if((f.buttons&10)==10)f.buttons&=~10u;
    return f;
}
enum { PS2_JOYCON_MENU_LATCH = 1u << 17 };

static int normalize_joycon(Ps2NativeNpadState *sample, uint32_t style, uint32_t id,
                            const Ps2NativePadFrame *previous) {
    const int left = style == PS2_NPAD_LEFT;
    const uint32_t raw = sample->buttons;
    static const uint32_t left_face[] = {0x8000, 0x1000, 0x4000, 0x2000};
    static const uint32_t right_face[] = {4, 1, 8, 2};
    const uint32_t *face = left ? left_face : right_face;
    uint32_t buttons = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (raw & face[i]) buttons |= 1u << i;

    const uint32_t sl = 1u << (left ? 16 : 18);
    const uint32_t sr = 1u << (left ? 17 : 19);
    const uint32_t start = left ? 0x800 : 0x400;
    const int gesture = (raw & (sl | sr | start)) == (sl | sr | start);
    const int latched = gesture || (previous->connected && previous->npad_id == id &&
        previous->style == style && (previous->buttons & PS2_JOYCON_MENU_LATCH) &&
        (raw & (sl | sr | start)));
    if (!latched) {
        if (raw & sl) buttons |= 0x40;
        if (raw & sr) buttons |= 0x80;
        if (raw & start) buttons |= 0x400;
    } else if (gesture) buttons = 0x800;
    if (!gesture) {
        if (raw & (left ? 0x40 : 0x80)) buttons |= 0x100;
        if (raw & (left ? 0x100 : 0x200)) buttons |= 0x200;
        if (raw & (left ? 0x10 : 0x20)) buttons |= 0x10;
    }
    const int32_t x = clamp(left ? sample->lx : sample->rx);
    const int32_t y = clamp(left ? sample->ly : sample->ry);
    sample->lx = left ? -y : y;
    sample->ly = left ? x : -x;
    sample->rx = sample->ry = 0;
    sample->buttons = buttons;
    return latched;
}

static uint32_t read_style(Ps2NativePad *p, uint32_t style, uint32_t id, Ps2NativePadFrame *out,
                           const Ps2NativePadFrame *previous) {
    Ps2NativeNpadState sample = {0};
    uint32_t rc = 0;
    if (p->reader) rc = p->reader(p->reader_context, style, id, &sample);
    else p->api.get[style](&sample, &id);
    ++p->reads;
    if (!rc && (sample.attributes & 1)) {
        const int latched = previous && (style == PS2_NPAD_LEFT || style == PS2_NPAD_RIGHT)
            ? normalize_joycon(&sample, style, id, previous) : 0;
        *out = map(&sample, id, style);
        if (latched) out->buttons |= PS2_JOYCON_MENU_LATCH;
    }
    return rc;
}

static uint32_t read_one(Ps2NativePad *p, uint32_t style, uint32_t id, Ps2NativePadFrame *out) {
    return read_style(p, style, id, out, NULL);
}
uint32_t ps2_native_pad_read(Ps2NativePad *p,Ps2NativePadFrame *out){
    if(!p||!out)return PS2_PAD_INVALID;if(!lock(p))return PS2_PAD_BUSY;
    if(!p->ready)return done(p,PS2_PAD_NOT_READY);
    Ps2NativePadFrame next=neutral(),previous=p->frame;uint64_t reads=p->reads;uint32_t rc=0;
    if(previous.connected)rc=read_one(p,previous.style,previous.npad_id,&next);
    for(uint32_t style=0;style<5&&!rc&&!next.connected;style++)
      for(uint32_t index=0;index<(style?8u:1u)&&!rc&&!next.connected;index++){
        uint32_t id=style?index:32;
        if(previous.connected&&previous.style==style&&previous.npad_id==id)continue;
        rc=read_one(p,style,id,&next);
    }
    if(previous.connected&&!next.connected)p->disconnects++;
    if(previous.connected&&next.connected&&(previous.style!=next.style||previous.npad_id!=next.npad_id))p->switches++;
    uint32_t used=(uint32_t)(p->reads-reads);if(used>p->max_reads_per_poll)p->max_reads_per_poll=used;
    p->frame=next;p->polls++;*out=next;return done(p,rc);
}
static uint32_t read_device(Ps2NativePad *p, uint32_t id, const Ps2NativePadFrame *previous,
                            Ps2NativePadFrame *frame) {
    if (id == 32) return read_style(p, PS2_NPAD_HANDHELD, id, frame, previous);
    uint32_t cached = previous->connected && previous->npad_id == id
        ? previous->style : PS2_NPAD_NONE;
    if (cached >= PS2_NPAD_FULL && cached <= PS2_NPAD_RIGHT) {
        uint32_t rc = read_style(p, cached, id, frame, previous);
        if (rc || frame->connected) return rc;
    }
    for (uint32_t style = PS2_NPAD_FULL; style <= PS2_NPAD_RIGHT; ++style) {
        if (style == cached) continue;
        uint32_t rc = read_style(p, style, id, frame, previous);
        if (rc || frame->connected) return rc;
    }
    return 0;
}

uint32_t ps2_native_pad_read_group(Ps2NativePad *p, Ps2NativePadFrame players[PS2_PAD_PLAYERS],
                                  const uint32_t devices[PS2_PAD_PLAYERS], unsigned count) {
    if (!p || !players || !devices || !count || count > PS2_PAD_PLAYERS) return PS2_PAD_INVALID;
    for (unsigned i = 0; i < count; ++i)
        if (devices[i] >= PS2_PAD_DEVICE_COUNT) return PS2_PAD_INVALID;
    if (!lock(p)) return PS2_PAD_BUSY;
    if (!p->ready) return done(p, PS2_PAD_NOT_READY);

    uint32_t ids[PS2_PAD_PLAYERS];
    uint64_t reserved = 0;
    for (unsigned i = 0; i < PS2_PAD_PLAYERS; ++i) ids[i] = PS2_NPAD_NONE;
    for (unsigned i = 0; i < count; ++i) {
        if (devices[i] < PS2_PAD_DEVICE_FIRST) continue;
        const uint32_t id = devices[i] == PS2_PAD_DEVICE_HANDHELD ? 32 : devices[i] - PS2_PAD_DEVICE_FIRST;
        if (reserved & (UINT64_C(1) << id)) continue;
        reserved |= UINT64_C(1) << id;
        ids[i] = id;
    }
    for (unsigned i = 0; i < count; ++i)
        if (devices[i] == PS2_PAD_DEVICE_AUTO && !(reserved & (UINT64_C(1) << i)))
            ids[i] = i;

    const uint64_t start = p->reads;
    Ps2NativePadFrame next[PS2_PAD_PLAYERS];
    for (unsigned i = 0; i < PS2_PAD_PLAYERS; ++i) next[i] = neutral();
    uint32_t rc = 0;
    if (devices[0] == PS2_PAD_DEVICE_AUTO) {
        const uint32_t previous = players[0].npad_id;
        const int keep_previous = players[0].connected && previous > 0 && previous < 8 &&
            !(reserved & (UINT64_C(1) << previous));
        if (keep_previous)
            rc = read_device(p, previous, &players[0], &next[0]);
        if (!rc && !next[0].connected && !(reserved & (UINT64_C(1) << 32)))
            rc = read_device(p, 32, &players[0], &next[0]);
        if (!rc && !next[0].connected && ids[0] != PS2_NPAD_NONE)
            rc = read_device(p, ids[0], &players[0], &next[0]);
        // A sole controller can have a higher HID ID after pairing or resuming.
        for (uint32_t id = 1; !rc && !next[0].connected && id < 8; ++id) {
            if ((keep_previous && id == previous) || (reserved & (UINT64_C(1) << id))) continue;
            rc = read_device(p, id, &players[0], &next[0]);
        }
    }
    for (unsigned i = 0; !rc && i < count; ++i) {
        if (next[i].connected || ids[i] == PS2_NPAD_NONE) continue;
        if ((!i && devices[0] == PS2_PAD_DEVICE_AUTO) ||
            (i && next[0].connected && ids[i] == next[0].npad_id)) continue;
        // Test an empty automatic slot every eighth poll. This avoids four
        // SDK style reads on every frame when a player has no controller.
        if (i && devices[i] == PS2_PAD_DEVICE_AUTO && !players[i].connected &&
            p->polls && p->polls % 8 != i) continue;
        rc = read_device(p, ids[i], &players[i], &next[i]);
    }

    if (rc)
        for (unsigned i = 0; i < PS2_PAD_PLAYERS; ++i) next[i] = neutral();
    if (p->frame.connected && !next[0].connected) ++p->disconnects;
    if (p->frame.connected && next[0].connected &&
        (p->frame.style != next[0].style || p->frame.npad_id != next[0].npad_id)) ++p->switches;
    const uint32_t reads = (uint32_t)(p->reads - start);
    if (reads > p->max_reads_per_poll) p->max_reads_per_poll = reads;
    ++p->polls;
    p->frame = next[0];
    memcpy(players, next, sizeof(next));
    return done(p, rc);
}
uint32_t ps2_native_pad_unbind(Ps2NativePad *p){
    if(!p)return PS2_PAD_INVALID;if(!lock(p))return PS2_PAD_BUSY;if(!p->ready)return done(p,PS2_PAD_NOT_READY);
    p->ready=0;p->sdk_verified=0;memset(&p->api,0,sizeof p->api);
    p->reader=NULL;p->reader_context=NULL;p->frame=neutral();return done(p,0);
}
