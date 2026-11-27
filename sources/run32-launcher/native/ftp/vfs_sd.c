#include "ftpsrv_vfs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifdef __SWITCH__
#define FTP_SD "sdmc:"
#else
#define FTP_SD "."
#endif

static unsigned generation;
#define FTP_WRITE_BUFFER_SIZE (1024u * 1024u)
static bool exchange;
static const char exchange_root[]=FTP_SD "/iz2s/modules/iz-exchange";

#ifdef IZ_FTP_NOTIFY_CATALOG
extern void iz_ounce_apps_sd_changed(const char *path,bool may_move_directory);
#else
static inline void iz_ounce_apps_sd_changed(const char *path,bool may_move_directory){
    (void)path;
    (void)may_move_directory;
}
#endif

void ftp_vfs_file_exchange(bool enabled){exchange=enabled;}

static bool exchange_folder(const char *name,size_t length){
    return (length==4&&!memcmp(name,"save",4))||(length==3&&(!memcmp(name,"tmp",3)||!memcmp(name,"rom",3)));
}

static bool visible(const char *name){
    return strcasecmp(name,"pc-link.key")&&strncasecmp(name,".pc-delivery.",13)&&
           strncasecmp(name,".ftp-stage-",11);
}

static int resolve(const char *path,char out[IZ_FTP_PATH],bool modifying){
    if(!path||path[0]!='/'||strlen(path)>768||(modifying&&!path[1]))goto invalid;
    if(exchange&&path[1]){
        const size_t length=strcspn(path+1,"/");
        if(!exchange_folder(path+1,length)||
           (modifying&&(!path[length+1]||!path[length+2]||(length==3&&!memcmp(path+1,"rom",3)))))goto invalid;
    }
    const char *root=exchange?exchange_root:FTP_SD;
    const size_t root_bytes=strlen(root);
    memcpy(out,root,root_bytes+1);
    size_t offset=root_bytes;
#ifndef __SWITCH__
    if(exchange){
        for(size_t i=2;i<=root_bytes;i++)if(!out[i]||out[i]=='/'){
            const char suffix=out[i];
            out[i]=0;
            struct stat st;
            const bool symlink=!lstat(out,&st)&&S_ISLNK(st.st_mode);
            out[i]=suffix;
            if(symlink)goto invalid;
        }
    }
#endif
    for(const char *p=path+1;*p;){
        size_t n=strcspn(p,"/");
        if(!n||n>255)goto invalid;
        char name[256];
        memcpy(name,p,n);name[n]=0;
        if(!strcmp(name,".")||!strcmp(name,"..")||!visible(name))goto invalid;
        for(size_t i=0;i<n;++i){
            unsigned char ch=(unsigned char)name[i];
            if(ch<32||ch==127||ch==':'||ch=='\\')goto invalid;
        }
        if(offset+1+n>=IZ_FTP_PATH){errno=ENAMETOOLONG;return -1;}
        out[offset++]='/';memcpy(out+offset,name,n+1);offset+=n;
#ifndef __SWITCH__
        /* POSIX test roots, unlike the console's FAT SD, can contain symlinks. */
        struct stat st;
        if(!lstat(out,&st)&&S_ISLNK(st.st_mode))goto invalid;
#endif
        p+=n;
        if(*p=='/')++p;
    }
    if(!exchange&&offset==root_bytes)out[offset++]='/';
    out[offset]=0;
    return 0;
invalid:
    errno=EACCES;
    return -1;
}

int ftp_vfs_open(struct FtpVfsFile *file,const char *path,enum FtpVfsOpenMode mode){
    file->write_buffer=NULL;
    if(resolve(path,file->target,mode!=FtpVfsOpenMode_READ))return -1;
    if(mode==FtpVfsOpenMode_READ){
        file->fd=fopen(file->target,"rb");
        return file->fd?0:-1;
    }
    if(mode!=FtpVfsOpenMode_WRITE){errno=ENOTSUP;return -1;}
    size_t parent=(size_t)(strrchr(file->target,'/')-file->target)+1;
    for(unsigned tries=0;tries<128;tries++){
        int n=snprintf(file->staging,sizeof file->staging,"%.*s.ftp-stage-%08x",
                       (int)parent,file->target,++generation);
        if(n<0||(size_t)n>=sizeof file->staging){errno=ENAMETOOLONG;break;}
        int fd=open(file->staging,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0){if(errno==EEXIST)continue;break;}
        file->fd=fdopen(fd,"wb");
        if(file->fd){
            /* Coalesce short network reads before writing to the SD card. */
            file->write_buffer=memalign(4096,FTP_WRITE_BUFFER_SIZE);
            if(file->write_buffer&&setvbuf(file->fd,file->write_buffer,_IOFBF,FTP_WRITE_BUFFER_SIZE)){
                free(file->write_buffer);
                file->write_buffer=NULL;
            }
            return 0;
        }
        close(fd);
        unlink(file->staging);
        break;
    }
    file->staging[0]=0;
    return -1;
}

