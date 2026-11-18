#define _POSIX_C_SOURCE 200809L
#include "../legacy/native/module-folder.h"
#include "import.h"
#include "json.h"
#include "../legacy/native/abi.h"
#include "../../deps/zlib/contrib/minizip/unzip.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __SWITCH2__
#include <switch/crypto/sha256.h>
#include <switch/runtime/devices/fs_dev.h>
#else
#include <openssl/evp.h>
#endif

#define BAD 0xe880u
#define UNSUPPORTED 0xe881u
#define CRC_ERROR 0xe882u
#define HASH_ERROR 0xe883u
#define MIB (1024u * 1024u)
#define MAX_COMPANIONS 16
#define OFFLINE_BUNDLE_LIMIT MIB
#define OFFLINE_LAYER_LIMIT UINT32_MAX

typedef struct { u8 *data; u32 bytes; } Blob;
typedef struct {
    unzFile archive, overlay;
    Json *checksums;
    int hashes;
} ZipSource;
typedef struct {
    char source[512];
    char name[80];
    char hash[65];
    u32 bytes;
} Companion;
typedef struct {
    IzAppHeader header;
    Blob image;
    Blob sections[SEC_COUNT];
    Companion companions[MAX_COMPANIONS];
    unsigned companion_count;
} Package;

static int safe_path(const char *path) {
    size_t length = strlen(path);
    if (!length || length > 511 || path[0] == '/' || path[length - 1] == '/') return 0;
    for (size_t i = 0; i < length; ++i) {
        size_t start = i;
        while (i < length && path[i] != '/') {
            unsigned ch = (unsigned char)path[i++];
            if (ch < 32 || ch == 127 || ch == ':' || ch == '\\') return 0;
        }
        size_t bytes = i - start;
        if (!bytes || (bytes == 1 && path[start] == '.') ||
            (bytes == 2 && path[start] == '.' && path[start + 1] == '.')) return 0;
    }
    return 1;
}

static int id_valid(const char *id) {
    size_t bytes = strlen(id);
    if (!bytes || bytes > 47) return 0;
    for (size_t i = 0; i < bytes; ++i) {
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') ||
              (i && id[i] == '-'))) return 0;
    }
    return 1;
}

static u32 zip_index(unzFile zip) {
    unz_global_info64 global;
    if (unzGetGlobalInfo64(zip, &global) != UNZ_OK || !global.number_entry ||
        global.number_entry > 1024) return BAD;
    char (*names)[512] = calloc((size_t)global.number_entry, 512);
    if (!names) return ENOMEM;
    u32 rc = 0;
    u64 total = 0;
    int cursor = unzGoToFirstFile(zip);
    for (u32 i = 0; i < global.number_entry; ++i) {
        unz_file_info64 info;
        if (cursor != UNZ_OK ||
            unzGetCurrentFileInfo64(zip, &info, names[i], 512, NULL, 0, NULL, 0) != UNZ_OK ||
            !info.size_filename || info.size_filename >= 512 ||
            strlen(names[i]) != info.size_filename) {
            rc = BAD;
            break;
        }
        size_t bytes = strlen(names[i]);
        if (names[i][bytes - 1] == '/') names[i][bytes - 1] = 0;
        unsigned mode = (info.external_fa >> 16) & 0170000;
        if (!safe_path(names[i]) || (info.flag & 1) ||
            (info.compression_method != 0 && info.compression_method != 8) ||
            (mode && mode != 0100000 && mode != 0040000) ||
            info.uncompressed_size > UINT32_MAX ||
            (total += info.uncompressed_size) > UINT32_MAX) {
            rc = BAD;
            break;
        }
        for (u32 j = 0; j < i; ++j) {
            if (!strcasecmp(names[j], names[i])) { rc = BAD; break; }
        }
        if (rc) break;
        cursor = unzGoToNextFile(zip);
    }
    free(names);
    return rc;
}

static u32 zip_entry(unzFile zip, const char *name, u32 limit, u32 *size) {
    if (!safe_path(name) || unzLocateFile(zip, name, 1) != UNZ_OK) return BAD;
    unz_file_info64 info;
    if (unzGetCurrentFileInfo64(zip, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK ||
        info.uncompressed_size > limit) return BAD;
    *size = (u32)info.uncompressed_size;
    return 0;
}

static u32 zip_blob(unzFile zip, const char *name, u32 limit, Blob *out) {
    u32 size;
    u32 rc = zip_entry(zip, name, limit, &size);
    if (rc) return rc;
    u8 *data = malloc((size_t)size + 1);
    if (!data) return ENOMEM;
    if (unzOpenCurrentFile(zip) != UNZ_OK) { free(data); return BAD; }
    u32 at = 0;
    while (at < size) {
        u32 count = size - at;
        if (count > 65536) count = 65536;
        int got = unzReadCurrentFile(zip, data + at, count);
        if (got <= 0) { rc = BAD; break; }
        at += (u32)got;
    }
    u8 extra;
    if (!rc && unzReadCurrentFile(zip, &extra, 1) != 0) rc = BAD;
    if (unzCloseCurrentFile(zip) != UNZ_OK) rc = CRC_ERROR;
    if (rc) { free(data); return rc; }
    data[size] = 0;
    *out = (Blob){ data, size };
    return 0;
}

static u32 file_blob(const char *name, u32 limit, Blob *out) {
    FILE *file = fopen(name, "rb");
    if (!file) return errno;
    u32 rc = BAD;
    struct stat info;
    if (!fstat(fileno(file), &info) && S_ISREG(info.st_mode) &&
        info.st_size > 0 && (u64)info.st_size <= limit) {
        out->bytes = (u32)info.st_size;
        out->data = malloc(out->bytes);
        if (!out->data) rc = ENOMEM;
        else if (fread(out->data, 1, out->bytes, file) == out->bytes) rc = 0;
    }
    if (fclose(file) && !rc) rc = EIO;
    return rc;
}

static int asset_path(Json *json, int node, const char *base, char out[512]) {
    char relative[512];
    if (jstr(json, node, relative, sizeof relative) || !safe_path(relative)) return 0;
    int written = snprintf(out, 512, "%s%s", base, relative);
    return written >= 0 && written < 512;
}

static u32 private_prefix(u32 used, u32 total) {
    u64 aligned = ((u64)used + 3) & ~3ull;
    return aligned < total ? aligned : total;
}

static u32 inflate_blob(Blob *blob, u32 limit, int trim) {
    // The final GPU memory is zeroed by the loader. Retain only the
    // initialized prefix while validating the full decompressed length.
    Blob decoded = {0};
    u32 capacity = 0, total = 0;
    u8 buffer[16384];
    z_stream stream = { .next_in = blob->data, .avail_in = blob->bytes };
    if (inflateInit(&stream) != Z_OK) return BAD;
    u32 rc = 0;
    for (;;) {
        stream.next_out = buffer;
        stream.avail_out = sizeof buffer;
        int status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) { rc = BAD; break; }
        u32 count = (u32)(sizeof buffer - stream.avail_out);
        if (count > limit - total) { rc = BAD; break; }
        u32 used = count;
        if (trim) while (used && !buffer[used - 1]) --used;
        u32 end = total + used;
        if (used) {
            if (end > UINT32_MAX) { rc = BAD; break; }
            if (end > capacity) {
                u64 next = capacity ? (u64)capacity * 2 : sizeof buffer;
                if (next < end) next = end;
                if (next > UINT32_MAX) next = UINT32_MAX;
                u8 *data = realloc(decoded.data, next);
                if (!data) { rc = ENOMEM; break; }
                decoded.data = data;
                capacity = next;
            }
            if (total > decoded.bytes) memset(decoded.data + decoded.bytes, 0, total - decoded.bytes);
            memcpy(decoded.data + total, buffer, used);
            decoded.bytes = end;
        }
        total += count;
        if (status == Z_STREAM_END) {
            if (stream.avail_in) rc = BAD;
            break;
        }
        if (!count && !stream.avail_in) { rc = BAD; break; }
    }
    inflateEnd(&stream);
    if (!rc && trim) {
        u32 retained = private_prefix(decoded.bytes, total);
        if (retained > capacity) {
            u8 *data = realloc(decoded.data, retained);
            if (data) decoded.data = data;
            else rc = ENOMEM;
        }
        if (!rc && retained > decoded.bytes) {
            memset(decoded.data + decoded.bytes, 0, retained - decoded.bytes);
            decoded.bytes = retained;
        }
    }
    if (rc) { free(decoded.data); return rc; }
    free(blob->data);
    *blob = decoded;
    return 0;
}

static int ipv4(const char *value, u32 *out) {
    u32 address = 0;
    for (unsigned octet = 0; octet < 4; ++octet) {
        if (*value < '0' || *value > '9' ||
            (*value == '0' && value[1] >= '0' && value[1] <= '9')) return 0;
        u32 part = 0;
        unsigned digits = 0;
        while (*value >= '0' && *value <= '9') {
            if (++digits > 3) return 0;
            part = part * 10 + (u32)(*value++ - '0');
        }
        if (part > 255 || *value != (octet == 3 ? '\0' : '.')) return 0;
        address = (address << 8) | part;
        if (octet < 3) ++value;
    }
    *out = address;
    return 1;
}

static u32 sdk_metadata(Json *json, int sdk, IzAppHeader *header) {
    if (sdk == -1) return 0;
    u32 version;
    if (ju32(json, jkey(json, sdk, "version"), &version) || version != 1 ||
        !jempty(json, jkey(json, sdk, "services")) || jkey(json, sdk, "ftpPort") != -1)
        return UNSUPPORTED;
    int capabilities = jkey(json, sdk, "capabilities");
    if (capabilities < 0 || json->nodes[capabilities].kind != '[') return BAD;
    u32 config[28] = { 0x314b4453, 1 };
    for (int i = capabilities + 1; i < json->nodes[capabilities].next; i = json->nodes[i].next) {
        u32 bit;
        if (jeq(json, i, "udp")) bit = 1;
        else if (jeq(json, i, "vibration")) bit = 2;
        else return UNSUPPORTED;
        if (config[2] & bit) return BAD;
        config[2] |= bit;
    }
    int endpoints = jkey(json, sdk, "endpoints");
    if (endpoints >= 0) {
        if (json->nodes[endpoints].kind != '[' || json->nodes[endpoints].count > 8) return BAD;
        config[3] = (u32)json->nodes[endpoints].count;
        if (config[3] && !(config[2] & 1)) return BAD;
        unsigned at = 4;
        for (int i = endpoints + 1; i < json->nodes[endpoints].next; i = json->nodes[i].next) {
            char address[16];
            if (jstr(json, jkey(json, i, "address"), address, sizeof address) ||
                !ipv4(address, &config[at]) || ju32(json, jkey(json, i, "port"), &config[at + 1]) ||
                !config[at + 1] || config[at + 1] > 65535) return BAD;
            at += 2;
        }
    }
    if (config[2]) {
        header->flags |= IZ_APP_SDK;
        config[20] = iz_crc(config, 80);
        memcpy(header->padding + 32, config, sizeof config);
    }
    return 0;
}

