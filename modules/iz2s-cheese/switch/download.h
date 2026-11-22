#ifndef IZ_STORE_DOWNLOAD_H
#define IZ_STORE_DOWNLOAD_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define RELEASE_URL_MAX 512
#define DOWNLOAD_MESSAGE 160

typedef bool (*DownloadSink)(void *, const void *, size_t);

bool github_release_url(const char *url, const char *extension);
bool catalog_url(const char *url);
bool github_probe(const char *url, char message[DOWNLOAD_MESSAGE]);
bool catalog_download(const char *url, uint64_t limit, DownloadSink sink, void *context,
                      char message[DOWNLOAD_MESSAGE]);
bool github_download(const char *url, uint64_t size, DownloadSink sink, void *context,
                     const atomic_bool *cancel, char message[DOWNLOAD_MESSAGE]);

#endif
