/*
 * th8_meta_msvc.h -- MSVC C Runtime meta-header for TH8.
 *
 * This header is internal to the TH8 build.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_META_MSVC_H
#define TH8_META_MSVC_H

/*
 * Source files that need MSVC CRT headers must include th8_meta_defs.h,
 * th8_meta_libc.h, and th8_meta_msvc.h explicitly, in that order.
 */

#if defined(_MSC_VER)

#  include <io.h>  /* _open, _read, _write, _close, _isatty */
#  include <fcntl.h> /* _O_BINARY, _O_RDONLY */
#  include <malloc.h> /* _msize() for xMemorySize */

#endif /* _MSC_VER */

#endif /* TH8_META_MSVC_H */