static int sha256_bytes(u8 digest[32], const void *data, size_t size) {
#ifdef __SWITCH2__
    sha256CalculateHash(digest, data, size);
    return 1;
#else
    unsigned out_size;
    return EVP_Digest(data, size, digest, &out_size, EVP_sha256(), NULL) && out_size == 32;
#endif
}

static void digest_text(const u8 hash[32], char text[65]) {
    const char *hex = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        text[i * 2] = hex[hash[i] >> 4];
        text[i * 2 + 1] = hex[hash[i] & 15];
    }
    text[64] = 0;
}

static unzFile zip_reader(const ZipSource *source, const char *name) {
    if (source->overlay && unzLocateFile(source->overlay, name, 1) == UNZ_OK) return source->overlay;
    return source->archive;
}

static u32 source_blob(const ZipSource *source, const char *name, u32 limit, Blob *out) {
    unzFile reader = zip_reader(source, name);
    u32 rc = zip_blob(reader, name, limit, out);
    if (rc || !source->overlay || reader == source->overlay) return rc;
    // The SD archive can change after the adapter lookup. Authenticate each
    // resource read from it against the manifest in the launcher RomFS.
    char expected[65], actual[65];
    u8 digest[32];
    if (jstr(source->checksums, jkey(source->checksums, source->hashes, name),
             expected, sizeof expected) || !sha256_bytes(digest, out->data, out->bytes)) return HASH_ERROR;
    digest_text(digest, actual);
    return strcmp(expected, actual) ? HASH_ERROR : 0;
}

static u32 asset(const ZipSource *source, Json *json, int node, const char *base,
                 u32 limit, Blob *out, int optional) {
    if (node == -1 && optional) return 0;
    char name[512];
    if (!asset_path(json, node, base, name)) return BAD;
    return source_blob(source, name, limit, out);
}

static int companion_order(const void *a, const void *b) {
    return strcmp(((const Companion *)a)->name, ((const Companion *)b)->name);
}

static u32 companion_binding(Companion *entries, unsigned count, u8 digest[32]) {
    char identities[4096] = "iz2s.sd-assets.v1\n[";
    size_t at = strlen(identities);
    for (unsigned i = 0; i < count; ++i) {
        const Companion *entry = &entries[i];
        if (i && !strcmp(entry->name, entries[i - 1].name)) return BAD;
        int bytes = snprintf(identities + at, sizeof identities - at, "%s[\"%s\",%u,\"%s\"]",
                             i ? "," : "", entry->name, entry->bytes, entry->hash);
        if (bytes < 0 || (size_t)bytes >= sizeof identities - at - 1) return BAD;
        at += (size_t)bytes;
    }
    identities[at++] = ']';
    return sha256_bytes(digest, identities, at) ? 0 : HASH_ERROR;
}

static u32 companion_metadata(const ZipSource *source, Json *json, int assets, const char *base,
                              Package *package) {
    if (assets == -1) return 0;
    if (json->nodes[assets].kind != '[' || json->nodes[assets].count > MAX_COMPANIONS) return BAD;
    if (json->nodes[assets].count && package->header.version != 2 && package->header.version != 3)
        return UNSUPPORTED;
    u64 total = 0;
    for (int i = assets + 1; i < json->nodes[assets].next; i = json->nodes[i].next) {
        Companion *entry = &package->companions[package->companion_count];
        if (json->nodes[i].kind != '{' || json->nodes[i].count != 4 ||
            !asset_path(json, jkey(json, i, "file"), base, entry->source) ||
            jstr(json, jkey(json, i, "path"), entry->name, sizeof entry->name) ||
            jstr(json, jkey(json, i, "sha256"), entry->hash, sizeof entry->hash) ||
            strlen(entry->hash) != 64 || strspn(entry->hash, "0123456789abcdef") != 64 ||
            ju32(json, jkey(json, i, "bytes"), &entry->bytes) ||
            !entry->bytes || entry->bytes > UINT32_MAX) return BAD;
        char expected[80];
        snprintf(expected, sizeof expected, "asset-%s.izdata", entry->hash);
        if (strcmp(expected, entry->name)) return BAD;
        if (source) {
            u32 size;
            if (zip_entry(zip_reader(source, entry->source), entry->source, UINT32_MAX, &size) ||
                size != entry->bytes) return BAD;
        } else if (strcmp(entry->source, entry->name)) return BAD;
        total += entry->bytes;
        if (total > UINT32_MAX) return BAD;
        ++package->companion_count;
    }
    if (!package->companion_count) return 0;
    qsort(package->companions, package->companion_count, sizeof(Companion), companion_order);
    IzAppHeader *header = &package->header;
    u32 rc = companion_binding(package->companions, package->companion_count, header->padding);
    if (rc) return rc;
    header->flags |= IZ_APP_ASSETS;
    header->reserved[1] = package->companion_count;
    header->reserved[2] = (u32)total;
    return 0;
}

static u32 native_metadata(const ZipSource *source, Json *json, int node, const char *base, Package *package) {
    if (node == -1) return 0;
    IzAppHeader *header = &package->header;
    u32 abi, data_bytes;
    if (!jeq(json, jkey(json, node, "format"), "iz2s.native-app") ||
        ju32(json, jkey(json, node, "abi"), &abi) ||
        (abi != 7 && (abi < 1 || abi > 4))) return UNSUPPORTED;
    if (ju32(json, jkey(json, node, "dataBytes"), &data_bytes) ||
        !data_bytes || data_bytes > 32u * MIB || data_bytes % 4096) return BAD;
    if (abi == 1 && (header->repeat || header->version != 1)) return UNSUPPORTED;
    if (abi >= 2) header->version = 2;
    header->flags |= IZ_APP_NATIVE;
    if (abi == 3 || abi == 4) header->flags |= IZ_APP_ENGINE_IMAGE;
    if (abi == 4) header->flags |= IZ_APP_COOPERATIVE;
    if (abi == 7) header->flags |= IZ_APP_SB4_COMPAT;
    header->reserved[0] = data_bytes;
    int list = jkey(json, node, "capabilities");
    if (list >= 0) {
        if (abi == 1 || json->nodes[list].kind != '[') return BAD;
        u32 seen = 0;
        static const struct { const char *name; u32 flags; } names[] = {
            { "audio-out", IZ_APP_AUDIO_OUT },
            { "engine-image", IZ_APP_ENGINE_IMAGE },
            { "cooperative-image", IZ_APP_COOPERATIVE | IZ_APP_ENGINE_IMAGE },
            { "worker", IZ_APP_WORKER | IZ_APP_COOPERATIVE | IZ_APP_ENGINE_IMAGE },
            { "presentation", IZ_APP_PRESENTATION | IZ_APP_WORKER | IZ_APP_COOPERATIVE | IZ_APP_ENGINE_IMAGE },
        };
        for (int i = list + 1; i < json->nodes[list].next; i = json->nodes[i].next) {
            unsigned which = 0, available = abi == 7 ? sizeof names / sizeof *names : 1;
            while (which < available && !jeq(json, i, names[which].name)) ++which;
            if (which == available) return UNSUPPORTED;
            if (seen & (1u << which)) return BAD;
            seen |= 1u << which;
            header->flags |= names[which].flags;
        }
    }
    u32 rc = companion_metadata(source, json, jkey(json, node, "assets"), base, package);
    if (rc) return rc;
    Blob *code = &package->sections[abi == 1 ? SEC_BEGIN : SEC_NATIVE_CODE];
    Blob *initial = &package->sections[abi == 1 ? SEC_AFTER : SEC_NATIVE_DATA];
    rc = asset(source, json, jkey(json, node, "code"), base, 16u * MIB, code, 0);
    if (rc) return rc;
    return asset(source, json, jkey(json, node, "initial"), base, data_bytes, initial, 1);
}

