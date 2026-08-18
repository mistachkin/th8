/*
 * th8_meta_posix.h -- POSIX / Unix meta-header for TH8.
 *
 * This header is internal to the TH8 build.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_META_POSIX_H
#define TH8_META_POSIX_H

/*
 * Source files that need POSIX headers must include th8_meta_defs.h,
 * th8_meta_libc.h, and th8_meta_posix.h explicitly, in that order.
 */

#if !defined(_WIN32) && !defined(WIN32)

/*
 * Core POSIX API.
 */

#  include <unistd.h>  /* read, write, close, getpid, chroot, ... */
#  include <sys/types.h>  /* pid_t, uid_t, gid_t, off_t */
#  include <sys/stat.h>  /* stat, fstat, chmod */
#  include <sys/time.h>  /* gettimeofday, struct timeval */
#  include <sys/mman.h>  /* mmap, mprotect, mlock, MAP_ANONYMOUS */
#  include <sys/resource.h> /* getrlimit, setrlimit */
#  include <sys/wait.h>  /* waitpid, WEXITSTATUS */

/*
 * Networking.
 */

#  include <sys/socket.h> /* socket, connect, sendto, recvfrom */
#  include <netinet/in.h> /* struct sockaddr_in, htons, ntohs */
#  include <netdb.h>  /* getaddrinfo, freeaddrinfo */
#  include <arpa/inet.h>  /* inet_ntop, inet_pton */
#  include <poll.h>  /* poll, struct pollfd */

/*
 * File system and process.
 */

#  include <fcntl.h>  /* open, O_RDONLY, O_CLOEXEC, O_NOFOLLOW */
#  include <dlfcn.h>  /* dlopen, dlsym, dlclose, dlerror */
#  include <pthread.h>  /* pthread_mutex_*, pthread_self */
#  include <pwd.h>  /* getpwuid, struct passwd */
#  include <grp.h>  /* getgrgid, struct group */
#  include <dirent.h>  /* opendir, readdir, closedir */
#  include <syslog.h>  /* syslog, openlog, closelog */

/*
 * Platform-conditional POSIX extensions.
 * macOS-specific headers are in th8_meta_macos.h.
 */

#  if defined(__FreeBSD__)
#    include <sys/sysctl.h> /* sysctl (for KERN_PROC_PATHNAME) */
#  endif

/*
 * Linux/BSD malloc introspection (not standard C or POSIX).
 */

#  if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||  \
      defined(__NetBSD__) || defined(__DragonFly__)
#    include <malloc.h>  /* malloc_usable_size() */
#  endif

/*
 * The glibc stub-resolver API (res_query / res_ninit / ns_msg) lives in the
 * dedicated th8_meta_glibc.h meta-header, which its consumers include
 * directly.  It is deliberately NOT pulled in here: it is glibc-only and its
 * one in-tree consumer (harpy/th8_time.c) needs it regardless of whether
 * libunbound is compiled in, whereas this POSIX header is shared by files that
 * neither need nor should drag in <resolv.h>.
 */

#endif /* !_WIN32 && !WIN32 */

#endif /* TH8_META_POSIX_H */
