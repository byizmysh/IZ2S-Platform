// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdio>
#include <zip.h>

// A seekable file without a POSIX descriptor, such as a HostVFS cookie.
// The source owns the stream after construction succeeds.
zip_source_t* ZipSourceFromStream(std::FILE* file, zip_uint64_t size, zip_error_t* error);

// Writes a new archive to an empty, seekable stream. The caller owns the
// stream and checks fclose before publishing a staged file.
zip_source_t* ZipSourceToStream(std::FILE* file, zip_error_t* error);