static u32 read_package(const ZipSource *source, const char *pad, Package *package) {
    Blob metadata = {0}, manifest = {0};
    Json *json = calloc(1, sizeof *json);
    if (!json) return ENOMEM;
    char entry[512], base[512], module_id[48];
    IzAppHeader *header = &package->header;
    u32 rc = source_blob(source, "module.json", 65536, &metadata);
    if (rc) goto done;
    if (jparse(json, (char *)metadata.data, metadata.bytes) ||
        !jeq(json, jkey(json, 0, "format"), "izlauncher.module") ||
        jstr(json, jkey(json, 0, "id"), module_id, sizeof module_id) || !id_valid(module_id)) {
        rc = BAD;
        goto done;
    }
    if (!jempty(json, jkey(json, 0, "dependencies"))) { rc = UNSUPPORTED; goto done; }
    int sw = jkey(json, 0, "switch");
    if (jstr(json, jkey(json, sw, "entry"), entry, sizeof entry) || !safe_path(entry)) {
        rc = BAD;
        goto done;
    }
    strcpy(base, entry);
    char *slash = strrchr(base, '/');
    if (slash) slash[1] = 0;
    else base[0] = 0;
    rc = source_blob(source, entry, 65536, &manifest);
    if (rc) goto done;
    if (jparse(json, (char *)manifest.data, manifest.bytes) ||
        !jeq(json, jkey(json, 0, "format"), "hblauncher.gpu-app") ||
        jstr(json, jkey(json, 0, "id"), header->id, sizeof header->id) ||
        strcmp(header->id, module_id) ||
        jtext(json, jkey(json, 0, "name"), header->name, sizeof header->name) || !header->name[0]) {
        rc = BAD;
        goto done;
    }
    u32 abi;
    if (ju32(json, jkey(json, 0, "abi"), &abi) || abi != 1) { rc = UNSUPPORTED; goto done; }
    const char *unsupported[] = { "hostRuntime", "localRuntime", "builtin", "displayOnly", "shellContribution" };
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; ++i) {
        if (jkey(json, 0, unsupported[i]) != -1) { rc = UNSUPPORTED; goto done; }
    }
    int audio = jkey(json, 0, "audio");
    if (audio >= 0) {
        u32 rate, channels;
        int effects = jkey(json, audio, "effects");
        if (ju32(json, jkey(json, audio, "sampleRate"), &rate) || rate != 48000 ||
            ju32(json, jkey(json, audio, "channels"), &channels) || channels != 2 ||
            !jeq(json, jkey(json, audio, "format"), "s16le") ||
            (effects >= 0 && (json->nodes[effects].kind != '{' || json->nodes[effects].count))) {
            rc = UNSUPPORTED;
            goto done;
        }
    }
    memcpy(header->magic, "IZSDAPP1", 8);
    header->version = 1;
    header->header_bytes = 512;
    rc = sdk_metadata(json, jkey(json, 0, "sdk"), header);
    if (rc) goto done;
    int groups = jkey(json, 0, "computeWorkgroups");
    header->groups[0] = header->groups[1] = header->groups[2] = 1;
    if (groups >= 0) {
        if (json->nodes[groups].kind != '[' || json->nodes[groups].count != 3) {
            rc = BAD;
            goto done;
        }
        int group = groups + 1;
        for (unsigned i = 0; i < 3; ++i) {
            if (ju32(json, group, &header->groups[i])) { rc = BAD; goto done; }
            group = json->nodes[group].next;
        }
    }
    int seq = jkey(json, 0, "computeSequence");
    if (seq >= 0 && (ju32(json, jkey(json, seq, "repeat"), &header->repeat) || !header->repeat ||
                     header->repeat > 512)) { rc = BAD; goto done; }
    int memory = jkey(json, 0, "memory");
    int memory_encoding = jkey(json, memory, "encoding");
    u32 memory_maximum = 32u * MIB;
    if (memory >= 0) {
        if (json->nodes[memory].kind != '{') { rc = BAD; goto done; }
        int profile = jkey(json, memory, "profile");
        if (profile >= 0) {
            if (!jeq(json, profile, "resident-audio-128")) { rc = UNSUPPORTED; goto done; }
            header->version = 2;
            header->flags |= IZ_APP_PRIVATE_128;
            memory_maximum = 128u * MIB;
        }
        if (ju32(json, jkey(json, memory, "bytes"), &header->private_bytes) ||
            header->private_bytes < 4096 || header->private_bytes > memory_maximum ||
            header->private_bytes % 4096) { rc = BAD; goto done; }
        if (jkey(json, memory, "initial") < 0) { rc = BAD; goto done; }
        if (memory_encoding >= 0 && !jeq(json, memory_encoding, "deflate")) {
            rc = UNSUPPORTED;
            goto done;
        }
    }
    int shader_encoding = jkey(json, 0, "shaderEncoding");
    if (shader_encoding >= 0 && !jeq(json, shader_encoding, "deflate")) { rc = UNSUPPORTED; goto done; }
    int shaders = jkey(json, 0, "shaders");
    const int nodes[] = {
        jkey(json, 0, "initialState"), jkey(json, 0, "data"),
        jkey(json, shaders, "vertex"), jkey(json, shaders, "fragment"), jkey(json, shaders, "compute"),
        jkey(json, seq, "begin"), jkey(json, seq, "after"), jkey(json, memory, "initial"),
    };
    for (int i = SEC_STATE; i <= SEC_PRIVATE; ++i) {
        const int is_shader = i >= SEC_VERTEX && i <= SEC_AFTER;
        int deflated = (i == SEC_PRIVATE && memory_encoding >= 0) || (is_shader && shader_encoding >= 0);
        if ((i == SEC_BEGIN || i == SEC_AFTER) && !header->repeat) continue;
        u32 limit = i == SEC_STATE ? 1024 : i == SEC_DATA ? 7u * MIB :
                    i == SEC_PRIVATE ? header->private_bytes : MIB;
        u32 stored_limit = i == SEC_PRIVATE && deflated ? memory_maximum : limit;
        Blob *blob = &package->sections[i];
        rc = asset(source, json, nodes[i], base, stored_limit, blob,
                   i == SEC_DATA || i == SEC_PRIVATE);
        if (rc) goto done;
        if (deflated) {
            rc = inflate_blob(blob, limit, i == SEC_PRIVATE);
            if (rc) goto done;
        } else if (i == SEC_PRIVATE) {
            u32 used = blob->bytes;
            while (used && !blob->data[used - 1]) --used;
            blob->bytes = private_prefix(used, blob->bytes);
        }
        if (is_shader && (blob->bytes < 20 ||
            memcmp(blob->data, "\x03\x02\x23\x07", 4))) { rc = BAD; goto done; }
    }
    rc = native_metadata(source, json, jkey(json, 0, "native"), base, package);
    if (rc) goto done;
    if ((header->flags & IZ_APP_SDK) && (header->flags & IZ_APP_PRESENTATION)) {
        rc = UNSUPPORTED;
        goto done;
    }

    // A zero-sized label asks the launcher to render the original title.
    package->sections[SEC_TILE] = (Blob){ calloc(1, IZ_TILE_BYTES), IZ_TILE_BYTES };
    if (!package->sections[SEC_TILE].data) { rc = ENOMEM; goto done; }
    rc = file_blob(pad, MIB, &package->sections[SEC_PAD]);
    if (rc) goto done;
    Blob *pad_blob = &package->sections[SEC_PAD];
    if (pad_blob->bytes < 20 || memcmp(pad_blob->data, "\x03\x02\x23\x07", 4)) { rc = BAD; goto done; }

    u64 size = sizeof *header;
    for (unsigned i = 0; i < SEC_COUNT; ++i) {
        Blob *blob = &package->sections[i];
        if (!blob->bytes) continue;
        size = (size + 3) & ~3ull;
        if (size + blob->bytes > UINT32_MAX) { rc = BAD; goto done; }
        header->section[i] = (IzSection){ (u32)size, blob->bytes, iz_crc(blob->data, blob->bytes), 0 };
        size += blob->bytes;
    }
    header->total_bytes = (u32)size;
    rc = iz_validate_header(header, size);
done:
    free(metadata.data);
    free(manifest.data);
    free(json);
    return rc;
}

static int legacy_image(const IzAppHeader *header) {
    return (header->flags & IZ_APP_WORKER) && !(header->flags & IZ_APP_SB4_COMPAT);
}

static u32 validate_image(Package *package, int allow_managed, int allow_legacy) {
    const Blob *image = &package->image;
    IzAppHeader *header = &package->header;
    if (image->bytes < sizeof *header) return BAD;
    memcpy(header, image->data, sizeof *header);
    u32 rc = iz_validate_header(header, image->bytes);
    if (rc) return rc;
    if ((header->version == 3 && !allow_managed) ||
        (!allow_legacy && legacy_image(header)))
        return UNSUPPORTED;

    for (unsigned i = 0; i < SEC_COUNT; ++i) {
        const IzSection *section = &header->section[i];
        if (!section->bytes) continue;
        const u8 *data = image->data + section->offset;
        if (iz_crc(data, section->bytes) != section->crc32) return CRC_ERROR;
        if (((i >= SEC_VERTEX && i <= SEC_COMPUTE) || i == SEC_PAD ||
             (header->repeat && (i == SEC_BEGIN || i == SEC_AFTER))) &&
             memcmp(data, "\x03\x02\x23\x07", 4)) return BAD;
    }
    return 0;
}

static u32 check_sd_bundle(const Blob *metadata, const ZipSource *source, Package *package,
                           const char *image_hash, const char *base) {
    Json *json = calloc(1, sizeof *json);
    if (!json) return ENOMEM;
    u32 rc = BAD, version, bytes;
    if (jparse(json, (const char *)metadata->data, metadata->bytes) ||
        !jeq(json, jkey(json, 0, "format"), "iz2s.sd-bundle") ||
        ju32(json, jkey(json, 0, "version"), &version) || version != 1) goto done;
    char id[48], expected[65];
    int launcher = jkey(json, 0, "launcher");
    int assets = jkey(json, 0, "assets");
    if (jstr(json, jkey(json, 0, "id"), id, sizeof id) ||
        ju32(json, jkey(json, launcher, "bytes"), &bytes) ||
        jstr(json, jkey(json, launcher, "sha256"), expected, sizeof expected) ||
        strlen(expected) != 64 || assets < 0) goto done;
    const Blob *image = &package->image;
    IzAppHeader *header = &package->header;
    if (image->bytes != bytes || strcmp(header->id, id)) goto done;
    char actual[65];
    if (!image_hash) {
        u8 digest[32];
        if (!sha256_bytes(digest, image->data, bytes)) { rc = HASH_ERROR; goto done; }
        digest_text(digest, actual);
        image_hash = actual;
    }
    if (strcmp(expected, image_hash)) { rc = HASH_ERROR; goto done; }
    // The manifest must reproduce the binding in the original header.
    IzAppHeader expected_header = *header;
    header->flags &= ~IZ_APP_ASSETS;
    header->reserved[1] = header->reserved[2] = 0;
    memset(header->padding, 0, 32);
    rc = companion_metadata(source, json, assets, base, package);
    if (!rc && memcmp(header, &expected_header, sizeof *header)) rc = HASH_ERROR;
done:
    free(json);
    return rc;
}

static int companion_filename(const char *name) {
    return strlen(name) == 77 && !strncmp(name, "asset-", 6) &&
        strspn(name + 6, "0123456789abcdef") == 64 && !strcmp(name + 70, ".izdata");
}

