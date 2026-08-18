/*
 * th8_meta_glibc.h -- glibc stub-resolver meta-header for TH8.
 *
 * This header is internal to the TH8 build.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_META_GLIBC_H
#define TH8_META_GLIBC_H

/*
 * Source files that need the glibc stub-resolver API must include
 * th8_meta_defs.h, th8_meta_libc.h, th8_meta_posix.h, and th8_meta_glibc.h
 * explicitly, in that order.
 *
 * These headers expose glibc's resolver interface (res_ninit / res_nquery /
 * res_nclose, struct __res_state, and the RES_USE_EDNS0 / RES_USE_DNSSEC
 * query options) used by the harpy NTP client's insecure-path DNSSEC AD-bit
 * check.  That check runs whenever the glibc resolver is present, independent
 * of whether libunbound is compiled in -- so, unlike the DANE/TLSA path in
 * th8_meta_posix.h, it is NOT gated on TH8_ENABLE_UNBOUND.  The interface is
 * glibc-only (musl and the BSDs do not define __GLIBC__ and expose a different
 * resolver surface), so everything is guarded on __GLIBC__ and this header is
 * a no-op elsewhere.  <netinet/in.h> and <arpa/nameser.h> are the historical
 * prerequisites of <resolv.h>.
 */

#if defined(__GLIBC__) && !defined(_WIN32) && !defined(WIN32)
#  include <netinet/in.h>  /* in_addr; prerequisite of <resolv.h> */
#  include <arpa/nameser.h> /* ns_msg, ns_rr, NS_* */
#  include <resolv.h>  /* res_ninit, res_nquery, res_nclose, __res_state */
#endif

#endif /* TH8_META_GLIBC_H */
