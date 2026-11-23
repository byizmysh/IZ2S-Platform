#define _POSIX_C_SOURCE 200809L
#include "download.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef __SWITCH2__
#include <switch.h>
#else
#include <openssl/ssl.h>
#endif

enum { IO_MS = 8000, REDIRECTS = 4, LOCATION_MAX = 4096, HEADER_LINE_MAX = 8192, BUFFER_SIZE = 16384 };

typedef struct {
    int fd;
    bool encrypted;
#ifdef __SWITCH2__
    SslContext context;
    SslConnection connection;
#else
    SSL_CTX *context;
    SSL *connection;
#endif
} Tls;

typedef struct {
    Tls *tls;
    const atomic_bool *cancel;
    uint64_t deadline;
    size_t offset, size;
    uint8_t buffer[BUFFER_SIZE];
} Reader;

typedef struct {
    unsigned status;
    bool counted, chunked, encoded;
    uint64_t length;
    char location[LOCATION_MAX];
} Response;

static bool error_message(char out[DOWNLOAD_MESSAGE], const char *message) {
    snprintf(out, DOWNLOAD_MESSAGE, "%s", message);
    return false;
}

static bool slug(const char *text, size_t size) {
    if (!size || (size == 1 && text[0] == '.') || (size == 2 && !memcmp(text, "..", 2))) return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
    }
    return true;
}

bool github_release_url(const char *url, const char *extension) {
    const char prefix[] = "https://github.com/";
    const char release[] = "/releases/download/";
    if (strlen(url) >= RELEASE_URL_MAX || strncmp(url, prefix, sizeof prefix - 1)) return false;
    const char *owner = url + sizeof prefix - 1;
    const char *repo = strchr(owner, '/');
    if (!repo || !slug(owner, (size_t)(repo - owner))) return false;
    ++repo;
    const char *rest = strchr(repo, '/');
    if (!rest || !slug(repo, (size_t)(rest - repo)) || strncmp(rest, release, sizeof release - 1)) return false;
    const char *tag = rest + sizeof release - 1;
    const char *file = strchr(tag, '/');
    if (!file || !slug(tag, (size_t)(file - tag))) return false;
    ++file;
    size_t size = strlen(file);
    if (!slug(file, size)) return false;
    const char *suffix = strrchr(file, '.');
    if (!suffix || suffix == file) return false;
    if (extension) return !strcasecmp(suffix + 1, extension);
    return !strcasecmp(suffix, ".nro") || !strcasecmp(suffix, ".iza") ||
           !strcasecmp(suffix, ".izdata");
}

typedef enum { RELEASE_GET, RELEASE_HEAD, CATALOG_GET, UPDATE_GET } Request;
static const char update_api[] = "https://api.github.com/repos/byizmysh/iz2s-cheese/releases/latest";

static const char *target(const char *url, Request request, char host[64], char port[6], bool *encrypted) {
    *encrypted = !strncmp(url, "https://", 8);
    if (!*encrypted && (request != CATALOG_GET || strncmp(url, "http://", 7))) return NULL;
    const char *name = url + (*encrypted ? 8 : 7);
    const char *slash = strchr(name, '/');
    if (!slash || slash == name || slash - name >= 64) return NULL;
    const char *colon = memchr(name, ':', (size_t)(slash - name));
    const char *end = colon ? colon : slash;
    memcpy(host, name, (size_t)(end - name));
    host[end - name] = 0;
    strcpy(port, *encrypted ? "443" : "80");
    if (*encrypted) {
        if (colon) return NULL;
        if (request == UPDATE_GET) {
            if (strcmp(url, update_api)) return NULL;
        } else if (request == CATALOG_GET) {
            if (strcmp(host, "raw.githubusercontent.com")) return NULL;
        } else if (strcmp(host, "github.com") && strcmp(host, "release-assets.githubusercontent.com") &&
                   strcmp(host, "objects.githubusercontent.com")) return NULL;
    } else {
        struct in_addr address;
        if (inet_pton(AF_INET, host, &address) != 1) return NULL;
        uint32_t ip = ntohl(address.s_addr);
        if ((ip >> 24) != 10 && (ip >> 20) != 0xac1 && (ip >> 16) != 0xc0a8) return NULL;
        if (colon) {
            size_t digits = (size_t)(slash - colon - 1);
            if (!digits || digits > 5 || strspn(colon + 1, "0123456789") != digits) return NULL;
            unsigned long value = strtoul(colon + 1, NULL, 10);
            if (!value || value > 65535) return NULL;
            memcpy(port, colon + 1, digits);
            port[digits] = 0;
        }
    }
    for (const unsigned char *ch = (const unsigned char *)slash; *ch; ++ch)
        if (*ch <= 32 || *ch > 126 || *ch == '\\' || *ch == '#') return NULL;
    return slash;
}

