#include "fs-dir.h"
#include <string.h>
#ifdef IZPSX_SB4_STORAGE
#include "sd-runtime/storage-sb4.h"
#include <stdlib.h>
#endif

static size_t bounded_length(const char *s,size_t max){size_t n=0;if(!s)return max;while(n<max&&s[n])n++;return n;}
static int name_valid(const char *s){size_t n=bounded_length(s,PS1_SD_ENTRY_NAME_MAX);if(!n||n>=PS1_SD_ENTRY_NAME_MAX)return 0;if((n==1&&s[0]=='.')||(n==2&&s[0]=='.'&&s[1]=='.'))return 0;for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32||(unsigned char)s[i]==127||s[i]=='/'||s[i]=='\\')return 0;return 1;}
static int canonical(const char *p){size_t n=bounded_length(p,PS1_SD_PATH_MAX);if(!n||n>=PS1_SD_PATH_MAX||p[0]!='/'||(n>1&&p[n-1]=='/'))return 0;for(size_t i=1;i<n;){size_t start=i;while(i<n&&p[i]!='/'){if((unsigned char)p[i]<32||(unsigned char)p[i]==127||p[i]=='\\')return 0;i++;}size_t k=i-start;if(!k||(k==1&&p[start]=='.')||(k==2&&p[start]=='.'&&p[start+1]=='.'))return 0;i++;}return 1;}
int ps1_sd_path_valid(const char *root,const char *path){if(!canonical(root)||!canonical(path))return 0;size_t n=bounded_length(root,PS1_SD_PATH_MAX),k=bounded_length(path,PS1_SD_PATH_MAX);return k>=n&&!memcmp(root,path,n)&&(path[n]==0||path[n]=='/'||n==1);}
int ps1_sd_join(const char *root,const char *directory,const char *name,char out[PS1_SD_PATH_MAX]){if(!ps1_sd_path_valid(root,directory)||!name_valid(name))return 0;size_t n=bounded_length(directory,PS1_SD_PATH_MAX),k=bounded_length(name,PS1_SD_ENTRY_NAME_MAX);size_t slash=n>1;if(n+slash+k>=PS1_SD_PATH_MAX)return 0;memmove(out,directory,n);if(slash)out[n++]='/';memcpy(out+n,name,k+1);return 1;}
int ps1_sd_parent(const char *root,const char *path,char out[PS1_SD_PATH_MAX]){if(!ps1_sd_path_valid(root,path))return 0;size_t n=bounded_length(path,PS1_SD_PATH_MAX),r=bounded_length(root,PS1_SD_PATH_MAX);if(n==r){memmove(out,path,n+1);return 0;}while(n>r&&path[n-1]!='/')n--;if(n>r)n--;if(n<r)n=r;memmove(out,path,n);out[n]=0;return 1;}

/* Decode CMIF after the complete HIPC response header. The only response
 * allowed to carry a capability is OpenDirectory (one moved handle). */