static int qualified_manifest(const char *name) {
    return strlen(name) == 79 && !strncmp(name, "sd-bundle-", 10) &&
        strspn(name + 10, "0123456789abcdef") == 64 && !strcmp(name + 74, ".json");
}

static int sd_export_preview(const char *name) {
    return !strcmp(name, "pad-normalize.comp.spv") ||
        !strcmp(name, "sd-tile.bin") || !strcmp(name, "sd-tile.json") ||
        !strcmp(name, "sd-tile-icon-0.png") || !strcmp(name, "managed-sd-tile.bin") ||
        !strcmp(name, "managed-sd-tile.json") || !strcmp(name, "managed-sd-tile-icon-0.png");
}

static u32 add_companion(Package *package, const char *source, const char *name, u64 bytes) {
    if (!bytes || bytes > UINT32_MAX || package->companion_count == MAX_COMPANIONS) return BAD;
    Companion *entry = &package->companions[package->companion_count++];
    strcpy(entry->source, source);
    strcpy(entry->name, name);
    memcpy(entry->hash, name + 6, 64);
    entry->hash[64] = 0;
    entry->bytes = (u32)bytes;
    return 0;
}

static u32 verify_companions(Package *package) {
    const IzAppHeader *header = &package->header;
    const unsigned count = package->companion_count;
    if (!(header->flags & IZ_APP_ASSETS)) return count ? HASH_ERROR : 0;
    if (count != header->reserved[1]) return HASH_ERROR;
    u64 bytes = 0;
    for (unsigned i = 0; i < count; ++i) bytes += package->companions[i].bytes;
    if (bytes != header->reserved[2]) return HASH_ERROR;
    qsort(package->companions, count, sizeof(Companion), companion_order);
    u8 digest[32];
    u32 rc = companion_binding(package->companions, count, digest);
    if (rc) return rc;
    return memcmp(header->padding, digest, sizeof digest) ? HASH_ERROR : 0;
}

/* With no folder ID to collect, read the single application at ZIP root. */
static u32 list_sd_entries(unzFile zip, Package *package, char *id, int *manifest) {
    static const char base[] = "iz2s/modules/";
    const size_t base_len = sizeof base - 1;
    int launcher = 0;
    int cursor = unzGoToFirstFile(zip);
    for (; cursor == UNZ_OK; cursor = unzGoToNextFile(zip)) {
        char member[512];
        unz_file_info64 info;
        if (unzGetCurrentFileInfo64(zip, &info, member, sizeof member, NULL, 0, NULL, 0) != UNZ_OK ||
            !info.size_filename || info.size_filename >= sizeof member ||
            strlen(member) != info.size_filename) return BAD;
        const char *leaf = member;
        if (id) {
            size_t bytes = (size_t)info.size_filename;
            if (member[bytes - 1] == '/' && bytes <= base_len && !strncmp(member, base, bytes)) {
                if (info.uncompressed_size) return BAD;
                continue;
            }
            if (strncmp(member, base, base_len)) return BAD;
            char *slash = strchr(member + base_len, '/');
            if (!slash || slash == member + base_len || slash - (member + base_len) >= 48) return BAD;
            *slash = 0;
            const char *folder = member + base_len;
            if (!id_valid(folder) || (*id && strcmp(id, folder))) return BAD;
            strcpy(id, folder);
            *slash = '/';
            leaf = slash + 1;
        }
        if (!*leaf) {
            if (info.uncompressed_size) return BAD;
        } else if (!strcmp(leaf, "launcher.iza")) {
            launcher = 1;
        } else if (!strcmp(leaf, "sd-bundle.json") || qualified_manifest(leaf)) {
            *manifest = 1;
        } else if (!sd_export_preview(leaf)) {
            if (!companion_filename(leaf)) return BAD;
            u32 rc = add_companion(package, member, leaf, info.uncompressed_size);
            if (rc) return rc;
        }
    }
    return cursor == UNZ_END_OF_LIST_OF_FILE && launcher ? 0 : BAD;
}

/* A package saved by an update may contain several generations of metadata.
 * Select the manifest bound to the bytes of the exported launcher. */
static u32 read_zip_manifest(unzFile zip, Package *package, const char *base, int *found) {
    *found = 0;
    u8 digest[32];
    char hash[65], path[160];
    if (!sha256_bytes(digest, package->image.data, package->image.bytes)) return HASH_ERROR;
    digest_text(digest, hash);
    snprintf(path, sizeof path, "%ssd-bundle-%s.json", base, hash);
    int cursor = unzLocateFile(zip, path, 1);
    if (cursor == UNZ_END_OF_LIST_OF_FILE) {
        snprintf(path, sizeof path, "%ssd-bundle.json", base);
        cursor = unzLocateFile(zip, path, 1);
    }
    if (cursor == UNZ_END_OF_LIST_OF_FILE) return 0;
    if (cursor != UNZ_OK) return BAD;

    *found = 1;
    package->companion_count = 0;
    Blob metadata = {0};
    u32 rc = zip_blob(zip, path, 16384, &metadata);
    if (!rc) rc = check_sd_bundle(&metadata, &(ZipSource){.archive=zip}, package, hash, base);
    free(metadata.data);
    return rc;
}

static u32 read_sd_tree(unzFile zip, Package *package, int nested, int allow_managed, int allow_legacy) {
    char id[48] = {0}, base[80] = {0}, path[112];
    int manifest = 0;
    u32 rc = list_sd_entries(zip, package, nested ? id : NULL, &manifest);
    if (rc) return rc;
    if (nested) snprintf(base, sizeof base, "iz2s/modules/%s/", id);
    snprintf(path, sizeof path, "%slauncher.iza", base);
    rc = zip_blob(zip, path, UINT32_MAX, &package->image);
    if (!rc) rc = validate_image(package, allow_managed, allow_legacy);
    if (rc) return rc;
    const IzAppHeader *header = &package->header;
    if (nested && strcmp(header->id, id)) return HASH_ERROR;

    if (!manifest) return verify_companions(package);

    unsigned members = package->companion_count;
    int found;
    rc = read_zip_manifest(zip, package, base, &found);
    if (rc) return rc;
    if (!found) return verify_companions(package);
    if (package->companion_count != members) return HASH_ERROR;
    const size_t base_len = strlen(base);
    for (unsigned i = 0; i < members; ++i) {
        const Companion *entry = &package->companions[i];
        if (strcmp(entry->source + base_len, entry->name)) return HASH_ERROR;
    }
    return 0;
}

static u32 read_sd_bundle(unzFile zip, Package *package, int allow_managed, int allow_legacy) {
    if (unzLocateFile(zip, "sd-bundle.json", 1) == UNZ_END_OF_LIST_OF_FILE) {
        int nested = unzLocateFile(zip, "launcher.iza", 1) == UNZ_END_OF_LIST_OF_FILE;
        return read_sd_tree(zip, package, nested, allow_managed, allow_legacy);
    }
    u32 rc = zip_blob(zip, "launcher.iza", UINT32_MAX, &package->image);
    if (!rc) rc = validate_image(package, allow_managed, allow_legacy);
    if (rc) return rc;
    int found;
    rc = read_zip_manifest(zip, package, "", &found);
    return !rc && !found ? BAD : rc;
}

static int sibling_path(const char *path, const char *leaf, char out[768]) {
    const char *slash = strrchr(path, '/');
    size_t directory = slash ? (size_t)(slash - path + 1) : 0;
    if (directory >= 768) return 0;
    int written = snprintf(out, 768, "%.*s%s", (int)directory, path, leaf);
    return written >= 0 && written < 768;
}

static u32 read_sibling_companions(const char *source, Package *package) {
    char path[768];
    if (!sibling_path(source, "", path)) return BAD;
    size_t length = strlen(path);
    if (!length) strcpy(path, ".");
    else if (length > 1 && path[length - 2] != ':') path[length - 1] = 0;
    DIR *directory = opendir(path);
    if (!directory) return errno;
    u32 rc = 0;
    while (!rc) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) { rc = errno; break; }
        if (!companion_filename(entry->d_name)) continue;
        if (!sibling_path(source, entry->d_name, path)) { rc = BAD; break; }
        struct stat info;
        if (lstat(path, &info)) { rc = errno; break; }
        if (!S_ISREG(info.st_mode)) { rc = BAD; break; }
        rc = add_companion(package, entry->d_name, entry->d_name, (u64)info.st_size);
    }
    if (closedir(directory) && !rc) rc = errno;
    return rc ? rc : verify_companions(package);
}

static u32 read_packed_bundle(const char *source, Package *package, Blob *metadata) {
    u8 digest[32];
    char hash[65], name[80], manifest[768];
    const Blob *image = &package->image;
    if (!sha256_bytes(digest, image->data, image->bytes)) return HASH_ERROR;
    digest_text(digest, hash);
    snprintf(name, sizeof name, "sd-bundle-%s.json", hash);
    if (!sibling_path(source, name, manifest)) return BAD;
    struct stat info;
    if (lstat(manifest, &info)) {
        if (errno != ENOENT) return errno;
        if (!sibling_path(source, "sd-bundle.json", manifest)) return BAD;
        if (lstat(manifest, &info))
            return errno == ENOENT ? read_sibling_companions(source, package) : (u32)errno;
    }
    if (!S_ISREG(info.st_mode)) return BAD;
    u32 rc = file_blob(manifest, 16384, metadata);
    return rc ? rc : check_sd_bundle(metadata, NULL, package, hash, "");
}
static FILE *create_file(const char *name) {
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        int failure = errno;
        close(fd);
        unlink(name);
        errno = failure;
    }
    return file;
}

static u32 finish_file(FILE *file, u32 rc) {
    if (!rc && (fflush(file) || fsync(fileno(file)))) rc = EIO;
    if (fclose(file) && !rc) rc = EIO;
    return rc;
}

