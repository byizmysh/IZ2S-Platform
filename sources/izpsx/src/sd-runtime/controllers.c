#include "controllers.h"
#include "hid-bindings.h"
#include "settings.h"
#include <string.h>
#define BAD_BINDING 0xf330u
#define PAD_OVERRUN 0xf331u
_Static_assert(PS1_HID_COUNT==9,"Controller binding table");
uint32_t ps1_controllers_bind(Ps1Controllers*p,uintptr_t sdk,uint64_t bytes){
 memset(p,0,sizeof *p);
 if(!sdk||sdk>=(1ull<<39)||bytes>(1ull<<39)-sdk)return BAD_BINDING;
 for(unsigned i=0;i<PS1_HID_COUNT;i++){
  uint32_t offset=ps1_hid_bindings[i].offset;
  if(bytes<64||offset>bytes-64)return BAD_BINDING;
  const volatile uint8_t*f=(const volatile uint8_t*)(sdk+offset);
  for(unsigned j=0;j<64;j++)if(f[j]!=ps1_hid_bindings[i].bytes[j])return BAD_BINDING;
 }
 for(unsigned i=0;i<PS1_HID_COUNT;i++)p->functions[i]=sdk+ps1_hid_bindings[i].offset;
 return 0;
}
uint32_t ps1_controllers_enable(Ps1Controllers*p){
 const uint32_t ids[]={0,1,2,3,4,5,6,7,32};
 uint32_t rc=((uint32_t(*)(const uint32_t*,int32_t))p->functions[PS1_HID_setIds])(ids,9);
 p->configuration_error=rc;p->configured=!rc;
 uint32_t styles=0,err=((uint32_t(*)(uint32_t*))p->functions[PS1_HID_getStyles])(&styles);
 if(!err&&!p->restore_styles)p->original_styles=styles;
 if(!err&&(styles&31u)!=31u){err=((uint32_t(*)(uint32_t))p->functions[PS1_HID_setStyles])(styles|31u);if(!err){p->restore_styles=1;err=((uint32_t(*)(uint32_t*))p->functions[PS1_HID_getStyles])(&styles);if(!err&&(styles&31u)!=31u)err=0xf333;}}
 p->style_error=err;return rc?rc:err;
}
uint32_t ps1_controllers_restore(Ps1Controllers*p){
 if(!p->restore_styles)return 0;
 uint32_t rc=((uint32_t(*)(uint32_t))p->functions[PS1_HID_setStyles])(p->original_styles),value=0;
 if(!rc)rc=((uint32_t(*)(uint32_t*))p->functions[PS1_HID_getStyles])(&value);
 if(!rc&&value!=p->original_styles)rc=0xf333;if(!rc)p->restore_styles=0;return rc;
}
const char*ps1_controller_style_name(unsigned style){switch(style){case PS1_PAD_LEFT:return "JOY-CON L / SIDEWAYS";case PS1_PAD_RIGHT:return "JOY-CON R / SIDEWAYS";case PS1_PAD_DUAL:return "JOY-CON PAIR";case PS1_PAD_HANDHELD:return "HANDHELD";case PS1_PAD_FULL:return "FULL CONTROLLER";default:return "NO DEVICE";}}
/* Normalize a sideways half to the same button/axis convention as a full pad.
 * SL/SR are L1/R1; outer shoulder/trigger are L2/R2; stick click is Select.
 * The half's +/- is Start; SL+SR and +/- together open the emulator menu. */