static uint32_t response(Ps1SdDirectory *s,uint8_t *m,int opened,uint32_t *handle,uint64_t *value){
 uint32_t *p=(void*)m;unsigned at=8,copies=0,moves=0;uint32_t received[30];
 if(p[1]&0x80000000u){uint32_t special=p[2];copies=(special>>1)&15;moves=(special>>5)&15;at+=4;if(special&1)at+=8;for(unsigned i=0;i<copies+moves;i++){received[i]=*(uint32_t*)(m+at);at+=4;}}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 uint32_t rc=at>512||*(uint32_t*)(m+at)!=0x4f434653?PS1_SD_PACKET:*(uint32_t*)(m+at+8);
 int valid=opened?copies==0&&moves==1&&received[0]>0&&received[0]<0x80000000u:copies==0&&moves==0;
 if(!rc&&!valid)rc=PS1_SD_PACKET;
 if(rc){for(unsigned i=0;i<copies+moves;i++)if(received[i]&&received[i]<0x80000000u&&s->close_handle(received[i]))s->leaked_handle=received[i];return rc;}
 if(opened)*handle=received[0];else *value=*(uint64_t*)(m+at+16);
 return 0;
}
uint32_t ps1_sd_list(Ps1SdDirectory *s,const char *root,const char *path,Ps1SdEntry *entries,size_t capacity,size_t *count){
 if(count)*count=0;
#ifdef IZPSX_SB4_STORAGE
 if(s&&s->app){
  if(!count||!entries||!capacity||capacity>SB4_STORAGE_LIST_MAX)return PS1_SD_INVALID;
  const Sb4Storage*storage=sb4ModuleStorage(s->app);
  const char*name=ps1_sb4_relative(root,path);
  if(!storage||!name)return PS1_SD_INVALID;
  uint32_t found=0;s->transport=0;s->calls++;
  uint32_t rc=storage->list(s->app,name,entries,(uint32_t)capacity,&found);
  if(!rc&&found>capacity)rc=PS1_SD_LIMIT;
  s->error=rc==SB4_STORAGE_LIMIT?PS1_SD_LIMIT:rc;
  if(s->error)return s->error;
  *count=found;s->entries_read+=found;return 0;
 }
#endif
 if(!s||!count||!entries||!capacity||!s->send||!s->close_handle||!s->filesystem||s->filesystem>=0x80000000u||!s->buffer||((uintptr_t)s->buffer&4095)||s->capacity<0x310||s->capacity>65536||!ps1_sd_path_valid(root,path))return PS1_SD_INVALID;
 if(s->leaked_handle)return PS1_SD_CLOSE;
 s->error=s->transport=0;_Alignas(4096) uint8_t message[4096];memset(s->buffer,0,PS1_SD_PATH_MAX);memcpy(s->buffer,path,strlen(path));memset(message,0,sizeof message);uint32_t *p=(void*)message;uintptr_t a=(uintptr_t)s->buffer;
 if((uint64_t)a>=(1ull<<39)||s->capacity>(1ull<<39)-(uint64_t)a)return PS1_SD_INVALID;
 p[0]=4u|(1u<<16);p[1]=9;p[2]=(0x301u<<16)|((uint32_t)(a>>36)&7u)<<6|((uint32_t)(a>>32)&15u)<<12;p[3]=(uint32_t)a;p[4]=0x49434653;p[6]=9;p[8]=3;
 uint32_t directory=0;s->calls++;uint32_t rc=s->send(message,sizeof message,s->filesystem);if(rc){s->transport=rc;return s->error=rc;}uint64_t got=0;rc=response(s,message,1,&directory,&got);if(rc)return s->error=rc;
 size_t count_total=0,per_page=s->capacity/0x310;uint64_t bytes=per_page*0x310;
 for(;;){memset(message,0,sizeof message);p[0]=4u|(1u<<24);p[1]=8;a=(uintptr_t)s->buffer;p[2]=(uint32_t)bytes;p[3]=(uint32_t)a;p[4]=((uint32_t)(a>>36)&7u)<<2|((uint32_t)(a>>32)&15u)<<28;p[8]=0x49434653;p[10]=0;
  s->calls++;rc=s->send(message,sizeof message,directory);if(rc){s->transport=rc;break;}rc=response(s,message,0,&directory,&got);if(rc)break;if(got>per_page){rc=PS1_SD_PACKET;break;}if(!got)break;if(got>capacity-count_total){rc=PS1_SD_LIMIT;break;}
  for(size_t i=0;i<(size_t)got;i++){const uint8_t *row=(const uint8_t*)s->buffer+i*0x310;Ps1SdEntry *e=&entries[count_total];if(bounded_length((const char*)row,0x301)>=0x301||!name_valid((const char*)row)||row[0x304]>1){rc=PS1_SD_ENTRY;break;}memcpy(e->name,row,strlen((const char*)row)+1);memcpy(&e->size,row+0x308,8);e->directory=row[0x304]==0;count_total++;s->entries_read++;}if(rc)break;
 }
 uint32_t close_rc=s->close_handle(directory);if(close_rc){s->leaked_handle=directory;if(!rc)rc=PS1_SD_CLOSE;}if(rc)return s->error=rc;*count=count_total;return 0;
}
static unsigned lower(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int suffix(const char *s,const char *ext){size_t n=strlen(s),k=strlen(ext);if(n<k)return 0;for(size_t i=0;i<k;i++)if(lower((unsigned char)s[n-k+i])!=(unsigned char)ext[i])return 0;return 1;}
static int compare(const Ps1SdEntry *a,const Ps1SdEntry *b){if(a->directory!=b->directory)return a->directory?-1:1;for(size_t i=0;;i++){unsigned x=lower((unsigned char)a->name[i]),y=lower((unsigned char)b->name[i]);if(x!=y)return x<y?-1:1;if(!x)return 0;}}
static int catalog_entry(const Ps1SdEntry *entry, int include_bin) {
    return entry->directory || suffix(entry->name, ".cue") || suffix(entry->name, ".pbp") ||
        (include_bin && suffix(entry->name, ".bin"));
}

#ifdef IZPSX_SB4_STORAGE
static int compare_catalog(const void *a, const void *b) { return compare(a, b); }

static uint32_t grow_catalog(const Sb4Compat *compat, Ps1SdCatalog *catalog, size_t capacity, size_t used) {
    if (capacity <= catalog->capacity) return 0;
    if (capacity > SIZE_MAX / sizeof(Ps1SdEntry)) return PS1_SD_LIMIT;
    void *memory = NULL;
    uint32_t rc = compat->allocate_aligned(&memory, 4096, capacity * sizeof(Ps1SdEntry));
    if (rc) return rc;
    if (!memory) return PS1_SD_INVALID;
    if (catalog->entries) {
        if (used) memcpy(memory, catalog->entries, used * sizeof(Ps1SdEntry));
        compat->release(catalog->entries);
    }
    *catalog = (Ps1SdCatalog){memory, capacity};
    return 0;
}

uint32_t ps1_sd_catalog_load(Ps1SdDirectory *sd, const char *root, const char *path,
                            Ps1SdCatalog *catalog, size_t *count) {
    *count = 0;
    const Sb4Compat *compat = sb4ModuleCompat(sd->app);
    const Sb4Storage *storage = sb4ModuleStorage(sd->app);
    const char *name = ps1_sb4_relative(root, path);
    if (!compat || !storage || !name) return PS1_SD_INVALID;
    const Sb4StorageDirectories *dirs = sb4StorageDirectories(storage);
    if (!dirs) {
        // Older launchers expose only the bounded, one-shot listing.
        uint32_t rc = grow_catalog(compat, catalog, 128, 0);
        if (rc) return sd->error = rc;
        const size_t capacity = catalog->capacity < SB4_STORAGE_LIST_MAX
            ? catalog->capacity : SB4_STORAGE_LIST_MAX;
        rc = ps1_sd_list(sd, root, path, catalog->entries, capacity, count);
        if (rc != PS1_SD_LIMIT || capacity == SB4_STORAGE_LIST_MAX) return rc;
        rc = grow_catalog(compat, catalog, SB4_STORAGE_LIST_MAX, 0);
        return rc ? (sd->error = rc) : ps1_sd_list(sd, root, path, catalog->entries, SB4_STORAGE_LIST_MAX, count);
    }

    uint32_t token = 0;
    uint32_t rc = ps1_sb4_receipt(sd, dirs->open(sd->app, name, &token));
    if (rc) return rc;
    if (!token) return sd->error = PS1_SD_PACKET;
    Ps1SdEntry page[SB4_STORAGE_PAGE_MAX];
    size_t used = 0;
    for (;;) {
        uint32_t rows = 0;
        rc = ps1_sb4_receipt(sd, dirs->read(sd->app, token, page, SB4_STORAGE_PAGE_MAX, &rows));
        if (rc || !rows) break;
        if (rows > SB4_STORAGE_PAGE_MAX) { rc = PS1_SD_PACKET; break; }
        for (uint32_t i = 0; i < rows; ++i) {
            if (!name_valid(page[i].name) || page[i].directory > 1) { rc = PS1_SD_ENTRY; break; }
            if (!catalog_entry(&page[i], 1)) continue;
            if (used == catalog->capacity) {
                if (used > SIZE_MAX / 2) { rc = PS1_SD_LIMIT; break; }
                rc = grow_catalog(compat, catalog, used ? used * 2 : 128, used);
                if (rc) break;
            }
            catalog->entries[used++] = page[i];
        }
        if (rc) break;
        sd->entries_read += rows;
    }
    const uint32_t closed = dirs->close(sd->app, token);
    sd->calls++;
    if (rc || closed) return sd->error = rc ? rc : closed;
    *count = used;
    return sd->error = 0;
}

void ps1_sd_catalog_release(Ps1SdDirectory *sd, Ps1SdCatalog *catalog) {
    if (catalog->entries) sb4ModuleCompat(sd->app)->release(catalog->entries);
    *catalog = (Ps1SdCatalog){0};
}
#endif

size_t ps1_sd_filter_images(Ps1SdEntry *entries, size_t count) {
    int cue = 0;
    for (size_t i = 0; i < count; ++i)
        if (!entries[i].directory && suffix(entries[i].name, ".cue")) cue = 1;
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!catalog_entry(&entries[i], !cue)) continue;
        if (kept != i) entries[kept] = entries[i];
        ++kept;
    }
#ifdef IZPSX_SB4_STORAGE
    if (kept > 1) qsort(entries, kept, sizeof *entries, compare_catalog);
#else
    for (size_t i = 1; i < kept; ++i) {
        Ps1SdEntry row = entries[i];
        size_t j = i;
        while (j && compare(&row, &entries[j - 1]) < 0) { entries[j] = entries[j - 1]; --j; }
        entries[j] = row;
    }
#endif
    return kept;
}
