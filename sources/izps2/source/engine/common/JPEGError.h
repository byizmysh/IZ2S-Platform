#pragma once

#include <cstdio>
#include <jpeglib.h>

#ifdef __SWITCH2__
extern "C" jpeg_error_mgr* iz_jpeg_error_mgr(jpeg_error_mgr* err, void (*fatal)(j_common_ptr));
#endif

inline void JPEGInitializeError(jpeg_error_mgr* err, void (*fatal)(j_common_ptr))
{
#ifdef __SWITCH2__
	iz_jpeg_error_mgr(err, fatal);
#else
	jpeg_std_error(err);
	err->error_exit = fatal;
#endif
}
