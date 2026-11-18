#define _POSIX_C_SOURCE 200809L
#include "apps.h"
#ifdef __SWITCH2__
#include <switch.h>
#endif
#include "../../basic-launcher/nro-format.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {IzPlatformApp app;char path[512];u8 header[128];u64 bytes;} Program;
enum { MAX_PROGRAMS=IZ_MAX_PROGRAMS, MAX_DEPTH=8 };
static Program *programs;
static u32 allocated;
static u32 program_count;
static char self_path[512];

void iz_ounce_apps_init(const char *self){
    free(programs);programs=NULL;allocated=program_count=0;self_path[0]=0;
    if(self&&strlen(self)<sizeof(self_path))strcpy(self_path,self);
}
static int program_order(const void *a,const void *b){
    return strcmp(((const Program*)a)->path,((const Program*)b)->path);
}
static bool read_header(const char *path,u8 *header,size_t capacity,size_t *read,u64 *bytes){
    FILE *f=fopen(path,"rb");if(!f)return false;
    struct stat st;bool ok=!fstat(fileno(f),&st)&&S_ISREG(st.st_mode)&&st.st_size>=128;
    *read=ok?fread(header,1,capacity,f):0;
    if(ok)*bytes=(u64)st.st_size;
    ok=ok&&!ferror(f)&&iz_nro_header(header,*read,*bytes);
    if(fclose(f))ok=false;
    return ok;
}
static u64 read_le(const u8 *bytes, unsigned length){
    u64 value=0;
    for(unsigned i=0;i<length;i++)value|=(u64)bytes[i]<<(i*8);
    return value;
}
static void read_nacp_title(Program *program){
    u64 image=read_le(program->header+24,4);
    if(image>program->bytes||program->bytes-image<56||image>LONG_MAX)return;
    FILE *file=fopen(program->path,"rb");
    if(!file)return;
    u8 aset[56];
    if(!fseek(file,(long)image,SEEK_SET)&&fread(aset,1,sizeof aset,file)==sizeof aset&&!memcmp(aset,"ASET",4)){
        u64 offset=read_le(aset+24,8),size=read_le(aset+32,8);
        if(offset>=sizeof aset&&offset<=program->bytes-image&&size>=0x300&&
           size<=program->bytes-image-offset&&image+offset<=LONG_MAX){
            for(unsigned i=0;i<16&&(u64)(i+1)*0x300<=size;i++){
                char title[256];
                if(image+offset+(u64)i*0x300>LONG_MAX||
                   fseek(file,(long)(image+offset+i*0x300),SEEK_SET)||
                   fread(title,1,sizeof title,file)!=sizeof title)break;
                const char *end=memchr(title,0,sizeof title);
                if(end&&end!=title&&strcspn(title,"\r\n\t")==strlen(title)){
                    size_t n=(size_t)(end-title);
                    if(n>=sizeof program->app.name)n=sizeof program->app.name-1;
                    while(n&&((unsigned char)title[n]&0xc0)==0x80)--n;
                    memcpy(program->app.name,title,n);
                    program->app.name[n]=0;
                    break;
                }
            }
        }
    }
    fclose(file);
}
static u32 add_program(const char *path,const char *name){
    if(!strcasecmp(path,self_path))return 0;
    Program p={0};size_t read=0;
    if(!iz_nro_arguments(path,(char[516]){0},516)||!read_header(path,p.header,sizeof(p.header),&read ,&p.bytes))return 0;
    strcpy(p.path,path);
    u64 hash=14695981039346656037ull;
    for(const unsigned char *s=(const void*)path;*s;s++){hash^=*s;hash*=1099511628211ull;}
    snprintf(p.app.id,sizeof(p.app.id),"nro-%016llx",(unsigned long long)hash);
    size_t n=strlen(name),base=n;
    const char *dot=strrchr(name,'.');if(dot)base=(size_t)(dot-name);
    const char *modules="sdmc:/iz2s/modules/";
    if(!strncmp(path,modules,strlen(modules))&&!strcasecmp(name,"launcher.nro")){
        name=path+strlen(modules);
        base=(size_t)(strchr(name,'/')-name);
    }
    if(base>=sizeof(p.app.name))base=sizeof(p.app.name)-1;
    memcpy(p.app.name,name,base);
    read_nacp_title(&p);
    for(u32 i=0;i<program_count;i++)if(!strcmp(programs[i].app.id,p.app.id))return 0;
    if(program_count==MAX_PROGRAMS)return 0xe860;
    if(program_count==allocated){
        u32 next=allocated?allocated*2:32;
        Program *larger=realloc(programs,next*sizeof(*programs));
        if(!larger)return 0xe861;
        programs=larger;allocated=next;
    }
    programs[program_count++]=p;return 0;
}
static u32 scan_directory(const char *path,unsigned depth){
    DIR *directory=opendir(path);
    if(!directory)return errno==ENOENT?0:0xe862;
    struct dirent *entry;u32 rc=0;
    for(;;){
        errno=0;entry=readdir(directory);
        if(!entry){if(errno)rc=0xe862;break;}
        if(entry->d_name[0]=='.')continue;
        char child[512];int length=snprintf(child,sizeof child,"%s/%s",path,entry->d_name);
        if(length<0||(size_t)length>=sizeof child)continue;
        struct stat st;if(lstat(child,&st)){rc=0xe862;continue;}
        if(S_ISDIR(st.st_mode)){
            if(depth<MAX_DEPTH){u32 child_rc=scan_directory(child,depth+1);if(child_rc)rc=child_rc;}
        }else if(S_ISREG(st.st_mode)){
            const char *extension=strrchr(entry->d_name,'.');
            if(extension&&!strcasecmp(extension,".nro")){u32 child_rc=add_program(child,entry->d_name);if(child_rc)rc=child_rc;}
        }
        if(rc==0xe860||rc==0xe861)break;
    }
    if(closedir(directory)&&!rc)rc=0xe862;
    return rc;
}
static u32 scan_modules(void){
    static const char root[]="sdmc:/iz2s/modules";
    DIR *dir=opendir(root);
    if(!dir)return errno==ENOENT?0:0xe862;
    u32 result=0;
    struct dirent *row;
    while((row=readdir(dir))!=NULL){
        if(row->d_name[0]=='.')continue;
        char folder[512],path[512];
        struct stat info;
        int n=snprintf(folder,sizeof folder,"%s/%s",root,row->d_name);
        if(n<0||(size_t)n>=sizeof folder||lstat(folder,&info)||!S_ISDIR(info.st_mode))continue;
        n=snprintf(path,sizeof path,"%s/launcher.nro",folder);
        if(n<0||(size_t)n>=sizeof path||lstat(path,&info)||!S_ISREG(info.st_mode))continue;
        result=add_program(path,"launcher.nro");
        if(result)break;
    }
    if(closedir(dir)&&!result)result=0xe862;
    return result;
}
u32 iz_ounce_apps_page(IzPlatformApp *apps,u32 capacity,u32 offset,u32 *count,u32 *total){
    if(!apps||!count||!total||!capacity||capacity>IZ_MAX_APPS)return 0xe85b;
    *count=*total=program_count=0;
    u32 rc=scan_modules();
    if(!rc)rc=scan_directory("sdmc:/switch",0);
    if(program_count>1)qsort(programs,program_count,sizeof(*programs),program_order);
    *total=program_count;
    for(u32 i=offset;i<program_count&&*count<capacity;i++){
        programs[i].app.token=i+1;apps[(*count)++]=programs[i].app;
    }
    return rc;
}
u32 iz_ounce_apps_scan(IzPlatformApp *apps,u32 capacity,u32 *count){
    u32 total;return iz_ounce_apps_page(apps,capacity,0,count,&total);
}
u32 iz_ounce_apps_prepare(u32 token,IzOunceLaunch *launch){
    if(!launch||!token||token>program_count)return 0xe85b;
    memset(launch,0,sizeof(*launch));Program *p=&programs[token-1];u64 bytes=0;
    if(!read_header(p->path,launch->header,sizeof(launch->header),&launch->header_bytes,&bytes))return 0xe85c;
    if(bytes!=p->bytes||memcmp(launch->header,p->header,sizeof(p->header)))return 0xe85c;
    if(!iz_nro_arguments(p->path,launch->arguments,sizeof(launch->arguments)))return 0xe85b;
    strcpy(launch->path,p->path);return 0;
}