static void sideways(Ps1Controller*p){
 uint64_t raw=p->buttons,b=0;int left=p->style==PS1_PAD_LEFT;
 const uint32_t from_left[]={0x8000,0x1000,0x4000,0x2000},from_right[]={4,1,8,2};
 const uint32_t*from=left?from_left:from_right;
 for(unsigned i=0;i<4;i++)if(raw&from[i])b|=1u<<i;
 uint32_t sl=1u<<(left?16:18),sr=1u<<(left?17:19),start=left?0x800:0x400;
 if(raw&sl)b|=0x40;if(raw&sr)b|=0x80;
 if(raw&(left?0x40:0x80))b|=0x100;if(raw&(left?0x100:0x200))b|=0x200;
 if(raw&(left?0x10:0x20))b|=0x10;
 if(raw&start){if((raw&(sl|sr))==(sl|sr))b=0x800;else b|=0x400;}
 int32_t x=p->axes[left?0:2],y=p->axes[left?1:3];p->axes[0]=left?-y:y;p->axes[1]=left?x:-x;p->axes[2]=p->axes[3]=0;p->buttons=b;
}
static void shortcut_release(Ps1Controller*p,const Ps1Controller*previous){
 if(p->style!=PS1_PAD_LEFT&&p->style!=PS1_PAD_RIGHT)return;
 unsigned left=p->style==PS1_PAD_LEFT;uint64_t mask=left?(0x30000|0x800):(0xc0000|0x400);
 if(p->buttons&0x800)p->shortcut_latched=1;
 else if(previous->style==p->style&&previous->shortcut_latched&&(p->physical_buttons&mask))p->shortcut_latched=1;
 if(p->shortcut_latched)p->buttons&=~(uint64_t)(0x40|0x80|0x400);
}
uint64_t ps1_controller_menu_buttons(const Ps1Controller*p){
 uint64_t b=p->buttons;if(p->style==PS1_PAD_LEFT||p->style==PS1_PAD_RIGHT){if(p->axes[0]<-12000)b|=0x1000;if(p->axes[0]>12000)b|=0x4000;if(p->axes[1]<-12000)b|=0x8000;if(p->axes[1]>12000)b|=0x2000;}return b;
}
static uint32_t read_style(Ps1Controllers*p,unsigned function,uint32_t id,unsigned style,Ps1Controller*out){
 uint8_t guarded[96];memset(guarded,0,sizeof guarded);
 for(unsigned i=0;i<16;i++)guarded[i]=guarded[80+i]=0xa5;
 uint8_t*raw=guarded+16;
 ((void(*)(void*,const uint32_t*))p->functions[function])(raw,&id);
 for(unsigned i=0;i<16;i++)if(guarded[i]!=0xa5||guarded[80+i]!=0xa5)return PAD_OVERRUN;
 uint32_t attributes;memcpy(&attributes,raw+32,4);if(!(attributes&1))return 0;
 uint64_t buttons,sample;memcpy(&sample,raw,8);memcpy(&buttons,raw+8,8);
 out->connected=1;out->id=id;out->style=style;out->attributes|=attributes;out->buttons|=buttons;if(sample>out->sample)out->sample=sample;
 for(unsigned i=0;i<4;i++){
  int32_t value;memcpy(&value,raw+16+i*4,4);if(value>32767)value=32767;if(value<-32767)value=-32767;
  int32_t old=out->axes[i];if((value<0?-value:value)>(old<0?-old:old))out->axes[i]=value;
 }out->physical_buttons=out->buttons;if(style==PS1_PAD_LEFT||style==PS1_PAD_RIGHT)sideways(out);return 0;
}
uint32_t ps1_controllers_poll(Ps1Controllers*p){
 Ps1Controller next[9];memset(next,0,sizeof next);uint32_t rc;
 const unsigned kinds[]={PS1_HID_padFullKey,PS1_HID_padJoyDual,PS1_HID_padJoyLeft,PS1_HID_padJoyRight};
 for(unsigned slot=0;slot<8;slot++){
  next[slot].id=slot;if(slot&&!p->configured)continue;
  const unsigned styles[]={PS1_PAD_FULL,PS1_PAD_DUAL,PS1_PAD_LEFT,PS1_PAD_RIGHT};
  for(unsigned kind=0;kind<4;kind++){rc=read_style(p,kinds[kind],slot,styles[kind],&next[slot]);if(rc)goto fail;if(next[slot].connected){shortcut_release(&next[slot],&p->device[slot]);break;}}
 }
 next[8].id=32;rc=read_style(p,PS1_HID_padHandheld,32,PS1_PAD_HANDHELD,&next[8]);if(rc)goto fail;
 memcpy(p->device,next,sizeof next);ps1_controllers_assign(p,p->assignment);p->polls++;return 0;
 fail:memset(p->player,0,sizeof p->player);memset(&p->menu,0,sizeof p->menu);return rc;
}
void ps1_controllers_assign(Ps1Controllers*p,const uint32_t assignment[PS1_PLAYER_COUNT]){
 uint32_t selected[PS1_PLAYER_COUNT];memcpy(selected,assignment,sizeof selected);memcpy(p->assignment,selected,sizeof selected);memset(p->player,0,sizeof p->player);
 int slots[PS1_PLAYER_COUNT];for(unsigned i=0;i<PS1_PLAYER_COUNT;i++)slots[i]=-1;
 for(unsigned i=0;i<PS1_PLAYER_COUNT;i++)if(selected[i]>=PS1_DEVICE_PAD1&&selected[i]<PS1_DEVICE_COUNT){int slot=selected[i]==PS1_DEVICE_HANDHELD?8:(int)selected[i]-PS1_DEVICE_PAD1,used=0;for(unsigned j=0;j<i;j++)if(slots[j]==slot)used=1;if(!used)slots[i]=slot;}
 for(unsigned i=0;i<PS1_PLAYER_COUNT;i++)if(selected[i]==PS1_DEVICE_AUTO){int slot=(int)i,used=0;for(unsigned j=0;j<PS1_PLAYER_COUNT;j++)if(slots[j]==slot)used=1;if(!used)slots[i]=slot;}
 if(selected[0]==PS1_DEVICE_AUTO&&slots[0]==0&&!p->device[0].connected){int used=0;for(unsigned j=1;j<PS1_PLAYER_COUNT;j++)if(slots[j]==8)used=1;if(!used)slots[0]=8;}
 for(unsigned i=0;i<PS1_PLAYER_COUNT;i++)if(slots[i]>=0)p->player[i]=p->device[slots[i]];
 memset(&p->menu,0,sizeof p->menu);
 if(p->player[0].connected)p->menu=p->player[0];else for(unsigned i=0;i<9;i++)if(p->device[i].connected){p->menu=p->device[i];break;}
}
uint32_t ps1_controllers_disconnect(Ps1Controllers*p,unsigned player){
 if(player>=PS1_PLAYER_COUNT||!p->player[player].connected||p->player[player].id>=8)return 0xf332;
 uint32_t id=p->player[player].id;
 return ((uint32_t(*)(const uint32_t*))p->functions[PS1_HID_disconnect])(&id);
}
uint32_t ps1_controller_buttons(uint64_t raw){
 const uint32_t native[]={2,8,0x10,0x400,0x2000,0x8000,0x1000,0x4000,1,4,0x40,0x80,0x100,0x200};uint32_t out=0;
 for(unsigned i=0;i<14;i++)if(raw&native[i])out|=1u<<i;
 return out;
}
