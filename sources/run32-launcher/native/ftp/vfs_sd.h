#pragma once
#include <dirent.h>
#include <stdio.h>
#include <stdbool.h>

#define IZ_FTP_PATH 780

struct FtpVfsFile {
    FILE *fd;
    void *write_buffer;
    char staging[IZ_FTP_PATH];
    char target[IZ_FTP_PATH];
};
struct FtpVfsDir {DIR *fd;bool exchange_root;};
struct FtpVfsDirEntry {struct dirent *entry;};

int ftp_vfs_finish(struct FtpVfsFile *file);
/* Set before starting a listener and clear after stopping its worker. */
void ftp_vfs_file_exchange(bool enabled);
