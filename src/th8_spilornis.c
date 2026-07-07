/*
 * th8_spilornis.c -- Spilornis bridge helper functions.
 *
 * This file contains helper functions used by the Spilornis
 * (Eagle list parser) integration.  Functions defined here use
 * the se_* prefixed types to avoid conflicts with Windows SDK
 * types in the amalgamation build.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_spilornis.h"

/*
 * th8_spilornis_isspace --
 *
 *	Narrow-char space classification for the Spilornis parser.
 *	Maps iswspace() to a simple ASCII check since we operate
 *	in UTF-8 mode where all delimiters are ASCII.
 */

int
th8_spilornis_isspace(int c)
{
    return (
        c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
        c == '\v');
}