static u32 checked_zip_member(unzFile zip, const Companion *entry, const char *dest) {
    u32 size;
    u32 rc = zip_entry(zip, entry->source, entry->bytes, &size);
    if (rc || size != entry->bytes || unzOpenCurrentFile(zip) != UNZ_OK) return BAD;
    FILE *file = dest ? create_file(dest) : NULL;
    if (dest && !file) { rc = errno; unzCloseCurrentFile(zip); return rc; }
    u8 buffer[16384], digest[32];
#ifdef __SWITCH2__
    Sha256Context hash;
    sha256ContextCreate(&hash);
#else
    EVP_MD_CTX *hash = EVP_MD_CTX_new();
    if (!hash || EVP_DigestInit_ex(hash, EVP_sha256(), NULL) != 1) rc = HASH_ERROR;
#endif
    for (u32 at = 0; !rc && at < size;) {
        u32 count = size - at;
        if (count > sizeof buffer) count = sizeof buffer;
        int got = unzReadCurrentFile(zip, buffer, count);
        if (got <= 0) { rc = BAD; break; }
#ifdef __SWITCH2__
        sha256ContextUpdate(&hash, buffer, (size_t)got);
#else
        if (EVP_DigestUpdate(hash, buffer, (size_t)got) != 1) { rc = HASH_ERROR; break; }
#endif
        if (file && fwrite(buffer, 1, (size_t)got, file) != (size_t)got) { rc = EIO; break; }
        at += (u32)got;
    }
    if (!rc && unzReadCurrentFile(zip, buffer, 1) != 0) rc = BAD;
    if (unzCloseCurrentFile(zip) != UNZ_OK && !rc) rc = CRC_ERROR;
#ifdef __SWITCH2__
    if (!rc) sha256ContextGetHash(&hash, digest);
#else
    unsigned digest_size;
    if (!rc && (EVP_DigestFinal_ex(hash, digest, &digest_size) != 1 || digest_size != 32))
        rc = HASH_ERROR;
    EVP_MD_CTX_free(hash);
#endif
    if (!rc) {
        const char *hex = "0123456789abcdef";
        for (unsigned i = 0; i < sizeof digest; ++i) {
            if (entry->hash[i * 2] != hex[digest[i] >> 4] || entry->hash[i * 2 + 1] != hex[digest[i] & 15]) {
                rc = HASH_ERROR;
                break;
            }
        }
    }
    if (file) {
        rc = finish_file(file, rc);
        if (rc) unlink(dest);
    }
    return rc;
}

static u32 write_image(const Package *package, const char *dest) {
    FILE *file = create_file(dest);
    if (!file) return errno;
    if (package->image.data) {
        const Blob *image = &package->image;
        u32 rc = fwrite(image->data, 1, image->bytes, file) == image->bytes ? 0 : EIO;
        rc = finish_file(file, rc);
        if (rc) unlink(dest);
        return rc;
    }
    const IzAppHeader *header = &package->header;
    u32 rc = fwrite(header, 1, sizeof *header, file) == sizeof *header ? 0 : EIO;
    u32 position = sizeof *header;
    for (unsigned i = 0; !rc && i < SEC_COUNT; ++i) {
        const Blob *blob = &package->sections[i];
        if (!blob->bytes) continue;
        u32 padding = header->section[i].offset - position;
        const u8 zero[3] = {0};
        if (padding > sizeof zero || fwrite(zero, 1, padding, file) != padding ||
            fwrite(blob->data, 1, blob->bytes, file) != blob->bytes) rc = EIO;
        position = header->section[i].offset + blob->bytes;
    }
    rc = finish_file(file, rc);
    if (rc) unlink(dest);
    return rc;
}

static u32 commit_update(const char *directory) {
#ifdef __SWITCH2__
    (void)directory;
    return fsdevCommitDevice("sdmc");
#else
    int fd = open(directory, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return errno;
    u32 result = fsync(fd) ? errno : 0;
    if (close(fd) && !result) result = errno;
    return result;
#endif
}

static u32 digest_and_copy(const char *path, u64 limit, char digest[65], u64 *bytes, const char *dest) {
    struct stat info;
    if (lstat(path, &info)) return errno;
    if (!S_ISREG(info.st_mode) || info.st_size < 0 || (u64)info.st_size > limit) return BAD;
    FILE *file = fopen(path, "rb");
    if (!file) return errno;
    FILE *output = NULL;
    if (dest) {
        output = create_file(dest);
        if (!output) { u32 error = errno; fclose(file); return error; }
    }
    u8 block[16384], hash[32];
    u64 total = 0;
    u32 result = 0;
#ifdef __SWITCH2__
    Sha256Context context;
    sha256ContextCreate(&context);
#else
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1) result = HASH_ERROR;
#endif
    while (!result && total < (u64)info.st_size) {
        u64 left = (u64)info.st_size - total;
        size_t expected = left > sizeof block ? sizeof block : (size_t)left;
        if (fread(block, 1, expected, file) != expected) { result = EIO; break; }
        if (output && fwrite(block, 1, expected, output) != expected) { result = EIO; break; }
#ifdef __SWITCH2__
        sha256ContextUpdate(&context, block, expected);
#else
        if (EVP_DigestUpdate(context, block, expected) != 1) result = HASH_ERROR;
#endif
        total += expected;
    }
    if (!result && (fgetc(file) != EOF || ferror(file))) result = EIO;
    if (fclose(file) && !result) result = EIO;
#ifdef __SWITCH2__
    if (!result) sha256ContextGetHash(&context, hash);
#else
    unsigned length = 0;
    if (!result && (EVP_DigestFinal_ex(context, hash, &length) != 1 || length != sizeof hash))
        result = HASH_ERROR;
    EVP_MD_CTX_free(context);
#endif
    if (output) {
        result = finish_file(output, result);
        if (result) unlink(dest);
    }
    if (result) return result;
    digest_text(hash, digest);
    *bytes = total;
    return 0;
}

static u32 file_digest(const char *path, u64 limit, char digest[65], u64 *bytes) {
    return digest_and_copy(path, limit, digest, bytes, NULL);
}

static u32 copy_packed_asset(const char *source, const Companion *entry, const char *dest) {
    char path[768], digest[65];
    u64 bytes;
    if (!sibling_path(source, entry->source, path)) return BAD;
    u32 rc = digest_and_copy(path, entry->bytes, digest, &bytes, dest);
    if (!rc && (bytes != entry->bytes || strcmp(digest, entry->hash))) {
        unlink(dest);
        rc = HASH_ERROR;
    }
    return rc;
}

static u32 read_published_header(const char *path, const char *id, IzAppHeader *header) {
    struct stat info;
    if (lstat(path, &info)) return errno;
    if (!S_ISREG(info.st_mode) || info.st_size < (off_t)sizeof(IzAppHeader)) return BAD;
    FILE *file = fopen(path, "rb");
    if (!file) return errno;
    size_t bytes = fread(header, 1, sizeof *header, file);
    int closed = fclose(file);
    if (bytes != sizeof *header || closed) return EIO;
    u32 result = iz_validate_header(header, (u64)info.st_size);
    return result ? result : strcmp(header->id, id) ? BAD : 0;
}

static u32 check_published_image(const char *path, const char *id) {
    IzAppHeader header;
    return read_published_header(path, id, &header);
}

/* The backup is taken only after the new assets and manifest are durable.
 * If no image is published, restore the old one. If both are present, keep
 * the published image once it is durable and remove the old backup. */
static u32 recover_update(const char *directory, const char *id) {
    char backup[848], current[848];
    snprintf(backup, sizeof backup, "%s/.launcher-update.bak", directory);
    struct stat info;
    if (lstat(backup, &info)) return errno == ENOENT ? 0 : (u32)errno;
    u32 result = check_published_image(backup, id);
    if (result) return result;
    snprintf(current, sizeof current, "%s/launcher.iza", directory);
    result = check_published_image(current, id);
    if (result == ENOENT) return rename(backup, current) ? (u32)errno : commit_update(directory);
    if (result) return result;
    result = commit_update(directory);
    if (result) return result;
    return unlink(backup) ? (u32)errno : commit_update(directory);
}

static u32 write_bundle(const Package *package, const char *path, const char *digest, u64 bytes) {
    FILE *file = create_file(path);
    if (!file) return errno;
    const char *id = package->header.id;
    int ok = fprintf(file, "{\"format\":\"iz2s.sd-bundle\",\"version\":1,\"id\":\"%s\","
        "\"launcher\":{\"bytes\":%llu,\"sha256\":\"%s\"},\"assets\":[", id,
        (unsigned long long)bytes, digest) >= 0;
    for (unsigned i = 0; ok && i < package->companion_count; ++i) {
        const Companion *asset = &package->companions[i];
        ok = fprintf(file, "%s{\"file\":\"%s\",\"path\":\"%s\",\"bytes\":%u,\"sha256\":\"%s\"}",
                     i ? "," : "", asset->name, asset->name, asset->bytes, asset->hash) >= 0;
    }
    if (ok) ok = fputs("]}\n", file) != EOF;
    return finish_file(file, ok ? 0 : EIO);
}

static u32 publish_asset(const char *source, const char *dest, const Companion *asset) {
    char digest[65];
    u64 bytes = 0;
    u32 result = file_digest(dest, asset->bytes, digest, &bytes);
    if (result == ENOENT) return rename(source, dest) ? (u32)errno : 0;
    if (result) return result;
    return bytes == asset->bytes && !strcmp(digest, asset->hash) ? 0 : HASH_ERROR;
}

