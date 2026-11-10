/* Non-executable PC package previews and a console-owned installation queue.
 * Publication is staged separately from rendering and applied only in menu. */
#include "abi.h"
#define WORD(p,n) (*(u32*)((u8*)(uintptr_t)(p)+(n)))
static int zeros(const void*data,size_t bytes){const u8*p=data;for(size_t i=0;i<bytes;i++)if(p[i])return 0;return 1;}
static int identity(const char*id){size_t n=0;while(n<48&&id[n]){char ch=id[n];if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||(n&&ch=='-')))return 0;n++;}return n&&n<48&&zeros(id+n,48-n);}
void iz_menu_changed(IzContext*c){
 // Both shell shaders hash menu fields. Change the existing host-edit epoch
 // at this joined frame boundary, so their guard seals our legitimate edits
 // instead of restoring an obsolete GPU checkpoint over the new catalog.
 u32 epoch=WORD(c->controls,108);epoch=epoch<0x3f800000u||epoch>=0x4b800000u?0x3f800000u:epoch+1u;WORD(c->controls,108)=epoch;WORD(c->snapshot,108)=epoch;
}
void iz_library_menu(IzContext*c){
 if(c->host_mode){if(c->host_install_sequence&&c->host_install_ack!=c->host_install_sequence)WORD(c->snapshot,796)=0;return;}
 u32 mask=0;for(unsigned i=0;i<c->library_count;i++)for(unsigned j=0;j<c->catalog_count;j++)if(!memcmp(c->library[i].id,c->catalog[j].id,48))mask|=1u<<i;
 WORD(c->snapshot,784)=0;WORD(c->snapshot,788)=0;WORD(c->snapshot,792)=0;WORD(c->snapshot,796)=(1u<<c->library_count)-1u;
 WORD(c->snapshot,808)=c->picker_observed;WORD(c->snapshot,812)=0;WORD(c->snapshot,832)=c->library_count;WORD(c->snapshot,836)=0;WORD(c->snapshot,840)=mask;WORD(c->snapshot,844)=c->picker_observed;
}
static u32 validate(IzContext*c){
 if(!c->library_staging||c->library_capacity<512||c->library_capacity>IZ_LIBRARY_CAPACITY)return 0xe500;
 const IzLibraryHeader*h=(void*)(uintptr_t)c->library_staging;
 if(memcmp(h->magic,"IZSDSEL1",8)||h->version!=1||h->count>IZ_LIBRARY_MAX||h->bytes!=512+h->count*(96+IZ_TILE_BYTES)||h->bytes>c->library_capacity||!zeros(h->padding,sizeof h->padding))return 0xe501;
 const IzLibraryEntry*rows=(void*)(h+1);if(iz_crc(rows,h->count*sizeof *rows)!=h->table_crc)return 0xe502;
 const u8*tiles=(const u8*)(rows+h->count);
 for(unsigned i=0;i<h->count;i++){
  if(!identity(rows[i].id)||!zeros(rows[i].reserved,sizeof rows[i].reserved))return 0xe503;
  for(unsigned j=0;j<i;j++)if(!memcmp(rows[i].id,rows[j].id,48))return 0xe503;
  if(iz_crc(tiles+i*IZ_TILE_BYTES,IZ_TILE_BYTES)!=rows[i].tile_crc)return 0xe504;
 }return 0;
}
static void acknowledge(IzContext*c){
 if(!c->request_count)return;const IzInstallRequest*first=&c->requests[c->request_head];
 if(__atomic_load_n(&c->request_ack,__ATOMIC_ACQUIRE)!=first->sequence)return;
 __atomic_add_fetch(&c->request_revision,1,__ATOMIC_ACQ_REL);c->request_error=c->request_ack_error;
 memset(&c->requests[c->request_head],0,sizeof *first);c->request_head=(c->request_head+1)%IZ_LIBRARY_MAX;c->request_count--;
 __atomic_store_n(&c->request_ack,0,__ATOMIC_RELEASE);__atomic_add_fetch(&c->request_revision,1,__ATOMIC_RELEASE);
}
static void capture(IzContext*c){
 u32 index=WORD(c->controls,812),sequence=WORD(c->controls,808);if(!index||sequence==c->picker_observed)return;
 c->picker_observed=sequence;c->request_error=0;const IzLibraryEntry*entry=index<=c->library_count?&c->library[index-1]:0;
 if(!entry)c->request_error=0xe505;
 else{
  int duplicate=0;for(unsigned i=0;i<c->request_count;i++){const IzInstallRequest*q=&c->requests[(c->request_head+i)%IZ_LIBRARY_MAX];if(!memcmp(q->id,entry->id,48)&&!memcmp(q->sha256,entry->sha256,32))duplicate=1;}
  if(!duplicate){
   if(c->request_count==IZ_LIBRARY_MAX||c->request_sequence==0xffffffffu)c->request_error=0xe506;
   else{
    __atomic_add_fetch(&c->request_revision,1,__ATOMIC_ACQ_REL);IzInstallRequest*q=&c->requests[(c->request_head+c->request_count)%IZ_LIBRARY_MAX];
    memset(q,0,sizeof *q);q->sequence=++c->request_sequence;memcpy(q->id,entry->id,48);memcpy(q->sha256,entry->sha256,32);c->request_count++;__atomic_add_fetch(&c->request_revision,1,__ATOMIC_RELEASE);
   }
  }
 }
 // A PC outage must not trap the user in the picker. Returning home does not
 // cancel the durable-in-process request or interfere with SD app execution.
 WORD(c->controls,812)=0;WORD(c->controls,836)=0;WORD(c->controls,844)=sequence;WORD(c->controls,800)=0;
 memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,1024);
 iz_menu_changed(c);
}
static void mixed_capture(IzContext*c){
 if(c->active)return;
 if(c->host_install_sequence&&__atomic_load_n(&c->host_install_ack,__ATOMIC_ACQUIRE)==c->host_install_sequence){
  __atomic_add_fetch(&c->request_revision,1,__ATOMIC_ACQ_REL);c->host_install_index=c->host_install_target=c->host_install_sequence=c->host_install_ack=0;__atomic_add_fetch(&c->request_revision,1,__ATOMIC_RELEASE);
 }
 if(c->host_install_sequence)return;
 u32 index=WORD(c->controls,812),sequence=WORD(c->controls,808),target=WORD(c->controls,792);if(!index||sequence==c->picker_observed)return;
 __atomic_add_fetch(&c->request_revision,1,__ATOMIC_ACQ_REL);c->picker_observed=sequence;
 if(index<=IZ_LIBRARY_MAX&&index<=WORD(c->controls,832)&&sequence&&(target==1||target==2)){c->host_install_index=index;c->host_install_target=target;c->host_install_sequence=sequence;WORD(c->controls,796)=0;}
 else c->request_error=0xe505;
 WORD(c->controls,784)=0;WORD(c->controls,792)=0;WORD(c->controls,800)=0;WORD(c->controls,812)=0;WORD(c->controls,836)=0;
 memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,1024);iz_menu_changed(c);__atomic_add_fetch(&c->request_revision,1,__ATOMIC_RELEASE);
}
void iz_library_tick(IzContext*c){
 if(c->host_mode){mixed_capture(c);return;}
 if(c->active)return;acknowledge(c);
 // Bind a selection to the catalog which was visible when it was made,
 // before accepting a newer host publication in this same frame.
 capture(c);u32 offer=__atomic_load_n(&c->library_offer,__ATOMIC_ACQUIRE);if(offer==c->library_applied)return;
 u32 error=validate(c);__atomic_add_fetch(&c->library_revision,1,__ATOMIC_ACQ_REL);c->library_error=error;if(!error){
  const IzLibraryHeader*h=(void*)(uintptr_t)c->library_staging;const IzLibraryEntry*rows=(void*)(h+1);const u8*tiles=(const u8*)(rows+h->count);
  c->library_count=h->count;memset(c->library,0,sizeof c->library);memcpy(c->library,rows,h->count*sizeof *rows);
  u8*target=(u8*)(uintptr_t)c->snapshot+1024+IZ_ATLAS_BYTES+16*IZ_TILE_BYTES;memset(target,0,16*IZ_TILE_BYTES);memcpy(target,tiles,h->count*IZ_TILE_BYTES);
  iz_library_menu(c);memcpy((u8*)(uintptr_t)c->controls+1024+IZ_ATLAS_BYTES+16*IZ_TILE_BYTES,target,16*IZ_TILE_BYTES);memcpy((u8*)(uintptr_t)c->controls+784,(u8*)(uintptr_t)c->snapshot+784,16);memcpy((u8*)(uintptr_t)c->controls+832,(u8*)(uintptr_t)c->snapshot+832,16);
  iz_menu_changed(c);
 }
 __atomic_store_n(&c->library_applied,offer,__ATOMIC_RELEASE);__atomic_add_fetch(&c->library_revision,1,__ATOMIC_RELEASE);
}