bool catalog_url(const char *url) {
    char host[64], port[6];
    bool encrypted;
    return strlen(url) < RELEASE_URL_MAX && target(url, CATALOG_GET, host, port, &encrypted);
}

bool catalog_same_origin(const char *left, const char *right) {
    char host[64], port[6], other_host[64], other_port[6];
    bool encrypted, other_encrypted;
    return target(left, CATALOG_GET, host, port, &encrypted) &&
        target(right, CATALOG_GET, other_host, other_port, &other_encrypted) &&
        encrypted == other_encrypted && !strcmp(host, other_host) &&
        strtoul(port, NULL, 10) == strtoul(other_port, NULL, 10);
}

static uint64_t milliseconds(void) {
    struct timespec clock;
    if (clock_gettime(CLOCK_MONOTONIC, &clock)) return 0;
    return (uint64_t)clock.tv_sec * 1000 + (uint64_t)clock.tv_nsec / 1000000;
}

static int connect_to(const char *host, const char *port, char out[DOWNLOAD_MESSAGE]) {
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_protocol = IPPROTO_TCP};
    struct addrinfo *addresses = NULL;
    int lookup = getaddrinfo(host, port, &hints, &addresses);
    if (lookup || !addresses) {
        if (lookup == EAI_NONAME)
            snprintf(out, DOWNLOAD_MESSAGE, "DNS: %s was not found. Check the DNS shown in Settings.", host);
        else {
#ifdef __SWITCH2__
            Result rc = resolverGetLastResult();
            snprintf(out, DOWNLOAD_MESSAGE, "DNS: %s failed (EAI %d, 0x%08x). Check Settings.", host, lookup, rc);
#else
            snprintf(out, DOWNLOAD_MESSAGE, "DNS: %s failed (EAI %d). Check the active network.", host, lookup);
#endif
        }
        return -1;
    }
    int fd = -1;
    unsigned attempts = 0;
    for (const struct addrinfo *addr = addresses; addr && attempts++ < 4; addr = addr->ai_next) {
        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd < 0) continue;
        bool connected = false;
        if (fcntl(fd, F_SETFL, O_NONBLOCK) == 0) {
            connected = connect(fd, addr->ai_addr, addr->ai_addrlen) == 0;
            if (!connected && errno == EINPROGRESS) {
                struct pollfd waiter = {.fd = fd, .events = POLLOUT};
                int error = 0;
                socklen_t size = sizeof error;
                connected = poll(&waiter, 1, IO_MS) > 0 &&
                    !getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) && !error;
            }
        }
        if (connected && fcntl(fd, F_SETFL, 0) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) snprintf(out, DOWNLOAD_MESSAGE, "Could not connect to %s:%s.", host, port);
    return fd;
}

static bool network_begin(bool encrypted, bool *owns_sockets, char out[DOWNLOAD_MESSAGE]) {
    *owns_sockets = false;
#ifdef __SWITCH2__
    Result rc = socketInitializeDefault();
    if (R_SUCCEEDED(rc)) {
        *owns_sockets = true;
    } else if (rc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)) {
        snprintf(out, DOWNLOAD_MESSAGE, "Could not start networking (0x%08x).", rc);
        return false;
    }
    // In hosted IZA, IZlauncher keeps the existing socket device alive for PC Link.
    // AlreadyInitialized does not acquire a reference to it.
    if (!encrypted) return true;
    rc = sslInitialize(1);
    if (R_FAILED(rc)) {
        if (*owns_sockets) socketExit();
        snprintf(out, DOWNLOAD_MESSAGE, "Could not start TLS (0x%08x).", rc);
        return false;
    }
#else
    (void)encrypted;
    (void)out;
#endif
    return true;
}

static void network_end(bool encrypted, bool owns_sockets) {
#ifdef __SWITCH2__
    if (encrypted) sslExit();
    if (owns_sockets) socketExit();
#else
    (void)encrypted;
    (void)owns_sockets;
#endif
}

