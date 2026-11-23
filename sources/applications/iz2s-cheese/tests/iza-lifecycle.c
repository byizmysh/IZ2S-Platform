/* Run the actual ARM64 module through INIT/FRAME/STOP with host services.
 * Device calls are mocks; this exercises relocation and ownership, not TLS IPC. */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sb4/native-app.h>

typedef uint32_t (*Entry)(IzNativeApp *, uint32_t);
static const char *image_path, *missing;
static int service_revision, creates, joins, closes, loads, unloads, reports, completed;
static void (*worker)(void *);
static void *worker_arg, *image_code, *image_data;
static size_t code_size, data_size;
static char report[128];
static uint64_t held;
static unsigned keyboard_open, keyboard_show, keyboard_close, tls_open, tls_close, sockets_closed, dns_calls;
static uint64_t read64(const void *p) { uint64_t v; memcpy(&v,p,8); return v; }
static uint32_t read32(const void *p) { uint32_t v; memcpy(&v,p,4); return v; }
static unsigned char *read_file(const char *path, size_t *bytes) {
    FILE *f=fopen(path,"rb"); assert(f); assert(!fseek(f,0,SEEK_END));
    *bytes=(size_t)ftell(f); rewind(f); unsigned char *p=malloc(*bytes); assert(p);
    assert(fread(p,1,*bytes,f)==*bytes); assert(!fclose(f)); return p;
}
static uint32_t load(IzNativeApp *app,const char *name,IzNativeImage *out) {
    (void)app; assert(!strncmp(name,"asset-",6)); assert(!image_code); ++loads;
    size_t bytes; unsigned char *raw=read_file(image_path,&bytes);
    assert(bytes>=256&&!memcmp(raw,"IZNATRL1",8));
    code_size=read32(raw+48); data_size=read32(raw+52);
    image_code=mmap(NULL,code_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    image_data=mmap(NULL,data_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(image_code!=MAP_FAILED&&image_data!=MAP_FAILED);
    void *segments[]={image_code,image_data};
    for(unsigned i=0;i<2;++i) {
        uint64_t offset=read64(raw+56+8*i); size_t length=read32(raw+40+4*i);
        assert(offset<=bytes&&length<=bytes-offset); memcpy(segments[i],raw+offset,length);
    }
    uint64_t records=read64(raw+72); uint32_t count=read32(raw+80);
    assert(read32(raw+84)==32 && records+32ull*count<=bytes);
    for(unsigned i=0;i<count;++i) {
        unsigned char *r=raw+records+32*i; assert(r[4]==1&&!r[5]&&r[6]<2&&r[7]<2);
        uint32_t offset=read32(r); uint64_t target=read64(r+16);
        assert(read64((char*)segments[r[6]]+offset)==read64(r+8));
        uint64_t value=(uintptr_t)segments[r[7]]+target;
        assert(offset+8<=(r[6]?data_size:code_size)); memcpy((char*)segments[r[6]]+offset,&value,8);
    }
    __builtin___clear_cache(image_code,(char*)image_code+code_size);
    assert(!mprotect(image_code,code_size,PROT_READ|PROT_EXEC));
    *out=(IzNativeImage){.version=1,.bytes=sizeof(*out),.code=image_code,.code_bytes=code_size,
        .data=image_data,.data_bytes=data_size,.entry=(char*)image_code+read32(raw+100),.token=42};
    free(raw); return 0;
}
static uint32_t unload(IzNativeApp *app,uint64_t token) {
    (void)app; assert(token==42&&joins==creates&&closes==creates);
    ++unloads; munmap(image_code,code_size);munmap(image_data,data_size);image_code=image_data=NULL;return 0;
}
static uint32_t version(void) {return service_revision;}
static uint32_t create(void *t,void (*fn)(void*),void *arg,void *stack,size_t size,int priority,int core) {
    (void)t;(void)stack;(void)size;(void)priority;(void)core;
    worker=fn;worker_arg=arg;completed=0;++creates;return 0;
}
static uint32_t start(void *t) {(void)t;return 0;}
static uint32_t join(void *t) {(void)t;assert(completed);++joins;return 0;}
static uint32_t close_thread(void *t) {(void)t;++closes;return 0;}
static int no_file(void) {errno=ENOENT;return -1;}
static FILE *no_stream(const char *p,const char *m) {(void)p;(void)m;errno=ENOENT;return NULL;}
static uint32_t no_network(void) {return 1;}
static int no_touch(void *state,size_t count) {(void)state;(void)count;return 0;}
static uint32_t keyboard_create(void *config,int count) {(void)config;(void)count;++keyboard_open;return 0;}
static uint32_t keyboard_display(void *config,char *text,size_t count) {(void)config;(void)text;(void)count;++keyboard_show;return 1;}
static void keyboard_end(void *config) {(void)config;++keyboard_close;}
static void config_ignore(void) {}
static uint32_t borrowed_sockets(void) {return 345u+(7u<<9);}
static void socket_end(void) {++sockets_closed;}
static uint32_t tls_begin(void) {++tls_open;return 0;}
static void tls_end(void) {++tls_close;}
static int dns_failure(void) {++dns_calls;return 7;}
static uint32_t dns_result(void) {return 0;}
static void unexpected(void) {assert(!"Unexpected device import called");}
static int *get_errno(void) {return &errno;}
static Sb4CFunction resolve(const char *name) {
    if(missing&&!strcmp(name,missing))return NULL;
#define MAP(n,f) if(!strcmp(name,n))return (Sb4CFunction)(f)
    MAP("sb4NativeServicesVersion",service_revision?version:NULL);
    MAP("__errno",get_errno);MAP("threadCreate",create);MAP("threadStart",start);
    MAP("threadWaitForExit",join);MAP("threadClose",close_thread);
    MAP("lstat",no_file);MAP("stat",no_file);MAP("access",no_file);MAP("fopen",no_stream);
    MAP("swkbdCreate",keyboard_create);MAP("swkbdShow",keyboard_display);MAP("swkbdClose",keyboard_end);
    MAP("swkbdConfigMakePresetDefault",config_ignore);MAP("swkbdConfigSetGuideText",config_ignore);
    MAP("swkbdConfigSetInitialText",config_ignore);MAP("socketInitialize",borrowed_sockets);
    MAP("socketExit",socket_end);MAP("sslInitialize",tls_begin);MAP("sslExit",tls_end);
    MAP("getaddrinfo",dns_failure);MAP("resolverGetLastResult",dns_result);
    MAP("nifmInitialize",no_network);MAP("hidGetTouchScreenStates",no_touch);
#undef MAP
    void *symbol=dlsym(RTLD_DEFAULT,name);
    return symbol?(Sb4CFunction)symbol:unexpected;
}
static uint32_t write_report(IzNativeApp *app,const char *name,const void *data,uint32_t bytes) {
    (void)app;(void)name;++reports;assert(bytes<sizeof report);memcpy(report,data,bytes);report[bytes]=0;return 0;
}
static uint64_t buttons(IzNativeApp *app) {(void)app;return held;}
static void request_exit(IzNativeApp *app) {(void)app;}
static IzServiceFunction service(uint32_t a,uint32_t b) {(void)a;(void)b;return NULL;}
static uint32_t allocate(void **p,size_t a,size_t n) {return (uint32_t)posix_memalign(p,a,n);}
static uint64_t tick(void) {return 1;}
static void sleep_ns(uint64_t n) {(void)n;}
static void press(Entry entry,IzNativeApp *app,uint64_t key) {
    held=key;assert(!entry(app,IZ_NATIVE_FRAME));held=0;assert(!entry(app,IZ_NATIVE_FRAME));
}
int main(int argc,char **argv) {
    assert(argc==4); image_path=argv[2];
    size_t bytes; unsigned char *bin=read_file(argv[1],&bytes);
    void *code=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(code!=MAP_FAILED);memcpy(code,bin,bytes);free(bin);
    __builtin___clear_cache(code,(char*)code+bytes);assert(!mprotect(code,bytes,PROT_READ|PROT_EXEC));
    Entry entry=(Entry)code;
    Sb4Compat api={.version=1,.bytes=sizeof api,.backend=1,.resolve_c=resolve,.resolve_service=service,
        .allocate_aligned=allocate,.release=free,.ticks=tick,.tick_hz=tick,.sleep_ns=sleep_ns};
    IzNativeApp7 module={0}; IzNativeApp *app=(IzNativeApp*)&module;
    module.sb4=&api; module.base.base.base.base.load_image=load;
    module.base.base.base.base.unload_image=unload;
    app->magic=0x414e5a49;app->version=7;app->bytes=sizeof module;app->data=calloc(1,4096);app->data_bytes=4096;
    app->controls=calloc(1,0x800000);app->controls_bytes=0x800000;
    app->write_file=write_report;app->buttons=buttons;app->request_exit=request_exit;
    unsigned char *art=read_file(argv[3],&bytes);assert(bytes<0x200000-1024);
    memcpy((char*)app->controls+1024,art,bytes);free(art);
    missing="sslConnectionDoHandshake";
    assert(entry(app,IZ_NATIVE_INIT)==0xe872);assert(reports==1&&!strcmp(report,missing));
    assert(!entry(app,IZ_NATIVE_STOP)&&loads==unloads&&!creates);
    missing="__assert_func";
    for(service_revision=0;service_revision<3;++service_revision) {
        memset(app->data,0,4096);assert(!entry(app,IZ_NATIVE_INIT));
        assert(entry(app,IZ_NATIVE_STOP)==IZ_NATIVE_STOP_PENDING);assert(loads==unloads+1);
        worker(worker_arg);completed=1;
        assert(!entry(app,IZ_NATIVE_STOP));assert(loads==unloads&&creates==joins&&joins==closes);
        missing=NULL;
    }
    memset(app->data,0,4096);assert(!entry(app,IZ_NATIVE_INIT));
    worker(worker_arg);completed=1;assert(!entry(app,IZ_NATIVE_FRAME));
    press(entry,app,1u<<2);press(entry,app,1u<<2); /* Catalogs > Add URL */
    assert(keyboard_open==1&&keyboard_show==1&&keyboard_close==1);
    press(entry,app,1u<<1);
    press(entry,app,1u<<11); /* Settings */
    press(entry,app,1u<<15);press(entry,app,1u<<15);press(entry,app,1u); /* Test GitHub */
    worker(worker_arg);completed=1;assert(!entry(app,IZ_NATIVE_FRAME));
    assert(tls_open==1&&tls_close==1&&dns_calls==1&&sockets_closed==0);
    assert(!entry(app,IZ_NATIVE_STOP));assert(loads==unloads&&creates==joins&&joins==closes);
    free(app->data);free(app->controls);
    puts("ARM64 module: relocation, service discovery, missing import, cancellation, repeated startup, keyboard, DNS failure, and socket/TLS cleanup PASS");
}