static u32 update_package(const Package *package, const char *stage, const char *dest,
                          const char *expected_old, int restore_only, int *updated) {
    u32 result = recover_update(dest, package->header.id);
    if (result) return result;
    char source[848], target[848], launcher[848], backup[848], candidate[848];
    snprintf(launcher, sizeof launcher, "%s/launcher.iza", dest);
    snprintf(candidate, sizeof candidate, "%s/launcher.iza", stage);
    snprintf(backup, sizeof backup, "%s/.launcher-update.bak", dest);
    result = check_published_image(launcher, package->header.id);
    if (result && result != ENOENT) return result;
    int replacing = !result;
    if (restore_only && replacing) return EEXIST;
    char digest[65], old_digest[65];
    u64 bytes = 0, old_bytes = 0;
    result = file_digest(candidate, UINT32_MAX, digest, &bytes);
    if (result) return result;
    if (replacing) {
        result = file_digest(launcher, UINT32_MAX, old_digest, &old_bytes);
        if (result) return result;
    }
    if (expected_old && (!replacing || strcmp(expected_old, old_digest))) return HASH_ERROR;

    for (unsigned i = 0; i < package->companion_count; ++i) {
        const Companion *asset = &package->companions[i];
        snprintf(source, sizeof source, "%s/%s", stage, asset->name);
        snprintf(target, sizeof target, "%s/%s", dest, asset->name);
        result = publish_asset(source, target, asset);
        if (result) return result;
    }
    snprintf(source, sizeof source, "%s/sd-bundle.json", stage);
    snprintf(target, sizeof target, "%s/sd-bundle-%s.json", dest, digest);
    result = write_bundle(package, source, digest, bytes);
    if (result) return result;
    char metadata_hash[65];
    u64 metadata_bytes = 0;
    result = file_digest(target, 16384, metadata_hash, &metadata_bytes);
    if (result == ENOENT) {
        if (rename(source, target)) return errno;
    } else {
        if (result) return result;
        char staged_hash[65];
        u64 staged_bytes = 0;
        result = file_digest(source, 16384, staged_hash, &staged_bytes);
        if (result) return result;
        if (staged_bytes != metadata_bytes || strcmp(staged_hash, metadata_hash)) return HASH_ERROR;
    }
    result = commit_update(dest);
    if (result) return result;
    if (replacing && old_bytes == bytes && !strcmp(digest, old_digest)) return EEXIST;

    if (replacing) {
        if (rename(launcher, backup)) return errno;
        if (expected_old) {
            result = file_digest(backup, UINT32_MAX, old_digest, &old_bytes);
            if (!result && strcmp(expected_old, old_digest)) result = HASH_ERROR;
        }
        if (!result) result = commit_update(dest);
    }
    if (!result && rename(candidate, launcher)) result = errno;
    if (result) {
        u32 recovery = recover_update(dest, package->header.id);
        return recovery ? recovery : result;
    }
    result = commit_update(dest);
    if (result) return result;
    *updated = replacing;
    if (replacing) {
        u32 cleanup = recover_update(dest, package->header.id);
        if (cleanup) fprintf(stderr, "IZLAUNCHER_IMPORT_BACKUP id=%s rc=0x%08x\n", package->header.id, cleanup);
    }
    return 0;
}

typedef enum { OFFLINE_NONE, OFFLINE_BUNDLE, OFFLINE_LAYER } OfflineAdapter;

static int adapter_file(const char *folder, const char *digest, char filename[768]) {
    struct stat info;
    int written = snprintf(filename, 768, "%s/%s.zip", folder, digest);
    return written > 0 && written < 768 && !stat(filename, &info) && S_ISREG(info.st_mode);
}

static int adapter_directory(const char *pad, u64 length, char folder[680]) {
    const char *slash = strrchr(pad, '/');
    size_t directory = slash ? (size_t)(slash - pad + 1) : 0;
    if (directory >= 640) return 0;
    int written = length
        ? snprintf(folder, 680, "%.*scompat/%llu", (int)directory, pad, (unsigned long long)length)
        : snprintf(folder, 680, "%.*scompat", (int)directory, pad);
    struct stat info;
    return written > 0 && written < 680 && !stat(folder, &info) && S_ISDIR(info.st_mode);
}

static OfflineAdapter known_adapter(const char *pad, u64 length, const char *digest,
                                    int zipped, char filename[768]) {
    char folder[680];
    if (zipped && adapter_directory(pad, length, folder)) {
        if (adapter_file(folder, digest, filename)) return OFFLINE_LAYER;
        if (length > OFFLINE_BUNDLE_LIMIT) {
            // A large source and its packed image can point to the same RomFS bundle.
            char reference[768], target[65];
            snprintf(reference, sizeof reference, "%s/%s.bundle", folder, digest);
            Blob recipe = {0};
            u32 rc = file_blob(reference, sizeof target - 1, &recipe);
            int valid = !rc && recipe.bytes == sizeof target - 1;
            if (valid) memcpy(target, recipe.data, sizeof target - 1);
            free(recipe.data);
            target[sizeof target - 1] = 0;
            if (valid && strspn(target, "0123456789abcdef") == sizeof target - 1 &&
                adapter_directory(pad, 0, folder) && adapter_file(folder, target, filename))
                return OFFLINE_BUNDLE;
        }
    }
    if (length <= OFFLINE_BUNDLE_LIMIT && adapter_directory(pad, 0, folder) &&
        adapter_file(folder, digest, filename)) return OFFLINE_BUNDLE;
    return OFFLINE_NONE;
}

static OfflineAdapter offline_adapter(const char *source, const char *pad, int zipped, char filename[768]) {
    struct stat info;
    if (lstat(source, &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || (u64)info.st_size > OFFLINE_LAYER_LIMIT) return OFFLINE_NONE;
    const u64 length = (u64)info.st_size;
    char folder[680], digest[65];
    if (length > OFFLINE_BUNDLE_LIMIT && (!zipped || !adapter_directory(pad, length, folder)))
        return OFFLINE_NONE;
    u64 bytes;
    if (file_digest(source, length, digest, &bytes) || bytes != length) return OFFLINE_NONE;
    return known_adapter(pad, length, digest, zipped, filename);
}

static u32 read_legacy_bundle(ZipSource *inputs, Package *package, const char *pad,
                              char adapted[768]) {
    u8 digest[32];
    char hash[65], id[48];
    const Blob *image = &package->image;
    if (image->bytes > OFFLINE_BUNDLE_LIMIT) return UNSUPPORTED;
    if (!sha256_bytes(digest, image->data, image->bytes)) return HASH_ERROR;
    digest_text(digest, hash);
    if (known_adapter(pad, image->bytes, hash, 0, adapted) != OFFLINE_BUNDLE) return UNSUPPORTED;
    for (unsigned i = 0; i < package->companion_count; ++i) {
        u32 rc = checked_zip_member(inputs->archive, &package->companions[i], NULL);
        if (rc) return rc;
    }
    strcpy(id, package->header.id);
    int closed = unzClose(inputs->archive);
    inputs->archive = NULL;
    if (closed != UNZ_OK) return BAD;
    inputs->archive = unzOpen64(adapted);
    if (!inputs->archive) return BAD;
    free(package->image.data);
    memset(package, 0, sizeof *package);
    u32 rc = zip_index(inputs->archive);
    if (!rc) rc = read_sd_bundle(inputs->archive, package, 0, 0);
    if (!rc && strcmp(package->header.id, id)) rc = HASH_ERROR;
    if (!rc) fprintf(stderr, "IZLAUNCHER_IMPORT_COMPAT_LEGACY_BUNDLE id=%s\n", id);
    return rc;
}

static u32 verify_repacked(unzFile zip, const Blob *manifest) {
    u32 rc = zip_index(zip);
    if (rc) return rc;
    Json *json = malloc(sizeof(*json));
    if (!json) return ENOMEM;
    unz_global_info64 info;
    int files = -1;
    if (!jparse(json, (const char *)manifest->data, manifest->bytes) &&
        jeq(json, jkey(json, 0, "algorithm"), "sha256"))
        files = jkey(json, 0, "files");
    if (files < 0 || json->nodes[files].kind != '{' ||
        unzGetGlobalInfo64(zip, &info) != UNZ_OK ||
        info.number_entry != (u64)json->nodes[files].count + 1) rc = HASH_ERROR;
    for (int key = files + 1; !rc && key < json->nodes[files].next; key = json->nodes[key + 1].next) {
        Companion member = {0};
        if (jstr(json, key, member.source, sizeof(member.source)) ||
            !strcmp(member.source, "checksums.json") ||
            jstr(json, key + 1, member.hash, sizeof(member.hash)) ||
            strlen(member.hash) != 64) rc = HASH_ERROR;
        if (!rc) rc = zip_entry(zip, member.source, UINT32_MAX, &member.bytes);
        if (!rc) rc = checked_zip_member(zip, &member, NULL);
    }
    free(json);
    return rc;
}

/* RomFS chooses the trusted recipe by the checksum manifest. Each original
 * ZIP member is verified before accepting the alternate container. */
static u32 repacked_adapter(unzFile zip, const char *pad, char filename[768], OfflineAdapter *kind) {
    char folder[680], index[768], digest[65];
    if (!adapter_directory(pad, 0, folder)) return 0;
    Blob manifest = {0}, recipe = {0};
    if (zip_blob(zip, "checksums.json", 65536, &manifest)) return 0;
    u8 hash[32];
    u32 rc = HASH_ERROR;
    if (sha256_bytes(hash, manifest.data, manifest.bytes)) {
        digest_text(hash, digest);
        snprintf(index, sizeof(index), "%s/repacked/%s", folder, digest);
        rc = file_blob(index, 95, &recipe);
    }
    if (!rc) {
        char relative[96];
        memcpy(relative, recipe.data, recipe.bytes);
        relative[recipe.bytes] = 0;
        int written = snprintf(filename, 768, "%s/%s", folder, relative);
        if (strlen(relative) != recipe.bytes || !safe_path(relative) ||
            written < 0 || written >= 768) rc = BAD;
        if (!rc) rc = verify_repacked(zip, &manifest);
        if (!rc) *kind = strchr(relative, '/') ? OFFLINE_LAYER : OFFLINE_BUNDLE;
    } else if (rc == ENOENT) {
        rc = 0;
    }
    free(recipe.data);
    free(manifest.data);
    return rc;
}

static u32 read_overlay(ZipSource *source, Blob *metadata) {
    u32 rc = zip_index(source->overlay);
    if (!rc) rc = zip_blob(source->overlay, "checksums.json", 65536, metadata);
    if (rc) return rc;
    source->checksums = calloc(1, sizeof(Json));
    if (!source->checksums) return ENOMEM;
    if (jparse(source->checksums, (char *)metadata->data, metadata->bytes) ||
        !jeq(source->checksums, jkey(source->checksums, 0, "algorithm"), "sha256"))
        return BAD;
    source->hashes = jkey(source->checksums, 0, "files");
    return source->hashes < 0 || source->checksums->nodes[source->hashes].kind != '{' ? BAD : 0;
}

static u32 open_overlay(ZipSource *source, const char *filename, Blob *metadata) {
    source->overlay = unzOpen64(filename);
    return source->overlay ? read_overlay(source, metadata) : BAD;
}

static voidpf ZCALLBACK open_memory(voidpf opaque, const void *filename, int mode) {
    (void)filename;
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ) return NULL;
    const Blob *archive = opaque;
    return fmemopen(archive->data, archive->bytes, "rb");
}

