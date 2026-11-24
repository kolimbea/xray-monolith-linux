// Linux: the system libjpeg(-turbo, ABI 62) headers, matching the library linked.
// The system library is built with `boolean` = int. cximage defines HAVE_BOOLEAN to reuse Windows'
// rpcndr `boolean` (unsigned char), which would change the struct layout; keep the library's int
// under a private name instead so it cannot clash with the compat layer's `boolean`.
#pragma once
#include <cstdio>
#undef HAVE_BOOLEAN
// INT32 comes from windows_base.h; jmorecfg.h skips its own typedef like it does for basetsd.h.
// (Not part of the library ABI: jpeglib.h does not use INT32.)
#define _BASETSD_H_
#define boolean xr_jpeg_boolean
#include <jpeglib.h>