static bool module_sidecar(const char *nro,const char *suffix,char output[512]){
    static const char root[]="sdmc:/iz2s/modules/", name[]="/launcher.nro";
    size_t size=strlen(nro);
    if(strncmp(nro,root,sizeof root-1)||size<sizeof root+sizeof name-2||
       strcmp(nro+size-(sizeof name-1),name))return false;
    size_t prefix=size-(sizeof name-1);
    size_t length=strlen(suffix)+1;
    if(prefix+length>512)return false;
    memcpy(output,nro,prefix);
    memcpy(output+prefix,suffix,length);
    return true;
}

/* The shared artwork decoder validates the NRO ASET section and bounds JPEG
   decoding. It is also used by IZ2S Store to render installed apps. */
extern bool installed_artwork(FILE *file, uint64_t length, bool iza, uint32_t *pixels);

static bool read_module_icon(const char *nro, uint32_t *pixels, size_t size) {
    char path[512];
    struct stat st;
    if (!module_sidecar(nro, "/.izstore-icon.rgba", path) ||
        lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_size != (off_t)size) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fread(pixels, 1, size, file) == size && fgetc(file) == EOF && !ferror(file);
    return fclose(file) == 0 && ok;
}

bool iz_ounce_apps_icon(u32 token, void *rgba, size_t size) {
    if (!rgba || !token || token > program_count || size != 96u * 96u * 4u) return false;
    const Program *program = &programs[token - 1];
    uint32_t *pixels = malloc(size);
    if (!pixels) return false;
    bool ok = read_module_icon(program->path, pixels, size);
    if (!ok) {
        FILE *file = fopen(program->path, "rb");
        if (file) {
            struct stat st;
            ok = !fstat(fileno(file), &st) && S_ISREG(st.st_mode) && st.st_size > 0 &&
                 installed_artwork(file, (uint64_t)st.st_size, false, pixels);
            if (fclose(file)) ok = false;
        }
    }
    if (ok) memcpy(rgba, pixels, size);
    free(pixels);
    return ok;
}

u32 iz_ounce_apps_remove(u32 token){
    IzOunceLaunch launch;
    u32 rc=iz_ounce_apps_prepare(token,&launch);
    if(rc)return rc;
    struct stat entry;
    if(lstat(launch.path,&entry))return errno;
    if(!S_ISREG(entry.st_mode))return EINVAL;
    char backup[512];
    if(module_sidecar(launch.path,"/.izstore-backup.nro",backup)&&
       !lstat(backup,&entry)&&S_ISREG(entry.st_mode)&&unlink(backup))return errno;
    if(unlink(launch.path))return errno;
    char icon[512];
    if(module_sidecar(launch.path,"/.izstore-icon.rgba",icon)&&!lstat(icon,&entry)&&S_ISREG(entry.st_mode))unlink(icon);
#ifdef __SWITCH2__
    return fsdevCommitDevice("sdmc");
#else
    return 0;
#endif
}