static unzFile memory_zip(Blob *archive) {
    zlib_filefunc64_def io;
    fill_fopen64_filefunc(&io);
    io.zopen64_file = open_memory;
    io.opaque = archive;
    return unzOpen2_64(archive, &io);
}

/* A version 3 menu image is a pointer to an ordinary source archive. Its
 * source can select a bundled adapter only after size, hash and ID checks. */
static u32 managed_identity(const IzAppHeader *header, Companion *asset) {
    *asset = (Companion){.bytes=header->reserved[2]};
    digest_text(header->padding + 32, asset->hash);
    snprintf(asset->name, sizeof asset->name, "asset-%s.izdata", asset->hash);
    u8 digest[32];
    u32 rc = companion_binding(asset, 1, digest);
    if (rc) return rc;
    return memcmp(header->padding, digest, sizeof digest) ? HASH_ERROR : 0;
}

static u32 managed_file(const char *path, const Companion *asset, char source[768]) {
    const char *slash = strrchr(path, '/');
    size_t directory = slash ? (size_t)(slash - path + 1) : 0;
    if (directory >= 768) return BAD;
    int written = snprintf(source, 768, "%.*s%s", (int)directory, path, asset->name);
    if (written < 0 || written >= 768) return BAD;
    char actual[65];
    u64 bytes;
    u32 rc = file_digest(source, asset->bytes, actual, &bytes);
    if (rc) return rc;
    return bytes != asset->bytes || strcmp(actual, asset->hash) ? HASH_ERROR : 0;
}

static u32 read_managed(const char *path, const char *pad, ZipSource *inputs,
                        Blob *metadata, Blob *source, Package *package,
                        OfflineAdapter *selected, char adapted[768]) {
    const IzAppHeader *header = &package->header;
    Companion asset;
    u32 rc = managed_identity(header, &asset);
    if (rc) return rc;
    u8 digest[32];
    char original[768], id[48];
    strcpy(id, header->id);
    OfflineAdapter adapter = known_adapter(pad, asset.bytes, asset.hash, 1, adapted);
    if (!asset.bytes || asset.bytes > OFFLINE_LAYER_LIMIT) return UNSUPPORTED;
    if (inputs->archive) {
        const Companion *member = &package->companions[0];
        if (package->companion_count != 1 || strcmp(member->name, asset.name) ||
            strcmp(member->hash, asset.hash) || member->bytes != asset.bytes) return BAD;
        rc = zip_blob(inputs->archive, member->source, asset.bytes, source);
        if (rc) return rc;
        if (source->bytes != asset.bytes || !sha256_bytes(digest, source->data, source->bytes) ||
            memcmp(digest, header->padding + 32, sizeof digest)) return HASH_ERROR;
        rc = unzClose(inputs->archive) == UNZ_OK ? 0 : BAD;
        inputs->archive = NULL;
        if (rc) return rc;
    } else {
        rc = managed_file(path, &asset, original);
        if (rc) return rc;
    }

    free(package->image.data);
    memset(package, 0, sizeof *package);
    if (adapter != OFFLINE_BUNDLE) inputs->archive = source->data ? memory_zip(source) : unzOpen64(original);
    if (adapter == OFFLINE_NONE && inputs->archive) {
        rc = repacked_adapter(inputs->archive, pad, adapted, &adapter);
        if (rc) return rc;
    }
    if (adapter == OFFLINE_NONE) return UNSUPPORTED;
    if (adapter == OFFLINE_BUNDLE) {
        if (inputs->archive) unzClose(inputs->archive);
        inputs->archive = unzOpen64(adapted);
    }
    if (!inputs->archive) return BAD;
    rc = zip_index(inputs->archive);
    if (!rc && adapter == OFFLINE_LAYER) rc = open_overlay(inputs, adapted, metadata);
    if (!rc) rc = adapter == OFFLINE_LAYER
        ? read_package(inputs, pad, package) : read_sd_bundle(inputs->archive, package, 0, 0);
    if (!rc && strcmp(package->header.id, id)) rc = HASH_ERROR;
    if (!rc) {
        *selected = adapter;
        fprintf(stderr, "IZLAUNCHER_IMPORT_COMPAT_MANAGED id=%s\n", id);
    }
    return rc;
}

static u32 installed_adapter(const char *path, const char *id, const char *pad,
                             OfflineAdapter *adapter, char recipe[768], char old_hash[65]) {
    IzAppHeader header;
    u32 rc = read_published_header(path, id, &header);
    if (rc) return rc == ENOENT ? EEXIST : rc;
    *adapter = OFFLINE_NONE;
    if (header.version == 3) {
        Companion asset;
        rc = managed_identity(&header, &asset);
        if (rc) return rc;
        *adapter = known_adapter(pad, asset.bytes, asset.hash, 1, recipe);
        if (*adapter == OFFLINE_NONE && asset.bytes <= OFFLINE_LAYER_LIMIT) {
            char source[768];
            rc = managed_file(path, &asset, source);
            if (rc == ENOENT) return EEXIST;
            if (rc) return rc;
            unzFile zip = unzOpen64(source);
            if (!zip) return EEXIST;
            rc = repacked_adapter(zip, pad, recipe, adapter);
            unzClose(zip);
            if (rc) return rc;
        }
        if (*adapter == OFFLINE_NONE) return 0;

        Blob image = {0};
        u8 digest[32];
        rc = file_blob(path, sizeof header + IZ_TILE_BYTES, &image);
        if (!rc && (image.bytes != sizeof header + IZ_TILE_BYTES ||
                    memcmp(image.data, &header, sizeof header) ||
                    iz_crc(image.data + sizeof header, IZ_TILE_BYTES) != header.section[SEC_TILE].crc32 ||
                    !sha256_bytes(digest, image.data, image.bytes))) rc = HASH_ERROR;
        if (!rc) digest_text(digest, old_hash);
        free(image.data);
        return rc;
    }
    if (header.version != 2 || !legacy_image(&header) || header.total_bytes > OFFLINE_BUNDLE_LIMIT) return 0;
    u64 bytes;
    rc = file_digest(path, OFFLINE_BUNDLE_LIMIT, old_hash, &bytes);
    if (!rc) *adapter = known_adapter(pad, bytes, old_hash, 0, recipe);
    return rc;
}

static u32 match_installed_adapter(const char *dest, const char *id, const char *pad,
                                  OfflineAdapter kind, const char *recipe, char old_hash[65]) {
    if (kind == OFFLINE_NONE) return EEXIST;
    char launcher[848], existing_recipe[768];
    snprintf(launcher, sizeof launcher, "%s/launcher.iza", dest);
    OfflineAdapter old_kind;
    u32 rc = installed_adapter(launcher, id, pad, &old_kind, existing_recipe, old_hash);
    if (rc) return rc;
    if (kind != old_kind) return EEXIST;
    if (!strcmp(recipe, existing_recipe)) return 0;
    if (kind != OFFLINE_BUNDLE) return EEXIST;
    // Known application inputs can select byte-identical bundled packages
    // under different source hashes. Verify the prepared bundles agree.
    char expected[65], actual[65];
    u64 expected_size = 0, actual_size = 0;
    rc = file_digest(recipe, OFFLINE_LAYER_LIMIT, expected, &expected_size);
    if (!rc) rc = file_digest(existing_recipe, OFFLINE_LAYER_LIMIT, actual, &actual_size);
    return rc ? rc : expected_size == actual_size && !strcmp(expected, actual) ? 0 : EEXIST;
}

