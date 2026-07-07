/*
 * th8_sqlite3.h -- Export macros for the TH8 SQLite extension.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_SQLITE3_H
#define TH8_SQLITE3_H

#if defined(_WIN32) || defined(__CYGWIN__)
#  define TH8_SQLITE3_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define TH8_SQLITE3_EXPORT __attribute__((visibility("default")))
#else
#  define TH8_SQLITE3_EXPORT
#endif

#endif /* TH8_SQLITE3_H */
