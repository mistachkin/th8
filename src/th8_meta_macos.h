/*
 * th8_meta_macos.h -- macOS / Darwin meta-header for TH8.
 *
 * This header is internal to the TH8 build.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_META_MACOS_H
#define TH8_META_MACOS_H

/*
 * Source files that need macOS headers must include th8_meta_defs.h,
 * th8_meta_libc.h, th8_meta_posix.h, and th8_meta_macos.h explicitly,
 * in that order.
 */

#if defined(__APPLE__)

#  include <malloc/malloc.h> /* malloc_size() */
#  include <mach-o/dyld.h> /* _NSGetExecutablePath */

#endif /* __APPLE__ */

#endif /* TH8_META_MACOS_H */