static u32 import_package(const char *source, const char *root, const char *pad, char id[48],
                          int pc, const char *update_id, int allow_restore, int *updated) {
    id[0] = 0;
    *updated = 0;
    FILE *probe = fopen(source, "rb");
    if (!probe) return errno;
    u8 magic[8];
    size_t got = fread(magic, 1, sizeof magic, probe);
    fclose(probe);
    int packed = got == sizeof magic && !memcmp(magic, "IZSDAPP1", sizeof magic);
    int zipped = got >= 4 && !memcmp(magic, "PK\3\4", 4);
    if (!packed && !zipped) return UNSUPPORTED;
    char adapted[768];
    OfflineAdapter adapter = pc ? OFFLINE_NONE : offline_adapter(source, pad, zipped, adapted);
    if (adapter == OFFLINE_BUNDLE) {
        fprintf(stderr, "IZLAUNCHER_IMPORT_COMPAT source=%s\n", source);
        source = adapted;
        packed = 0;
        zipped = 1;
    }
    unzFile zip = zipped ? unzOpen64(source) : NULL;
    if (zipped && !zip) return BAD;
    ZipSource inputs = {.archive=zip};
    Blob hashes = {0}, managed = {0}, packed_manifest = {0};
    Package *package = calloc(1, sizeof *package);
    u32 rc = package ? 0 : ENOMEM;
    const char *phase = "manifest";
    char destination[768], stage[768], file[848];
    int staged = 0, image_written = 0;
    unsigned companions_written = 0;
    struct stat info;
    if (!rc && packed) {
        rc = file_blob(source, UINT32_MAX, &package->image);
        if (!rc) rc = validate_image(package, !pc, 0);
        if (!rc && package->header.version != 3 && (package->header.flags & IZ_APP_ASSETS))
            rc = pc ? UNSUPPORTED : read_packed_bundle(source, package, &packed_manifest);
    } else if (!rc) {
        if (!pc && adapter == OFFLINE_NONE) {
            phase = "repacked-archive";
            rc = repacked_adapter(zip, pad, adapted, &adapter);
            if (!rc && adapter == OFFLINE_BUNDLE) {
                unzClose(zip);
                inputs.archive = zip = unzOpen64(adapted);
                if (!zip) rc = BAD;
            }
            if (!rc && adapter != OFFLINE_NONE)
                fprintf(stderr, "IZLAUNCHER_IMPORT_COMPAT_REPACKED source=%s\n", source);
        }
        if (!rc) phase = "manifest";
        if (!rc) rc = zip_index(zip);
        if (!rc && adapter == OFFLINE_LAYER) {
            fprintf(stderr, "IZLAUNCHER_IMPORT_COMPAT_OVERLAY source=%s\n", source);
            rc = open_overlay(&inputs, adapted, &hashes);
        }
        if (!rc) rc = inputs.overlay || unzLocateFile(zip, "module.json", 1) == UNZ_OK
            ? read_package(&inputs, pad, package)
            : read_sd_bundle(zip, package, !pc, !pc && adapter == OFFLINE_NONE);
        if (!rc && legacy_image(&package->header)) {
            phase = "legacy-bundle";
            rc = read_legacy_bundle(&inputs, package, pad, adapted);
            if (!rc) adapter = OFFLINE_BUNDLE;
        }
    }
    if (!rc && package->header.version == 3) {
        phase = "managed-source";
        rc = read_managed(source, pad, &inputs, &hashes, &managed, package, &adapter, adapted);
    }
    if (rc) goto done;
    strcpy(id, package->header.id);
    const int foldrift_alias = update_id && !strcmp(update_id, "foldrift") &&
        !strcmp(id, "iz-file-deck");
    const int explicit_update = pc || foldrift_alias || (update_id && !strcmp(id, update_id));
    phase = "destination";
    if (snprintf(destination, sizeof destination, "%s/%s", root, iz_module_folder(id)) >= (int)sizeof destination) {
        rc = BAD;
        goto done;
    }
    int existing_directory = !lstat(destination, &info);
    if (existing_directory && !S_ISDIR(info.st_mode)) { rc = ENOTDIR; goto done; }
    if (!existing_directory && errno != ENOENT) { rc = errno; goto done; }
    char old_hash[65];
    int restore_only = 0;
    if (existing_directory && !explicit_update) {
        if (allow_restore) {
            snprintf(file, sizeof file, "%s/launcher.iza", destination);
            if (lstat(file, &info)) {
                if (errno != ENOENT) { rc = errno; goto done; }
                restore_only = 1;
            }
        }
        if (!restore_only) {
            rc = match_installed_adapter(destination, id, pad, adapter, adapted, old_hash);
            if (rc) goto done;
        }
    }
    phase = "stage-mkdir";
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        if (snprintf(stage, sizeof stage, "%s/.import-%s-%08x-%u", root, id,
                     iz_crc(&package->header, sizeof package->header), attempt) >= (int)sizeof stage) {
            rc = BAD;
            goto done;
        }
        if (!mkdir(stage, 0700)) { staged = 1; break; }
        if (errno != EEXIST) { rc = errno; goto done; }
    }
    if (!staged) { rc = EBUSY; goto done; }
    phase = "companions";
    for (; companions_written < package->companion_count; ++companions_written) {
        const Companion *entry = &package->companions[companions_written];
        if (snprintf(file, sizeof file, "%s/%s", stage, entry->name) >= (int)sizeof file) {
            rc = BAD;
            goto done;
        }
        rc = inputs.archive ? checked_zip_member(zip_reader(&inputs, entry->source), entry, file)
                            : copy_packed_asset(source, entry, file);
        if (rc) goto done;
    }
    phase = "archive-close";
    if (inputs.archive) {
        if (unzClose(inputs.archive) != UNZ_OK) rc = BAD;
        inputs.archive = NULL;
        if (rc) goto done;
    }
    if (inputs.overlay) {
        if (unzClose(inputs.overlay) != UNZ_OK) rc = BAD;
        inputs.overlay = NULL;
        if (rc) goto done;
    }

    phase = "file-write";
    snprintf(file, sizeof file, "%s/launcher.iza", stage);
    rc = write_image(package, file);
    if (rc) goto done;
    image_written = 1;
    if (packed_manifest.bytes && !existing_directory) {
        snprintf(file, sizeof file, "%s/sd-bundle.json", stage);
        FILE *metadata = create_file(file);
        if (!metadata) { rc = errno; goto done; }
        rc = finish_file(metadata, fwrite(packed_manifest.data, 1, packed_manifest.bytes, metadata) ==
            packed_manifest.bytes ? 0 : EIO);
        if (rc) goto done;
    }
    phase = "publish";
    if (existing_directory) rc = update_package(package, stage, destination,
                                                 explicit_update || restore_only ? NULL : old_hash,
                                                 restore_only, updated);
    else if (!lstat(destination, &info)) rc = EEXIST;
    else if (errno != ENOENT) rc = errno;
    else if (rename(stage, destination)) rc = errno;
    else staged = 0;
done:
    if (inputs.archive) unzClose(inputs.archive);
    if (inputs.overlay) unzClose(inputs.overlay);
    free(inputs.checksums);
    free(hashes.data);
    free(managed.data);
    free(packed_manifest.data);
    if (staged) {
        snprintf(file, sizeof file, "%s/sd-bundle.json", stage);
        unlink(file);
        if (image_written) {
            snprintf(file, sizeof file, "%s/launcher.iza", stage);
            unlink(file);
        }
        for (unsigned i = 0; i < companions_written; ++i) {
            snprintf(file, sizeof file, "%s/%s", stage, package->companions[i].name);
            unlink(file);
        }
        rmdir(stage);
    }
    if (rc) fprintf(stderr, "IZLAUNCHER_IMPORT_ERROR phase=%s id=%s rc=0x%08x\n", phase, id, rc);
    if (package) {
        free(package->image.data);
        for (unsigned i = 0; i < SEC_COUNT; ++i) free(package->sections[i].data);
        free(package);
    }
    return rc;
}

uint32_t iz_import_package(const char *source, const char *root, const char *pad, char id[48]) {
    int updated;
    return import_package(source, root, pad, id, 0, NULL, 0, &updated);
}

uint32_t iz_import_pc_package(const char *source, const char *root, const char *pad, char id[48], int *updated) {
    if (!updated) return BAD;
    return import_package(source, root, pad, id, 1, NULL, 0, updated);
}

static int make_roots(void) {
    const char *folders[] = { "sdmc:/iz2s", "sdmc:" IZ_SD_ROOT };
    struct stat info;
    for (unsigned i = 0; i < sizeof folders / sizeof *folders; ++i) {
        if (mkdir(folders[i], 0700) && errno != EEXIST) return errno;
        if (lstat(folders[i], &info) || !S_ISDIR(info.st_mode)) return ENOTDIR;
    }
    return 0;
}

static void recover_updates(const char *root) {
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!id_valid(entry->d_name)) continue;
        char module[768];
        int written = snprintf(module, sizeof module, "%s/%s", root, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof module) continue;
        struct stat info;
        if (lstat(module, &info) || !S_ISDIR(info.st_mode)) continue;
        u32 result = recover_update(module, entry->d_name);
        if (result) fprintf(stderr, "IZLAUNCHER_IMPORT_RECOVERY id=%s rc=0x%08x\n", entry->d_name, result);
    }
    closedir(dir);
}

IzImportResult iz_import_folder(const char *folder,const char *root,const char *pad,int explicit_add) {
    recover_updates(root);
    DIR *dir = opendir(folder);
    if (!dir) return errno == ENOENT ? IZ_IMPORT_EMPTY : IZ_IMPORT_FAILED;
    unsigned seen = 0, added = 0, upgraded = 0, present = 0, failed = 0;
    while (seen < 32) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) ++failed;
            break;
        }
        if (entry->d_name[0] == '.') continue;
        const char *extension = strrchr(entry->d_name, '.');
        int package_name = extension && !strcasecmp(extension, ".iza");
        char path[768], id[48];
        int length = snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof path) {
            ++failed;
            continue;
        }
        struct stat info;
        if (lstat(path, &info)) {
            ++failed;
            continue;
        }
        const char *update_id = NULL;
        char file_id[48];
        if (S_ISDIR(info.st_mode)) {
            static const char launcher_name[] = "/launcher.iza";
            if (sizeof launcher_name > sizeof path - (size_t)length) { ++failed; continue; }
            memcpy(path + length, launcher_name, sizeof launcher_name);
            if (lstat(path, &info)) {
                if (errno != ENOENT) ++failed;
                continue;
            }
            if (explicit_add) update_id = entry->d_name;
        } else if (!package_name) continue;
        else if (explicit_add && !strcasecmp(extension, ".iza")) {
            const size_t bytes = (size_t)(extension - entry->d_name);
            if (bytes && bytes < sizeof file_id) {
                for (size_t i = 0; i < bytes; ++i)
                    file_id[i] = (char)tolower((unsigned char)entry->d_name[i]);
                file_id[bytes] = '\0';
                if (id_valid(file_id)) update_id = file_id;
            }
        }
        if (!S_ISREG(info.st_mode)) continue;
        ++seen;
        int updated = 0;
        u32 result = import_package(path, root, pad, id, 0, update_id, explicit_add, &updated);
        fprintf(stderr, "IZLAUNCHER_IMPORT file=%s id=%s rc=0x%08x updated=%d\n",
                path + strlen(folder) + 1, id, result, updated);
        if (!result) {
            if (updated) ++upgraded;
            else ++added;
        }
        else if (result == EEXIST) ++present;
        else ++failed;
    }
    if (closedir(dir)) ++failed;
    if (failed) return added || upgraded ? IZ_IMPORT_PARTIAL : IZ_IMPORT_FAILED;
    if (added) return IZ_IMPORT_ADDED;
    if (upgraded) return IZ_IMPORT_UPDATED;
    return present ? IZ_IMPORT_CURRENT : IZ_IMPORT_EMPTY;
}

IzImportResult iz_import_incoming(int explicit_add) {
    int rc = make_roots();
    if (rc) {
        fprintf(stderr, "IZLAUNCHER_IMPORT_ERROR phase=module-root rc=0x%08x\n", (unsigned)rc);
        return IZ_IMPORT_FAILED;
    }
    if(mkdir("sdmc:" IZ_INCOMING_DIRECTORY,0700)&&errno!=EEXIST)return IZ_IMPORT_FAILED;
    return iz_import_folder("sdmc:" IZ_INCOMING_DIRECTORY,"sdmc:" IZ_SD_ROOT,
                            "romfs:/menu/pad-normalize.comp.spv",explicit_add);
}