int ftp_vfs_read(struct FtpVfsFile *file,void *buf,size_t size){
    size_t got=fread(buf,1,size,file->fd);
    return ferror(file->fd)?-1:(int)got;
}
int ftp_vfs_write(struct FtpVfsFile *file,const void *buf,size_t size){
    return fwrite(buf,1,size,file->fd)==size?(int)size:-1;
}
int ftp_vfs_seek(struct FtpVfsFile *file,const void *buf,size_t size,size_t offset){
    (void)buf;(void)size;
    if(file->staging[0]||offset>LONG_MAX){errno=EINVAL;return -1;}
    return fseek(file->fd,(long)offset,SEEK_SET);
}
int ftp_vfs_close(struct FtpVfsFile *file){
    int rc=file->fd?fclose(file->fd):0;
    file->fd=NULL;
    free(file->write_buffer);
    file->write_buffer=NULL;
    if(file->staging[0]){unlink(file->staging);file->staging[0]=0;}
    return rc;
}
int ftp_vfs_finish(struct FtpVfsFile *file){
    if(!file->fd||!file->staging[0])return -1;
    int rc=fclose(file->fd);
    file->fd=NULL;
    free(file->write_buffer);
    file->write_buffer=NULL;
    if(!rc)rc=rename(file->staging,file->target);
    if(rc)unlink(file->staging);
    else iz_ounce_apps_sd_changed(file->target,false);
    file->staging[0]=0;
    return rc;
}
int ftp_vfs_isfile_open(struct FtpVfsFile *file){return file->fd!=NULL;}

int ftp_vfs_opendir(struct FtpVfsDir *dir,const char *path){
    char name[IZ_FTP_PATH];
    if(resolve(path,name,false))return -1;
    dir->fd=opendir(name);
    dir->exchange_root=exchange&&!path[1];
    return dir->fd?0:-1;
}
const char *ftp_vfs_readdir(struct FtpVfsDir *dir,struct FtpVfsDirEntry *entry){
    while((entry->entry=readdir(dir->fd))){
        const char *name=entry->entry->d_name;
        if(strcmp(name,".")&&strcmp(name,"..")&&visible(name)&&
           (!dir->exchange_root||exchange_folder(name,strlen(name))))return name;
    }
    return NULL;
}
int ftp_vfs_closedir(struct FtpVfsDir *dir){
    int rc=dir->fd?closedir(dir->fd):0;
    dir->fd=NULL;
    return rc;
}
int ftp_vfs_isdir_open(struct FtpVfsDir *dir){return dir->fd!=NULL;}
int ftp_vfs_lstat(const char *path,struct stat *st){char name[IZ_FTP_PATH];return resolve(path,name,false)?-1:lstat(name,st);}
int ftp_vfs_stat(const char *path,struct stat *st){return ftp_vfs_lstat(path,st);}
int ftp_vfs_dirlstat(struct FtpVfsDir *dir,const struct FtpVfsDirEntry *entry,const char *path,struct stat *st){
    (void)dir;(void)entry;
    return ftp_vfs_lstat(path,st);
}
int ftp_vfs_mkdir(const char *path){char name[IZ_FTP_PATH];return resolve(path,name,true)?-1:mkdir(name,0777);}
int ftp_vfs_unlink(const char *path){
    char name[IZ_FTP_PATH];
    if(resolve(path,name,true)||unlink(name))return -1;
    iz_ounce_apps_sd_changed(name,false);
    return 0;
}
int ftp_vfs_rmdir(const char *path){char name[IZ_FTP_PATH];return resolve(path,name,true)?-1:rmdir(name);}
int ftp_vfs_rename(const char *from,const char *to){
    char source[IZ_FTP_PATH],dest[IZ_FTP_PATH];
    if(resolve(from,source,true)||resolve(to,dest,true)||rename(source,dest))return -1;
    iz_ounce_apps_sd_changed(source,true);
    iz_ounce_apps_sd_changed(dest,true);
    return 0;
}
int ftp_vfs_readlink(const char *path,char *buf,size_t bytes){(void)path;(void)buf;(void)bytes;return -1;}
const char *ftp_vfs_getpwuid(const struct stat *st){(void)st;return "iz2s";}
const char *ftp_vfs_getgrgid(const struct stat *st){(void)st;return "iz2s";}