static bool tls_handshake(Tls *tls, const char *host, char out[DOWNLOAD_MESSAGE]) {
#ifdef __SWITCH2__
    Result rc = sslCreateContext(&tls->context, SslVersion_TlsV12 | SslVersion_TlsV13);
    if (R_SUCCEEDED(rc)) rc = sslContextCreateConnection(&tls->context, &tls->connection);
    if (R_SUCCEEDED(rc)) rc = sslConnectionSetOption(&tls->connection, SslOptionType_DoNotCloseSocket, true);
    if (R_SUCCEEDED(rc)) {
        errno = 0;
        int fd = socketSslConnectionSetSocketDescriptor(&tls->connection, tls->fd);
        if (fd >= 0 && fd != tls->fd) close(fd);
        if (fd < 0 && errno != ENOENT) return error_message(out, "TLS could not attach to the network socket.");
        rc = sslConnectionSetHostName(&tls->connection, host, (u32)strlen(host) + 1);
    }
    if (R_SUCCEEDED(rc)) rc = sslConnectionSetVerifyOption(&tls->connection,
        SslVerifyOption_PeerCa | SslVerifyOption_HostName | SslVerifyOption_DateCheck);
    if (R_SUCCEEDED(rc)) rc = sslConnectionSetIoTimeout(&tls->connection, IO_MS);
    if (R_SUCCEEDED(rc)) rc = sslConnectionDoHandshake(&tls->connection, NULL, NULL, NULL, 0);
    if (R_SUCCEEDED(rc)) return true;
    snprintf(out, DOWNLOAD_MESSAGE, "GitHub TLS handshake failed (0x%08x).", rc);
    return false;
#else
    const struct timeval timeout = {.tv_sec = IO_MS / 1000};
    setsockopt(tls->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(tls->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    tls->context = SSL_CTX_new(TLS_client_method());
    if (!tls->context || !SSL_CTX_set_min_proto_version(tls->context, TLS1_2_VERSION) ||
        !SSL_CTX_set_default_verify_paths(tls->context)) return error_message(out, "Could not load TLS settings.");
    SSL_CTX_set_verify(tls->context, SSL_VERIFY_PEER, NULL);
    tls->connection = SSL_new(tls->context);
    return (tls->connection && SSL_set_fd(tls->connection, tls->fd) &&
            SSL_set_tlsext_host_name(tls->connection, host) && SSL_set1_host(tls->connection, host) &&
            SSL_connect(tls->connection) == 1) || error_message(out, "GitHub TLS handshake failed.");
#endif
}

static void tls_close(Tls *tls) {
#ifdef __SWITCH2__
    if (serviceIsActive(&tls->connection.s)) sslConnectionClose(&tls->connection);
    if (serviceIsActive(&tls->context.s)) sslContextClose(&tls->context);
#else
    SSL_free(tls->connection);
    SSL_CTX_free(tls->context);
#endif
    if (tls->fd >= 0) close(tls->fd);
}

static int tls_read(Tls *tls, void *buffer, size_t size) {
    if (!tls->encrypted) return (int)recv(tls->fd, buffer, size, 0);
#ifdef __SWITCH2__
    u32 got = 0;
    Result rc = sslConnectionRead(&tls->connection, buffer, (u32)size, &got);
    return R_FAILED(rc) || got > size ? -1 : (int)got;
#else
    return SSL_read(tls->connection, buffer, (int)size);
#endif
}

static bool tls_write(Tls *tls, const char *buffer, size_t size) {
    while (size) {
        if (!tls->encrypted) {
            ssize_t sent = send(tls->fd, buffer, size, 0);
            if (sent <= 0) return false;
            buffer += sent;
            size -= (size_t)sent;
            continue;
        }
#ifdef __SWITCH2__
        u32 sent = 0;
        if (R_FAILED(sslConnectionWrite(&tls->connection, buffer, (u32)size, &sent))) return false;
#else
        int sent = SSL_write(tls->connection, buffer, (int)size);
#endif
        if (sent <= 0 || (size_t)sent > size) return false;
        buffer += sent;
        size -= (size_t)sent;
    }
    return true;
}

static bool fill(Reader *reader) {
    if (reader->cancel && atomic_load(reader->cancel)) return false;
    if (reader->offset < reader->size) return true;
    if (milliseconds() >= reader->deadline) return false;
    int size = tls_read(reader->tls, reader->buffer, sizeof reader->buffer);
    if (size <= 0) return false;
    reader->offset = 0;
    reader->size = (size_t)size;
    return true;
}

static bool line(Reader *reader, char *text, size_t capacity, size_t *budget) {
    for (size_t used = 0; used < capacity && *budget; ++used) {
        if (!fill(reader)) return false;
        char ch = (char)reader->buffer[reader->offset++];
        --*budget;
        if (ch == '\n') {
            if (!used || text[used - 1] != '\r') return false;
            text[used - 1] = 0;
            return true;
        }
        if (!ch) return false;
        text[used] = ch;
    }
    return false;
}

static bool number(const char *text, unsigned base, char stop, uint64_t *value) {
    *value = 0;
    bool digit_seen = false;
    for (; *text && *text != stop; ++text) {
        unsigned digit = *text >= '0' && *text <= '9' ? (unsigned)(*text - '0') :
            *text >= 'a' && *text <= 'f' ? (unsigned)(*text - 'a' + 10) :
            *text >= 'A' && *text <= 'F' ? (unsigned)(*text - 'A' + 10) : base;
        if (digit >= base || *value > (UINT64_MAX - digit) / base) return false;
        *value = *value * base + digit;
        digit_seen = true;
    }
    return digit_seen;
}

static bool read_response(Reader *reader, Response *response) {
    size_t budget = 32768;
    char text[HEADER_LINE_MAX];
    if (!line(reader, text, sizeof text, &budget) || strlen(text) < 12 ||
        (strncmp(text, "HTTP/1.0 ", 9) && strncmp(text, "HTTP/1.1 ", 9))) return false;
    for (unsigned i = 9; i < 12; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        response->status = response->status * 10 + (unsigned)(text[i] - '0');
    }
    for (;;) {
        if (!line(reader, text, sizeof text, &budget)) return false;
        if (!*text) return !(response->chunked && response->counted);
        char *value = strchr(text, ':');
        if (!value) return false;
        *value++ = 0;
        value += strspn(value, " \t");
        size_t size = strlen(value);
        while (size && (value[size-1] == ' ' || value[size-1] == '\t')) value[--size] = 0;
        if (!strcasecmp(text, "location")) {
            if (response->location[0] || !size || size >= sizeof response->location) return false;
            memcpy(response->location, value, size + 1);
        } else if (!strcasecmp(text, "content-length")) {
            if (response->counted || !number(value, 10, 0, &response->length)) return false;
            response->counted = true;
        } else if (!strcasecmp(text, "transfer-encoding")) {
            if (response->chunked || strcasecmp(value, "chunked")) return false;
            response->chunked = true;
        } else if (!strcasecmp(text, "content-encoding") && strcasecmp(value, "identity")) response->encoded = true;
    }
}

static bool receive(Reader *reader, uint64_t amount, DownloadSink sink, void *context) {
    while (amount) {
        if (!fill(reader)) return false;
        size_t size = reader->size - reader->offset;
        if (size > amount) size = (size_t)amount;
        if (!sink(context, reader->buffer + reader->offset, size)) return false;
        reader->offset += size;
        amount -= size;
    }
    return true;
}

static bool body(Reader *reader, const Response *response, uint64_t amount, bool exact,
                 DownloadSink sink, void *context) {
    if (!response->chunked)
        return response->counted && response->length <= amount && (!exact || response->length == amount) &&
               receive(reader, response->length, sink, context);
    char text[192];
    size_t budget = 1024 * 1024;
    for (;;) {
        uint64_t size;
        if (!line(reader, text, sizeof text, &budget) || !number(text, 16, ';', &size) || size > amount) return false;
        if (!size) {
            do {
                if (!line(reader, text, sizeof text, &budget)) return false;
            } while (*text);
            return !exact || amount == 0;
        }
        if (!receive(reader, size, sink, context) || !line(reader, text, sizeof text, &budget) || *text) return false;
        amount -= size;
    }
}

static bool request_url(const char *url, Request mode, uint64_t size, DownloadSink sink, void *context,
                        const atomic_bool *cancel, char out[DOWNLOAD_MESSAGE]) {
    if (cancel && atomic_load(cancel)) return error_message(out, "Download cancelled.");
    bool catalog = mode == CATALOG_GET || mode == UPDATE_GET;
    bool site_probe = mode == RELEASE_HEAD && !url;
    if (site_probe) url = "https://github.com/";
    if (!site_probe && (mode == UPDATE_GET ? strcmp(url, update_api) != 0 :
        catalog ? !catalog_url(url) : !github_release_url(url, NULL)))
        return error_message(out, catalog ? "Use raw GitHub HTTPS or a private LAN HTTP URL." :
                                             "Use a direct GitHub release asset link.");
    bool encrypted = !strncmp(url, "https://", 8);
    bool owns_sockets;
    if (!network_begin(encrypted, &owns_sockets, out)) return false;
    Reader *reader = calloc(1, sizeof *reader);
    if (!reader) {
        network_end(encrypted, owns_sockets);
        return error_message(out, "Could not allocate a download buffer.");
    }
    reader->cancel = cancel;
    reader->deadline = milliseconds() + (mode == RELEASE_GET ? 180000 : 25000);
    char location[LOCATION_MAX], host[64], port[6];
    strcpy(location, url);
    bool ok = false;
    for (unsigned attempt = 0; attempt <= REDIRECTS; ++attempt) {
        if (cancel && atomic_load(cancel)) break;
        bool target_encrypted;
        const char *path = target(location, mode, host, port, &target_encrypted);
        if (!path || target_encrypted != encrypted) {
            error_message(out, "Server redirected to an unsupported URL.");
            break;
        }
        char request[LOCATION_MAX + 200];
        int length = snprintf(request, sizeof request, "%s %s HTTP/1.1\r\nHost: %s:%s\r\n"
            "User-Agent: IZ2S-Cheese\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            mode == RELEASE_HEAD ? "HEAD" : "GET", path, host, port);
        if (length < 0 || (size_t)length >= sizeof request) { error_message(out, "The download URL is too long."); break; }
        Tls tls = {.fd = connect_to(host, port, out), .encrypted = encrypted};
        if (tls.fd < 0) break;
        if (cancel && atomic_load(cancel)) { tls_close(&tls); break; }
        if (encrypted && !tls_handshake(&tls, host, out)) { tls_close(&tls); break; }
        if (!encrypted) {
            const struct timeval timeout = {.tv_sec = IO_MS / 1000};
            setsockopt(tls.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(tls.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        }
        reader->tls = &tls;
        reader->offset = reader->size = 0;
        Response response = {0};
        if (!tls_write(&tls, request, (size_t)length) || !read_response(reader, &response)) {
            tls_close(&tls);
            error_message(out, "Server returned an invalid or incomplete HTTP response.");
            break;
        }
        if (encrypted && (response.status == 301 || response.status == 302 || response.status == 303 ||
            response.status == 307 || response.status == 308)) {
            tls_close(&tls);
            strcpy(location, response.location);
            error_message(out, "GitHub download has too many redirects.");
            continue;
        }
        if (response.status == 200 && !response.encoded) {
            ok = mode == RELEASE_HEAD || body(reader, &response, size, !catalog, sink, context);
            if (!ok) error_message(out, "Download was interrupted or its size is invalid.");
        } else snprintf(out, DOWNLOAD_MESSAGE, "Server returned an unexpected response (HTTP %u).", response.status);
        tls_close(&tls);
        break;
    }
    free(reader);
    network_end(encrypted, owns_sockets);
    if (cancel && atomic_load(cancel)) return error_message(out, "Download cancelled.");
    if (ok) out[0] = '\0';
    return ok;
}

bool github_download(const char *url, uint64_t size, DownloadSink sink, void *context,
                     const atomic_bool *cancel, char out[DOWNLOAD_MESSAGE]) {
    return request_url(url, RELEASE_GET, size, sink, context, cancel, out);
}

bool github_probe(const char *url, char out[DOWNLOAD_MESSAGE]) {
    return request_url(url, RELEASE_HEAD, 0, NULL, NULL, NULL, out);
}

bool catalog_download(const char *url, uint64_t limit, DownloadSink sink, void *context, char out[DOWNLOAD_MESSAGE]) {
    return request_url(url, CATALOG_GET, limit, sink, context, NULL, out);
}

bool github_latest_release(DownloadSink sink, void *context, char out[DOWNLOAD_MESSAGE]) {
    return request_url(update_api, UPDATE_GET, 64u * 1024u, sink, context, NULL, out);
}
